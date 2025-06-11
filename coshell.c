/*
 * coshell.c
 *  - CoShell: 터미널 기반 협업 툴 통합 구현
 *    1) ToDo 리스트 관리
 *    2) 실시간 채팅 서버/클라이언트
 *    3) 파일 전송용 QR 코드 생성 & 화면 출력 (전체화면 모드, 분리된 qr.c/qr.h 사용)
 *    4) ncurses UI: 분할 창, 버튼, 로비 → ToDo/Chat/QR 전환
 *
 * 빌드 예시:
 *   gcc coshell.c chat.c todo_ui.c todo_core.c todo_client.c qr.c -o coshell -Wall -O2 -std=c11 -lncursesw -lpthread
 *
 * 사용 패키지 (Ubuntu/Debian):
 *   sudo apt update
 *   sudo apt install -y libncursesw5-dev qrencode
 *
 * 실행 방식:
 *   ./coshell                  # 메뉴/CLI/UI 모드 선택
 *   ./coshell server           # Chat 서버 (Serveo 터널링 포함)
 *   ./coshell add <item>       # CLI 모드: ToDo 추가
 *   ./coshell list             # CLI 모드: ToDo 목록 출력
 *   ./coshell del <index>      # CLI 모드: ToDo 삭제
 *   ./coshell qr <filepath>    # CLI 모드: ASCII QR 출력
 */

#define _POSIX_C_SOURCE 200809L

#include <locale.h>
#include <ncurses.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "chat.h"
#include "todo.h"
#include "qr.h" 
#include"todo_server.h"


#define MAX_CLIENTS   5
#define BUF_SIZE      1024
#define INPUT_HEIGHT  3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511

 // Chat 포트 (로컬)
#define LOCAL_PORT    12345

// Mode constants
#define MODE_LOBBY      0
#define MODE_TODO       1
#define MODE_CHAT       2
#define MODE_QR_INPUT   3
#define MODE_QR_FULL    4
#define MODE_TZ         5

WINDOW* win_time = NULL;  // 왼쪽 상단: 시간 표시
WINDOW* win_custom = NULL;  // 왼쪽 중간/하단: 로비·Chat·QR
WINDOW* win_todo = NULL;  // 오른쪽 전체: ToDo 목록
WINDOW* win_input = NULL;  // 맨 아래: 커맨드 입력창

volatile sig_atomic_t resized = 0;   // 터미널 리사이즈 감지 플래그
volatile int chat_running = 0;       // 채팅 모드 활성화 플래그

// ─── TimeZone 설정용 자료구조 ─────────────────
typedef struct {
    char buf[16];
    int  len;
} TzState;

typedef struct {
    const char *code;   // internal key (안 써도 무방)
    const char *label;  // 화면에 찍을 이름
    int          offset;// UTC로부터의 초 단위 오프셋
} TZOption;

static const TZOption tzOptions[] = {
    { "USA_PT", "USA PT",  -8*3600 },
    { "USA_ET", "USA ET",  -5*3600 },
    { "UK_GMT", "UK GMT",   0      },
    { "FR_CET", "FR CET",  +1*3600 },
    { "RU_MSK", "RU MSK",  +3*3600 },
    { "UAE_GST","UAE GST", +4*3600 },
    { "IN_IST", "IN IST",   5*3600 + 30*60 },
    { "CN_CST", "CN CST",  +8*3600 },
    { "JP_JST", "JP JST",  +9*3600 },
    { "AU_AEST","AU AEST", +10*3600 }
};
static const int tzOptionCount = sizeof(tzOptions)/sizeof(tzOptions[0]);

// 기본값: 첫 번째(USA ET), 두 번째(UK GMT)
static int  tz1_offset = tzOptions[1].offset;
static int  tz2_offset = tzOptions[2].offset;
static char tz1_label[16] = "USA ET";
static char tz2_label[16] = "UK GMT";

// 로비 텍스트
static const char *lobby_text[] = {
    "Welcome!",
    "CoShell, short for \"cooperation in Shell,\" is a terminal-based collaboration toolbox.",
    "",
    "Enter a command below to start collaborating:",
    "",
    "1. To-Do List Management",
    "2. Chat",
    "3. QR Code",
    "4. Setting Time",
    "",
    "You can exit the program at any time by typing exit",
    "",
    "If the screen breaks for a moment, press the space key or minimize and then maximize the window again."
};


static const int lobby_lines = sizeof(lobby_text) / sizeof(lobby_text[0]);

// State structs for modes
typedef struct {
    char buf[512];
    int len;
} TodoState;

typedef struct {
    int step; // 0: host, 1: port, 2: nickname, 3: chat running
    char host[128];
    char port_str[16];
    char nickname[64];
    int port;
} ChatState;

typedef struct {
    int pathlen;
    char pathbuf[MAX_PATH_LEN + 1];
} QRInputState;

/*==============================*/
/*        함수 전방 선언        */
/*==============================*/

// 공통
static void cleanup_ncurses(void);
void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
    const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3);
static void* timer_thread_fn(void* arg);
void update_time(WINDOW* w);

// 모드 처리
static void handle_todo_mode(TodoState* state, int* mode);
static void handle_chat_mode(ChatState* state, int* mode);
static void handle_qr_input_mode(QRInputState* qr_state, int* mode);
static void handle_qr_full_mode(QRInputState* qr_state, int* mode);
static void handle_tz_mode(TzState* state, int* mode);
// 메인/UI/CLI 로직
static void show_main_menu(void);
static void cli_main(int argc, char* argv[]);
static void ui_main(void);

// Serveo 터널 (Chat 서버용)
static int setup_serveo_tunnel(int local_port);

/*==============================*/
/*          main 함수           */
/*==============================*/
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");  // 반드시 locale 설정 (wide char 지원)

    if (argc == 1) {
        show_main_menu();
    }
    else if (strcmp(argv[1], "add") == 0 ||
        strcmp(argv[1], "list") == 0 ||
        strcmp(argv[1], "del") == 0 ||
        strcmp(argv[1], "qr") == 0)
    {
        cli_main(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "ui") == 0) {
        start_todo_server(TODO_SERVER_PORT);
	ui_main();
    }
    else if (strcmp(argv[1], "server") == 0) {
        printf(">> Serveo.net: Chat 서버 원격 포트 요청 중...\n");
        int remote_port = setup_serveo_tunnel(LOCAL_PORT);
        if (remote_port < 0) {
            fprintf(stderr, "Serveo 터널 실패. 로컬 Chat 서버만 실행.\n");
        }
        else {
            printf(">> Serveo Chat 주소: serveo.net:%d → 내부 %d 포트\n", remote_port, LOCAL_PORT);
        }
	start_todo_server(TODO_SERVER_PORT);

        chat_server(LOCAL_PORT);
    }
    else {
        fprintf(stderr, "Invalid mode.\n");
        return 1;
    }

    return 0;
}

/*==============================*/
/*      메인 메뉴 출력 함수     */
/*==============================*/
static void show_main_menu(void) {
    int choice = 0;
    while (1) {
        printf("\033[H\033[J");
        printf("\n===== CoShell Main Menu =====\n");
        printf("1. Run Chat Server (Serveo 터널링)\n");
        printf("2. Run Client (ToDo + Chat UI)\n");
        printf("3. Exit\n");
        printf("Select (1-3): ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        getchar();  // 개행 제거

        if (choice == 1) {
            printf("\033[H\033[J");
            printf("Chat server (Serveo 터널링) 실행 중...\n");
            int remote_port = setup_serveo_tunnel(LOCAL_PORT);
            if (remote_port < 0) {
                fprintf(stderr, "Serveo 터널링 실패. 로컬 Chat 서버 실행.\n");
            }
            else {
                printf(">> Serveo Chat 주소: serveo.net:%d\n", remote_port);
            
	    }
	    start_todo_server(TODO_SERVER_PORT);


            chat_server(LOCAL_PORT);
            break;
        }
        else if (choice == 2) {
            
	    start_todo_server(TODO_SERVER_PORT);
            ui_main();
            break;
        }
        else if (choice == 3) {
            printf("Exiting program.\n");
            break;
        }
        else {
            printf("Invalid selection.\n");
            sleep(1);
        }
    }
}

/*==============================*/
/*        CLI 모드 함수         */
/*==============================*/
static void cli_main(int argc, char* argv[]) {
    if (argc == 0) return;
    load_todo();

    if (strcmp(argv[0], "list") == 0) {
        for (int i = 0;i < todo_count;i++) {
            printf("%d. %s\n", i + 1, todos[i]);
        }
    }
    else if (strcmp(argv[0], "add") == 0 && argc >= 2) {
        char buf[512] = { 0 };
        for (int i = 1;i < argc;i++) {
            strcat(buf, argv[i]);
            if (i < argc - 1) strcat(buf, " ");
        }
        add_todo(buf);
        printf("Added: %s\n", buf);
    }
    else if (strcmp(argv[0], "del") == 0 && argc == 2) {
        int idx = atoi(argv[1]) - 1;
        if (idx < 0 || idx >= todo_count) {
            printf("Invalid index.\n");
            return;
        }
        pthread_mutex_lock(&todo_lock);
        free(todos[idx]);
        for (int i = idx;i < todo_count - 1;i++) {
            todos[i] = todos[i + 1];
        }
        todo_count--;
        FILE* fp = fopen(USER_TODO_FILE, "w");
        if (fp) {
            for (int i = 0;i < todo_count;i++) {
                fprintf(fp, "%s\n", todos[i]);
            }
            fclose(fp);
        }
        pthread_mutex_unlock(&todo_lock);
        printf("Deleted todo #%d\n", idx + 1);
    }
    else if (strcmp(argv[0], "qr") == 0 && argc == 2) {
        show_qr_cli(argv[1]);
    }
    else {
        fprintf(stderr, "Unknown CLI command.\n");
    }
}

/*==============================*/
/*         UI 모드 함수         */
/*==============================*/
static void ui_main(void) {
    setlocale(LC_ALL, "");
    atexit(cleanup_ncurses);
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGWINCH, SIG_IGN);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);    // stdscr에도 KEY_RESIZE 이벤트를 전달
    curs_set(1);

    // 첫 화면: 로비
    create_windows(1);
    load_todo();
    draw_todo(win_todo);

    // State initialization
    TodoState todo_state = { .len = 0 };
    memset(todo_state.buf, 0, sizeof(todo_state.buf));
    ChatState chat_state = { .step = 0, .port = 0 };
    memset(chat_state.host, 0, sizeof(chat_state.host));
    memset(chat_state.port_str, 0, sizeof(chat_state.port_str));
    memset(chat_state.nickname, 0, sizeof(chat_state.nickname));
    QRInputState qr_state = { .pathlen = 0 };
    memset(qr_state.pathbuf, 0, sizeof(qr_state.pathbuf));
    TzState   tz_state = { .len = 0 };

    char cmdbuf[MAX_CMD_LEN + 1] = { 0 };
    int cmdlen = 0;
    time_t last_time = 0;
    int mode = MODE_LOBBY;  // 0 = 로비, 1 = ToDo, 2 = Chat, 3 = QR 입력, 4 = QR 전체화면

    while (1) {
        // (1) 매초 시간 업데이트
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // (2) KEY_RESIZE 체크
        wtimeout(win_input, 0);
        int tch = wgetch(win_input);
        if (tch == KEY_RESIZE) {
            resized = 1;
        }
        // ─────────────────────────────────────────────────
        // (3) 리사이즈 플래그가 세워지면 즉시 화면을 재구성
        // ─────────────────────────────────────────────────
        if (resized) {
            resized = 0;
            endwin();
            refresh();
            clear();

            // ncurses 내부 윈도우를 모두 삭제하고 새로운 크기를 감지
            create_windows(mode == MODE_LOBBY);
            if (mode == MODE_LOBBY) {
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_TODO) {
                // ToDo 모드: redraw ToDo list
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_CHAT) {
                // Chat 모드: 안내 메시지 다시 그려줌
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                wrefresh(win_custom);

                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_QR_INPUT) {
                // QR 모드(경로 입력) 재진입: pathbuf 내용까지 다시 그려줌
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Enter path for QR code (or 'q' to cancel):");
                mvwprintw(win_custom, 2, 2, "%s", qr_state.pathbuf);
                wrefresh(win_custom);

                werase(win_input);
                box(win_input, 0, 0);
                mvwprintw(win_input, 1, 2, "%s", qr_state.pathbuf);
                wmove(win_input, 1, 2 + qr_state.pathlen);
                wrefresh(win_input);

                load_todo();
                draw_todo(win_todo);
            }
            continue;
        }

        // ─────────────────────────────────────────────────
        // (A) QR 경로 입력 모드 (mode == 3)
        // ─────────────────────────────────────────────────
        if (mode == MODE_QR_INPUT) {
            handle_qr_input_mode(&qr_state, &mode);
            continue;
        }

        // ─────────────────────────────────────────────────
        // (B) QR 전체화면 모드 (mode == 4)
        // ─────────────────────────────────────────────────
        if (mode == MODE_QR_FULL) {
            handle_qr_full_mode(&qr_state, &mode);
            continue;
        }

        // ─────────────────────────────────────────────────
        // (C) ToDo 모드
        // ─────────────────────────────────────────────────
        if (mode == MODE_TODO) {
            handle_todo_mode(&todo_state, &mode);
            continue;
        }

        // ─────────────────────────────────────────────────
        // (D) Chat 모드 (호스트/포트/닉네임 입력 및 실행)
        // ─────────────────────────────────────────────────
        if (mode == MODE_CHAT) {
            handle_chat_mode(&chat_state, &mode);
            continue;
        }
        if (mode == MODE_TZ) {
            handle_tz_mode(&tz_state, &mode);
            continue;
        }

        // ─────────────────────────────────────────────────
        // (E) 나머지 모드: 로비 (mode == 0)
        // ─────────────────────────────────────────────────

        // (E-1) 입력창(Command) 그리기
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "Command: %.*s", cmdlen, cmdbuf);
        wmove(win_input, 1, 11 + cmdlen);
        wrefresh(win_input);

        // (E-2) 비차단으로 키 입력 받기 (200ms 대기)
        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) {
            resized = 1;
            continue;
        }

        if (ch != ERR) {
            // 백스페이스
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                }
            }
            // Enter 입력
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';

                // exit → 프로그램 종료
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;
                }
                // 1 → To-Do 모드 진입
                else if (cmdlen > 0 && cmdbuf[0] == '1') {
                    mode = MODE_TODO;
                    todo_state.len = 0;
                    memset(todo_state.buf, 0, sizeof(todo_state.buf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // 2 → Chat 모드 진입
                else if (cmdlen > 0 && cmdbuf[0] == '2') {
                    mode = MODE_CHAT;
                    chat_state.step = 0;
                    chat_state.port = 0;
                    memset(chat_state.host, 0, sizeof(chat_state.host));
                    memset(chat_state.port_str, 0, sizeof(chat_state.port_str));
                    memset(chat_state.nickname, 0, sizeof(chat_state.nickname));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // 3 → QR 경로 입력 모드 진입
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
                    mode = MODE_QR_INPUT;
                    qr_state.pathlen = 0;
                    memset(qr_state.pathbuf, 0, sizeof(qr_state.pathbuf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                else if (cmdlen > 0 && cmdbuf[0] == '4') {
                    mode = MODE_TZ;
                    tz_state.len = 0;
                    memset(tz_state.buf, 0, sizeof(tz_state.buf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }



                // a <item> → ToDo 항목 추가 (비대화형 모드)
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char* item = cmdbuf + 2;
                    add_todo(item);
                    draw_todo(win_todo);
                }
                // f <filepath> → QR 전체화면 모드 바로 실행
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char* filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                    create_windows(1);
                    load_todo();
                    draw_todo(win_todo);
                }
                else {
                    /*==============================*/
                    /*    Unknown command 처리      */
                    /*==============================*/
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Unknown command: %s", cmdbuf);
                    mvwprintw(win_custom, 3, 2, "Available commands in main UI:");
                    mvwprintw(win_custom, 4, 4, "1             : Enter To-Do mode");
                    mvwprintw(win_custom, 5, 4, "2             : Enter Chat mode");
                    mvwprintw(win_custom, 6, 4, "3             : Enter QR mode");
                    mvwprintw(win_custom, 7, 4, "a <item>      : Add To-Do (non-interactive)");
                    mvwprintw(win_custom, 8, 4, "f <filepath>  : Show QR for <filepath>");
                    mvwprintw(win_custom, 9, 4, "exit          : Exit program");
                    wrefresh(win_custom);
                    napms(3000);  // 3초간 표시한 뒤 자동으로 로비로 돌아감

                    mode = MODE_LOBBY;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    int maxy, maxx;
                    getmaxyx(win_custom, maxy, maxx);
                    int inner_lines = maxy - 2;
                    int inner_cols = maxx - 4;
                    print_wrapped_lines(win_custom, 1, inner_lines, inner_cols,
                        lobby_text, lobby_lines);
                    wrefresh(win_custom);
                }

                // 입력 버퍼 초기화
                cmdlen = 0;
                memset(cmdbuf, 0, sizeof(cmdbuf));
            }
            else if (ch >= 32 && ch <= 126) {
                if (cmdlen < MAX_CMD_LEN) {
                    cmdbuf[cmdlen++] = (char)ch;
                }
            }
        }
    }

    endwin();  // ncurses 종료
	  endwin();
	  echo();
	  nocbreak();
	  curs_set(1);
}

/* Handle ToDo mode input */
static void handle_todo_mode(TodoState* state, int* mode) {
    // (1) 매 프레임: 도움말과 ToDo 리스트 다시 그리기
    werase(win_custom);
    box(win_custom, 0, 0);
    draw_custom_help(win_custom);      // todo.c에 정의된 도움말 함수
    werase(win_todo);
    box(win_todo, 0, 0);
    load_todo();                       // 파일 → 메모리 로드
    draw_todo(win_todo);               // 메모리 → 화면 출력
    wrefresh(win_custom);
    wrefresh(win_todo);

    // (2) 입력창 그리기
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", state->buf);
    wmove(win_input, 1, 2 + state->len);
    wrefresh(win_input);

    // (3) 입력 처리
    wtimeout(win_input, 200);
    int ch = wgetch(win_input);
    if (ch == KEY_RESIZE) {
        resized = 1;
        return;
    }
    if (ch == ERR) return;

    if (ch == KEY_BACKSPACE || ch == 127) {
        if (state->len > 0) {
            state->buf[--state->len] = '\0';
        }
    }
    else if (ch == '\n' || ch == KEY_ENTER) {
        state->buf[state->len] = '\0';
        char* cmd = state->buf;

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "Q") == 0) {
            // 로비로 돌아가기
            *mode = MODE_LOBBY;
            create_windows(1);
            load_todo();
            draw_todo(win_todo);
            return;  // 여기서 즉시 리턴하여 메인 UI 초기화 화면 유지
        }
        else if (strcmp(cmd, "team") == 0) {
            switch_to_team_mode(win_custom, win_todo);
        }
        else if (strcmp(cmd, "user") == 0) {
            switch_to_user_mode(win_custom, win_todo);
        }
        else if (strncmp(cmd, "add ", 4) == 0) {
            add_todo(cmd + 4);
        }
        else if (strncmp(cmd, "done ", 5) == 0) {
            int idx = atoi(cmd + 5);
            done_todo(idx);
        }
        else if (strncmp(cmd, "undo ", 5) == 0) {
            int idx = atoi(cmd + 5);
            undo_todo(idx);
        }
        else if (strncmp(cmd, "del ", 4) == 0) {
            int idx = atoi(cmd + 4);
            del_todo(idx);
        }
        else if (strncmp(cmd, "edit ", 5) == 0) {
            char* p = strchr(cmd + 5, ' ');
            if (p) {
                *p = '\0';
                int idx = atoi(cmd + 5);
                char* text = p + 1;
                edit_todo(idx, text);
            }
            else {
                mvwprintw(win_custom, 8, 2, "Usage: edt <num> <new text>");
                wrefresh(win_custom);
                napms(1000);
            }
        }
        else {
            mvwprintw(win_custom, 8, 2, "Unknown: %s", cmd);
            wrefresh(win_custom);
            napms(1000);
        }

        // (4) 변경 후 다시 그리기
        werase(win_custom);
        box(win_custom, 0, 0);
        draw_custom_help(win_custom);
        load_todo();
        draw_todo(win_todo);
        werase(win_input);
        box(win_input, 0, 0);
        wrefresh(win_input);

        // 입력 버퍼 초기화
        state->len = 0;
        memset(state->buf, 0, sizeof(state->buf));
    }
    else if (ch >= 32 && ch <= 126) {
        if (state->len < (int)sizeof(state->buf) - 1) {
            state->buf[state->len++] = (char)ch;
        }
    }
}

/* Handle Chat mode (host/port/nickname and run) */
static void handle_chat_mode(ChatState* state, int* mode) {
    // Step 0: Host
    if (state->step == 0) {
        werase(win_custom);
        box(win_custom, 0, 0);
        mvwprintw(win_custom, 1, 2, "Enter Chat host:");
        wrefresh(win_custom);

        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "%s", state->host);
        wmove(win_input, 1, 2 + strlen(state->host));
        wrefresh(win_input);

        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) { resized = 1; return; }
        if (ch == ERR) return;

        if (ch == KEY_BACKSPACE || ch == 127) {
            int len = strlen(state->host);
            if (len > 0) {
                state->host[len - 1] = '\0';
            }
        }
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (strlen(state->host) == 0) {
                mvwprintw(win_custom, 2, 2, "Host cannot be empty. Try again.");
                wrefresh(win_custom);
                napms(1000);
            }
            else {
                state->step = 1;
            }
        }
        else if (ch >= 32 && ch <= 126) {
            int len = strlen(state->host);
            if (len < (int)sizeof(state->host) - 1) {
                state->host[len] = (char)ch;
                state->host[len + 1] = '\0';
            }
        }
    }
    // Step 1: Port
    else if (state->step == 1) {
        werase(win_custom);
        box(win_custom, 0, 0);
        mvwprintw(win_custom, 1, 2, "Enter Chat port (or 'q' to cancel):");
        wrefresh(win_custom);

        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "%s", state->port_str);
        wmove(win_input, 1, 2 + strlen(state->port_str));
        wrefresh(win_input);

        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) { resized = 1; return; }
        if (ch == ERR) return;

        if (ch == KEY_BACKSPACE || ch == 127) {
            int len = strlen(state->port_str);
            if (len > 0) {
                state->port_str[len - 1] = '\0';
            }
        }
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (strlen(state->port_str) == 0) {
                mvwprintw(win_custom, 2, 2, "Port cannot be empty. Try again.");
                wrefresh(win_custom);
                napms(1000);
            }
            else if (strcmp(state->port_str, "q") == 0 || strcmp(state->port_str, "Q") == 0) {
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Cancelled Chat. Returning to main UI...");
                wrefresh(win_custom);
                napms(1000);
                *mode = MODE_LOBBY;
                create_windows(1);
                load_todo();
                draw_todo(win_todo);
            }
            else {
                state->port = atoi(state->port_str);
                if (state->port <= 0 || state->port > 65535) {
                    mvwprintw(win_custom, 2, 2, "Invalid port. Try again (or 'q').");
                    wrefresh(win_custom);
                    napms(1000);
                }
                else {
                    state->step = 2;
                }
            }
        }
        else if (ch >= 32 && ch <= 126) {
            int len = strlen(state->port_str);
            if (len < (int)sizeof(state->port_str) - 1) {
                state->port_str[len] = (char)ch;
                state->port_str[len + 1] = '\0';
            }
        }
    }
    // Step 2: Nickname
    else if (state->step == 2) {
        werase(win_custom);
        box(win_custom, 0, 0);
        mvwprintw(win_custom, 1, 2, "Enter Nickname (no spaces):");
        wrefresh(win_custom);

        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "%s", state->nickname);
        wmove(win_input, 1, 2 + strlen(state->nickname));
        wrefresh(win_input);

        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) { resized = 1; return; }
        if (ch == ERR) return;

        if (ch == KEY_BACKSPACE || ch == 127) {
            int len = strlen(state->nickname);
            if (len > 0) {
                state->nickname[len - 1] = '\0';
            }
        }
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (strlen(state->nickname) == 0) {
                mvwprintw(win_custom, 2, 2, "Nickname cannot be empty. Try again.");
                wrefresh(win_custom);
                napms(1000);
            }
            else {
                state->step = 3;
            }
        }
        else if (ch >= 32 && ch <= 126) {
            int len = strlen(state->nickname);
            if (len < (int)sizeof(state->nickname) - 1) {
                state->nickname[len] = (char)ch;
                state->nickname[len + 1] = '\0';
            }
        }
    }
    // Step 3: Start chat client
    else if (state->step == 3) {
        werase(win_custom);
        box(win_custom, 0, 0);
        mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
        wrefresh(win_custom);

        pthread_t timer_thread_id;
        chat_running = 1;
        if (pthread_create(&timer_thread_id, NULL, timer_thread_fn, NULL) != 0) {
            // 실패해도 무시
        }
        chat_client(state->host, state->port, state->nickname, win_custom, win_input);
        chat_running = 0;
        pthread_join(timer_thread_id, NULL);

        // 채팅 모드 종료 후 → 메인 UI로 복귀
        state->step = 0;
        memset(state->host, 0, sizeof(state->host));
        memset(state->port_str, 0, sizeof(state->port_str));
        memset(state->nickname, 0, sizeof(state->nickname));
        // argv[0]부터 프로그램 전체를 execvp로 덮어쓴다
	cleanup_ncurses();
        char* argv_new[] = { "./coshell", "ui", NULL };
        execvp(argv_new[0], argv_new);

    }
}

/*      Handle QR path input mode     */
static void handle_qr_input_mode(QRInputState* qr_state, int* mode) {
    werase(win_custom);
    box(win_custom, 0, 0);
    mvwprintw(win_custom, 1, 2, "Enter path for QR code (or 'q' to cancel):");
    mvwprintw(win_custom, 2, 2, "%s", qr_state->pathbuf);
    wrefresh(win_custom);

    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", qr_state->pathbuf);
    wmove(win_input, 1, 2 + qr_state->pathlen);
    wrefresh(win_input);

    wtimeout(win_input, 200);
    int ch = wgetch(win_input);
    if (ch == KEY_RESIZE) { resized = 1; return; }
    if (ch == ERR) return;

    if (ch == KEY_BACKSPACE || ch == 127) {
        if (qr_state->pathlen > 0) {
            qr_state->pathlen--;
            qr_state->pathbuf[qr_state->pathlen] = '\0';
        }
    }
    else if (ch == '\n' || ch == KEY_ENTER) {
        if (qr_state->pathlen > 0) {
            *mode = MODE_QR_FULL;
            werase(win_input);
            box(win_input, 0, 0);
            wrefresh(win_input);
        }
    }
    else if (ch == 'q' || ch == 'Q') {
        *mode = MODE_LOBBY;
        create_windows(1);
        load_todo();
        draw_todo(win_todo);
    }
    else if (ch >= 32 && ch <= 126) {
        if (qr_state->pathlen < MAX_PATH_LEN) {
            qr_state->pathbuf[qr_state->pathlen++] = (char)ch;
            qr_state->pathbuf[qr_state->pathlen] = '\0';
        }
    }
}


static void handle_qr_full_mode(QRInputState* qr_state, int* mode) {
    // 1) curses 기반으로 전체화면 QR 그리기 (내부에서 'q'를 기다렸다가 리턴)
    process_and_show_file(win_custom, qr_state->pathbuf);

    // 2) ncurses 윈도우/모드 정리
    cleanup_ncurses();

    // 3) execvp로 UI 다시 실행 (로비)
    char* argv_new[] = { "./coshell", "ui", NULL };
    execvp(argv_new[0], argv_new);

    // execvp 실패 시
    perror("execvp failed");
    exit(1);
}


/*==============================*/
/*   ncurses 초기화/정리/리사이즈 */
/*==============================*/
static void cleanup_ncurses(void) {
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }
    endwin();

}

/*==============================*/
/*   Time 문자열 생성 함수     */
/*==============================*/
static void get_time_strings(char* local_buf, int len1,
    char* tz1_buf, int len2,
    char* tz2_buf, int len3)
{
    time_t t = time(NULL);

    // 1) 로컬
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    // 2) 첫 번째 원격 타임존
    time_t t1 = t + tz1_offset;
    struct tm tm1 = *gmtime(&t1);
    snprintf(tz1_buf, len2,
        "%04d-%02d-%02d %02d:%02d:%02d (%s)",
        tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday,
        tm1.tm_hour, tm1.tm_min, tm1.tm_sec,
        tz1_label);

    // 3) 두 번째 원격 타임존
    time_t t2 = t + tz2_offset;
    struct tm tm2 = *gmtime(&t2);
    snprintf(tz2_buf, len3,
        "%04d-%02d-%02d %02d:%02d:%02d (%s)",
        tm2.tm_year + 1900, tm2.tm_mon + 1, tm2.tm_mday,
        tm2.tm_hour, tm2.tm_min, tm2.tm_sec,
        tz2_label);
}


void update_time(WINDOW* w) {
    char local_buf[32], tz1_buf[128], tz2_buf[128];
    get_time_strings(local_buf, sizeof(local_buf),
        tz1_buf, sizeof(tz1_buf),
        tz2_buf, sizeof(tz2_buf));

    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Time ");
    mvwprintw(w, 1, 2, "Local: %s", local_buf);
    mvwprintw(w, 2, 2, "%s: %s", tz1_label, tz1_buf);
    mvwprintw(w, 3, 2, "%s: %s", tz2_label, tz2_buf);
    wrefresh(w);
}

// 매초 윈도우의 시간을 업데이트해 주는 스레드 함수 (Chat 모드에서만 사용)
static void* timer_thread_fn(void* arg) {
    (void)arg;
    while (chat_running) {
        update_time(win_time);
        sleep(1);
    }
    return NULL;
}

// 주어진 문자열 배열(lines)를 max_cols 폭에 맞춰 win에 출력
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
    const char* lines[], int n)
{
    int row = start_y;
    for (int i = 0; i < n && row < start_y + max_lines; i++) {
        const char* text = lines[i];
        int len = text ? (int)strlen(text) : 0;
        int offset = 0;
        if (len == 0) {
            if (row < start_y + max_lines) {
                mvwaddch(win, row++, 2, ' ');
            }
            continue;
        }
        while (offset < len && row < start_y + max_lines) {
            int chunk = (len - offset > max_cols ? max_cols : len - offset);
            mvwprintw(win, row++, 2, "%.*s", chunk, text + offset);
            offset += chunk;
        }
    }
}

// 윈도우들을 새로 생성/배치
void create_windows(int in_lobby) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (1) 기존 윈도우 삭제
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }

    // (2) 크기 계산
    int left_width = cols / 2;
    int right_width = cols - left_width;
    int left_height = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height = 5;    // border(2) + 라벨(1) + 시간 3줄 = 5
    int custom_y = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) 상단 타이틀
    mvprintw(0, 0, "<< CoShell >>");
    refresh();

    // (4) 왼쪽 상단: Time 표시 창
    win_time = newwin(time_height, left_width, title_height, 0);
    // (5) 왼쪽 중간/하단: 로비 또는 모드 콘텐츠
    win_custom = newwin(custom_height, left_width, custom_y, 0);
    // (6) 오른쪽 전체: ToDoList 창
    win_todo = newwin(rows - INPUT_HEIGHT, right_width, 0, left_width);
    // (7) 맨 아래: Command 입력창
    win_input = newwin(INPUT_HEIGHT, cols, rows - INPUT_HEIGHT, 0);

    // (8) 스크롤 설정
    scrollok(win_time, FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo, TRUE);

    // (9) Time 창 초기화
    box(win_time, 0, 0);
    mvwprintw(win_time, 0, 2, " Time ");
    mvwprintw(win_time, 1, 2, "Local:    --:--:--");
    mvwprintw(win_time, 2, 2, "USA  :    --:--:--");
    mvwprintw(win_time, 3, 2, "UK   :    --:--:--");
    wrefresh(win_time);

    // (10) Custom 창(로비 또는 모드)
    box(win_custom, 0, 0);
    if (in_lobby) {
        int maxy, maxx;
        getmaxyx(win_custom, maxy, maxx);
        int inner_lines = maxy - 2;
        int inner_cols = maxx - 4;
        print_wrapped_lines(win_custom, 1, inner_lines, inner_cols,
            lobby_text, lobby_lines);
    }
    wrefresh(win_custom);

    // (11) ToDoList 창 초기화
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 1, 2, "=== ToDo List ===");
    wrefresh(win_todo);

    // (12) 입력창 초기화
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "Command: ");
    wrefresh(win_input);
}

/*==============================*/
/*   Serveo 터널 (Chat 서버용)  */
/*==============================*/
static int setup_serveo_tunnel(int local_port) {
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]); close(pipe_fd[1]);
        return -1;
    }
    else if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        char forward_arg[64];
        snprintf(forward_arg, sizeof(forward_arg), "0:localhost:%d", local_port);
        execlp("ssh", "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "ServerAliveInterval=60",
            "-N", "-R", forward_arg,
            "serveo.net",
            (char*)NULL);
        perror("execlp");
        _exit(1);
    }
    else {
        close(pipe_fd[1]);
        FILE* fp = fdopen(pipe_fd[0], "r");
        if (!fp) { close(pipe_fd[0]); return -1; }
        char line[512];
        int remote_port = -1;
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "Allocated port %d", &remote_port) == 1) {
                printf(">> Serveo 터널 할당 완료: serveo.net:%d\n", remote_port);
                fflush(stdout);
                break;
            }
        }
        fclose(fp);
        return remote_port;
    }
}


static void handle_tz_mode(TzState* state, int* mode) {
    int row = 1;
    werase(win_custom);
    box(win_custom, 0, 0);
    mvwprintw(win_custom, row++, 2, "4. Setting TimeZone");
    mvwprintw(win_custom, row++, 2, "Usage: <slot> <option#>  (slot:1 or 2)");
    mvwprintw(win_custom, row++, 2, "or 'q' to cancel");
    mvwprintw(win_custom, row++, 2, "Available options:");
    for (int i = 0; i < tzOptionCount; i++) {
        mvwprintw(win_custom, row++, 2, "%2d. %s", i + 1, tzOptions[i].label);
    }
    mvwprintw(win_custom, row++, 2, "%s", state->buf);
    wrefresh(win_custom);

    // 입력창
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", state->buf);
    wmove(win_input, 1, 2 + state->len);
    wrefresh(win_input);

    wtimeout(win_input, 200);
    int ch = wgetch(win_input);
    if (ch == KEY_RESIZE) { resized = 1; return; }
    if (ch == ERR)        return;

    if (ch == KEY_BACKSPACE || ch == 127) {
        if (state->len > 0) state->buf[--state->len] = '\0';
    }
    else if (ch == '\n' || ch == KEY_ENTER) {
        if (state->len == 0) return;
        if (state->buf[0] == 'q' || state->buf[0] == 'Q') {
            *mode = MODE_LOBBY;
            create_windows(1);
            load_todo(); draw_todo(win_todo);
            return;
        }
        // tokenizing
        char tmp[16];
        strcpy(tmp, state->buf);
        int slot, opt;
        if (sscanf(tmp, "%d %d", &slot, &opt) == 2
            && (slot == 1 || slot == 2) && opt >= 1 && opt <= tzOptionCount) {
            // 적용
            if (slot == 1) {
                tz1_offset = tzOptions[opt - 1].offset;
                snprintf(tz1_label, sizeof(tz1_label), "%s", tzOptions[opt - 1].label);

            }
            else {
                tz2_offset = tzOptions[opt - 1].offset;
                snprintf(tz2_label, sizeof(tz2_label), "%s", tzOptions[opt - 1].label);

            }
            mvwprintw(win_custom, row + 1, 2, "Slot %d set to %s", slot, tzOptions[opt - 1].label);
            wrefresh(win_custom);
            napms(1000);
            *mode = MODE_LOBBY;
            create_windows(1);
            load_todo(); draw_todo(win_todo);
        }
        else {
            mvwprintw(win_custom, row + 1, 2, "Invalid input. Try again.");
            wrefresh(win_custom);
            napms(1000);
            state->len = 0;
            state->buf[0] = '\0';
        }
    }
    else if (ch >= 32 && ch <= 126) {
        if (state->len < (int)sizeof(state->buf) - 1) {
            state->buf[state->len++] = (char)ch;
            state->buf[state->len] = '\0';
        }
    }
}
