/*
 * coshell.c
 *  - CoShell: 터미널 기반 협업 툴 통합 구현
 *    1) ToDo 리스트 관리
 *    2) 실시간 채팅 서버/클라이언트
 *    3) 파일 전송용 QR 코드 생성 & 화면 출력 (전체화면 모드)
 *    4) ncurses UI: 분할 창, 버튼, 로비 → ToDo/Chat/QR 전환
 *
 * 빌드 예시:
 *   gcc coshell.c chat.c -o coshell -Wall -O2 -std=c11 -lncursesw -lpthread
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
 *   ./coshell qr <filepath>    # CLI 모드: QR 출력
 *
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
#include <sys/stat.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include "chat.h"

#define MAX_CLIENTS   5
#define BUF_SIZE      1024
#define TODO_FILE     "tasks_personal.txt"
#define MAX_TODO      100
#define INPUT_HEIGHT  3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511
#define MAX_QR_BYTES  800

// Chat 포트 (로컬)
#define LOCAL_PORT    12345

/*==============================*/
/*   전역 변수 (공유 데이터)    */
/*==============================*/

// ncurses 윈도우 포인터
static WINDOW *win_time    = NULL;  // 왼쪽 상단: 시간
static WINDOW *win_custom  = NULL;  // 왼쪽 중간/하단: 로비·Chat·QR
static WINDOW *win_todo    = NULL;  // 오른쪽 전체: ToDo 목록
static WINDOW *win_input   = NULL;  // 맨 아래: 커맨드 입력창

// ToDo 리스트 전역 데이터
static pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;
static char *todos[MAX_TODO];
static int todo_count = 0;

// 로비 텍스트
static const char* lobby_text[] = {
    "Welcome!",
    "CoShell: terminal-based collaboration toolbox.",
    "1. To-Do List Management",
    "2. Chat",
    "3. QR Code",
    "",
    "Type 'exit' to quit at any time."
};
static const int lobby_lines = sizeof(lobby_text)/sizeof(lobby_text[0]);

/*==============================*/
/*    함수 전방 선언             */
/*==============================*/

// 공통
static void cleanup_ncurses(void);
static void handle_winch(int sig);
static void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
                                const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
                             char* us_buf, int len2,
                             char* uk_buf, int len3);
static void update_time(WINDOW* w);
static int pick_version_for_module(int max_module);
static void show_qrcode_fullscreen(const char* path);
static void process_and_show_file(WINDOW* custom, const char* path);

// 메인/UI/CLI 로직
static void show_main_menu(void);
static void cli_main(int argc, char *argv[]);
static void ui_main(void);
static void load_todo(void);
static void draw_todo(WINDOW *win_todo);
static void add_todo(const char *item);
static void show_qr_cli(const char *filename);

// Serveo 터널 (Chat 서버용)
static int setup_serveo_tunnel(int local_port);

/*==============================*/
/*        main 함수             */
/*==============================*/
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    if (argc == 1) {
        show_main_menu();
    }
    else if (strcmp(argv[1], "add") == 0 ||
             strcmp(argv[1], "list")== 0 ||
             strcmp(argv[1], "del") == 0 ||
             strcmp(argv[1], "qr")  == 0)
    {
        cli_main(argc-1, &argv[1]);
    }
    else if (strcmp(argv[1], "ui") == 0) {
        ui_main();
    }
    else if (strcmp(argv[1], "server") == 0) {
        // Chat 서버 + Serveo 터널
        printf(">> Serveo.net: Chat 서버 원격 포트 요청 중...\n");
        int remote_port = setup_serveo_tunnel(LOCAL_PORT);
        if (remote_port < 0) {
            fprintf(stderr, "Serveo 터널 실패. 로컬 Chat 서버만 실행.\n");
        } else {
            printf(">> Serveo Chat 주소: serveo.net:%d → 내부 %d 포트\n", remote_port, LOCAL_PORT);
        }
        chat_server(LOCAL_PORT);
    }
    else {
        fprintf(stderr, "Invalid mode.\n");
        return 1;
    }
    return 0;
}

/*==============================*/
/*   메인 메뉴 출력 함수       */
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

        if (scanf("%d", &choice)!=1) {
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
            } else {
                printf(">> Serveo Chat 주소: serveo.net:%d\n", remote_port);
            }
            chat_server(LOCAL_PORT);
            break;
        }
        else if (choice == 2) {
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
/*       CLI 모드 함수          */
/*==============================*/
static void cli_main(int argc, char *argv[]) {
    if (argc == 0) return;
    load_todo();

    if (strcmp(argv[0], "list")==0) {
        for (int i=0;i<todo_count;i++) {
            printf("%d. %s\n", i+1, todos[i]);
        }
    }
    else if (strcmp(argv[0], "add")==0 && argc>=2) {
        char buf[512] = {0};
        for (int i=1;i<argc;i++) {
            strcat(buf, argv[i]);
            if (i<argc-1) strcat(buf," ");
        }
        add_todo(buf);
        printf("Added: %s\n", buf);
    }
    else if (strcmp(argv[0], "del")==0 && argc==2) {
        int idx = atoi(argv[1]) - 1;
        if (idx<0 || idx>=todo_count) {
            printf("Invalid index.\n");
            return;
        }
        pthread_mutex_lock(&todo_lock);
        free(todos[idx]);
        for (int i=idx;i<todo_count-1;i++) {
            todos[i] = todos[i+1];
        }
        todo_count--;
        FILE *fp = fopen(TODO_FILE,"w");
        if (fp) {
            for (int i=0;i<todo_count;i++) {
                fprintf(fp, "%s\n", todos[i]);
            }
            fclose(fp);
        }
        pthread_mutex_unlock(&todo_lock);
        printf("Deleted todo #%d\n", idx+1);
    }
    else if (strcmp(argv[0], "qr")==0 && argc==2) {
        show_qr_cli(argv[1]);
    }
    else {
        fprintf(stderr, "Unknown CLI command.\n");
    }
}

/*==============================*/
/*      UI 모드 함수            */
/*==============================*/
static void ui_main(void) {
    setlocale(LC_ALL, "");
    atexit(cleanup_ncurses);
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGWINCH, handle_winch);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    // 첫 화면: 로비
    create_windows(1);

    char cmdbuf[MAX_CMD_LEN + 1] = {0};
    int cmdlen = 0;
    char pathbuf[MAX_PATH_LEN + 1] = {0};
    int pathlen = 0;
    time_t last_time = 0;
    int mode = 0;  // 0=로비,1=ToDo,2=Chat,3=QR 입력,4=QR 전체화면

    int input_y = 1, input_x = 10;

    while (1) {
        // 매초 시간 업데이트
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // (A) QR 경로 입력 모드
        if (mode == 3) {
            werase(win_custom);
            box(win_custom, 0, 0);
            mvwprintw(win_custom, 1, 2, "Enter path for QR code:");
            mvwprintw(win_custom, 2, 2, "%s", pathbuf);
            wrefresh(win_custom);

            werase(win_input);
            box(win_input, 0, 0);
            mvwprintw(win_input, input_y, input_x, "%s", pathbuf);
            wmove(win_input, input_y, input_x + pathlen);
            wrefresh(win_input);

            wtimeout(win_input, 200);
            int ch = wgetch(win_input);
            if (ch != ERR) {
                if (ch == KEY_BACKSPACE || ch == 127) {
                    if (pathlen > 0) {
                        pathlen--;
                        pathbuf[pathlen] = '\0';
                    }
                }
                else if (ch == '\n' || ch == KEY_ENTER) {
                    mode = 4;
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                else if (ch >= 32 && ch <= 126) {
                    if (pathlen < MAX_PATH_LEN) {
                        pathbuf[pathlen++] = (char)ch;
                    }
                }
            }
            continue;
        }

        // (B) QR 전체화면 모드
        if (mode == 4) {
            process_and_show_file(win_custom, pathbuf);
            mode = 0;
            continue;
        }

        // (C) 나머지 모드: 로비/ToDo/Chat/Command
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "Command: %.*s", cmdlen, cmdbuf);
        wmove(win_input, input_y, input_x + cmdlen);
        wrefresh(win_input);

        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch != ERR) {
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                }
            }
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;  // 메인 UI 종료
                }
                else if (cmdbuf[0] == '1') {
                    mode = 1;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Entering To-Do List mode...");
                    wrefresh(win_custom);
                }
                // (ui_main 안의 '2' 선택 시 채팅 모드 진입 부분)
                else if (cmdbuf[0] == '2') {
                    // (A) Chat 모드 진입 안내
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter Chat host and port:");
                    wrefresh(win_custom);

                    // (B) 호스트 입력
                    char host[128] = {0};
                    while (1) {
                        werase(win_input);
                        box(win_input, 0, 0);
                        mvwprintw(win_input, 1, 2, "Host: ");
                        wmove(win_input, 1, 8);
                        wrefresh(win_input);

                        // 블로킹 모드로 전환하여 입력 대기
                        wtimeout(win_input, -1);
                        echo();
                        flushinp();  // ncurses 내부에 남은 입력 버퍼 비우기
                        wgetnstr(win_input, host, sizeof(host) - 1);
                        noecho();

                        // 빈 문자열이면 재입력 요구
                        if (strlen(host) == 0) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Host cannot be empty. Try again.");
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        break;
                    }

                    // (C) 포트 입력
                    char port_str[16] = {0};
                    int port = 0;
                    while (1) {
                        werase(win_input);
                        box(win_input, 0, 0);
                        mvwprintw(win_input, 1, 2, "Port: ");
                        wmove(win_input, 1, 8);
                        wrefresh(win_input);

                        // 블로킹 모드로 전환하여 입력 대기
                        wtimeout(win_input, -1);
                        echo();
                        flushinp();
                        wgetnstr(win_input, port_str, sizeof(port_str) - 1);
                        noecho();

                        if (strlen(port_str) == 0) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Port cannot be empty. Try again.");
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        // 포트가 숫자가 아니거나 1~65535 범위가 아니면 재입력
                        port = atoi(port_str);
                        if (port <= 0 || port > 65535) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Invalid port: %s. Try again.", port_str);
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        break;
                    }

                    // (D) 닉네임 입력
                    char nickname[64] = {0};
                    while (1) {
                        werase(win_custom);
                        box(win_custom, 0, 0);
                        mvwprintw(win_custom, 1, 2, "Enter Nickname (no spaces):");
                        wrefresh(win_custom);

                        werase(win_input);
                        box(win_input, 0, 0);
                        mvwprintw(win_input, 1, 2, "Nickname: ");
                        wmove(win_input, 1, 12);
                        wrefresh(win_input);

                        wtimeout(win_input, -1);
                        echo();
                        flushinp();
                        wgetnstr(win_input, nickname, sizeof(nickname) - 1);
                        noecho();

                        if (strlen(nickname) == 0) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Nickname cannot be empty. Try again.");
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        break;
                    }

                    // (E) 채팅 모드 진입 전, 안내 메시지
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                    wrefresh(win_custom);

                    // (F) 채팅 모드 진입: 채팅창으로 win_custom, 입력창으로 win_input 사용
                    chat_client(host, port, nickname, win_custom, win_input);

                    // (G) 채팅 모드 종료 후 → 메인 UI(로비)로 돌아가기
                    werase(win_custom);
                    create_windows(1);
                }
                else if (cmdbuf[0] == '3') {
                    mode = 3;
                    pathlen = 0;
                    memset(pathbuf, 0, sizeof(pathbuf));
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter path for QR code:");
                    wrefresh(win_custom);
                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                else if (cmdbuf[0] == 'a' && cmdbuf[1] == ' ' && cmdlen > 2) {
                    const char *item = cmdbuf + 2;
                    add_todo(item);
                    draw_todo(win_todo);
                }
                else if (cmdbuf[0] == 'f' && cmdbuf[1] == ' ' && cmdlen > 2) {
                    const char *filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                }
                else {
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Unknown command: %s", cmdbuf);
                    wrefresh(win_custom);
                    napms(2000);
                    mode = 0;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    int maxy, maxx;
                    getmaxyx(win_custom, maxy, maxx);
                    int inner_lines = maxy - 2;
                    int inner_cols = maxx - 4;
                    print_wrapped_lines(win_custom, 1, inner_lines, inner_cols, lobby_text, lobby_lines);
                    wrefresh(win_custom);
                }

                // 입력 버퍼 초기화
                cmdlen = 0;
                memset(cmdbuf, 0, sizeof(cmdbuf));
                werase(win_input);
                box(win_input, 0, 0);
                mvwprintw(win_input, 1, 2, "Command: ");
                wrefresh(win_input);
            }
            else if (ch >= 32 && ch <= 126) {
                if (cmdlen < MAX_CMD_LEN) {
                    cmdbuf[cmdlen++] = (char)ch;
                }
            }
        }
    }

    endwin();  // ncurses 종료
}


/*==============================*/
/*   ToDo – 파일 ↔ 메모리 로직   */
/*==============================*/
static void load_todo(void) {
    FILE *fp = fopen(TODO_FILE, "r");
    if (!fp) return;
    pthread_mutex_lock(&todo_lock);
    todo_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && todo_count < MAX_TODO) {
        line[strcspn(line, "\n")] = '\0';
        todos[todo_count++] = strdup(line);
    }
    pthread_mutex_unlock(&todo_lock);
    fclose(fp);
}

static void draw_todo(WINDOW *win_todo) {
    pthread_mutex_lock(&todo_lock);
    werase(win_todo);
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 0, 2, " ToDo List ");
    for (int i = 0; i < todo_count; i++) {
        mvwprintw(win_todo, i + 1, 2, "%d. %s", i + 1, todos[i]);
    }
    pthread_mutex_unlock(&todo_lock);
    wrefresh(win_todo);
}

static void add_todo(const char *item) {
    pthread_mutex_lock(&todo_lock);
    if (todo_count < MAX_TODO) {
        todos[todo_count++] = strdup(item);
        FILE *fp = fopen(TODO_FILE, "a");
        if (fp) {
            fprintf(fp, "%s\n", item);
            fclose(fp);
        }
    }
    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*   QR 코드(전체화면) 로직      */
/*==============================*/
static void show_qrcode_fullscreen(const char* path) {
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    werase(stdscr);
    wrefresh(stdscr);

    int safe_rows = rows - 2;
    int safe_cols_mod = cols / 2;
    if (safe_rows < 1 || safe_cols_mod < 1) {
        clear();
        mvprintw(rows/2, (cols-18)/2, "Terminal too small!");
        mvprintw(rows/2+1, (cols-28)/2, "Press any key to return");
        refresh();
        getch();
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = pick_version_for_module(max_module);
    if (version < 1) version = 1;

    WINDOW *qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    mvwprintw(qrwin, 0, 0, "Press 'q' to return (QR v%d)", version);
    wrefresh(qrwin);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
             path, version);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
        mvwprintw(qrwin, rows-1, 0, "Press 'q' to return");
        wrefresh(qrwin);
        keypad(qrwin, TRUE);
        nodelay(qrwin, FALSE);
        int c;
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q');
        delwin(qrwin);
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    char linebuf[1024];
    int row = 1;
    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        int len = (int)strlen(linebuf);
        if (len > 0 && (linebuf[len-1]=='\n' || linebuf[len-1]=='\r'))
            linebuf[len-1] = '\0';
        if (row >= safe_rows) break;
        mvwprintw(qrwin, row++, 0, "%s", linebuf);
        wrefresh(qrwin);
    }
    pclose(fp);

    mvwprintw(qrwin, rows-1, 0, "Press 'q' to return");
    wrefresh(qrwin);

    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') break;
    }

    delwin(qrwin);
    clear();
    create_windows(1);
    signal(SIGWINCH, old_winch);
}

static void process_and_show_file(WINDOW* custom, const char* path) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    const char* ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".txt") != 0)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Only .c or .txt allowed: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    if (st.st_size > MAX_QR_BYTES) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File too large (%ld bytes).", (long)st.st_size);
        mvwprintw(custom, 2, 2, "Max allowed for QR: %d bytes", MAX_QR_BYTES);
        mvwprintw(custom, 4, 2, "Press 'q' to return");
        wrefresh(custom);
        int c; keypad(custom, TRUE); nodelay(custom, FALSE);
        while ((c = wgetch(custom)) != 'q' && c != 'Q');
        clear();
        create_windows(1);
        return;
    }

    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Generating QR for: %s", path);
    mvwprintw(custom, 2, 2, "Press any key to view QR...");
    wrefresh(custom);

    napms(300);
    wgetch(custom);

    show_qrcode_fullscreen(path);
}

/*==============================*/
/*   ncurses 초기화/정리/리사이즈 */
/*==============================*/
static void cleanup_ncurses(void) {
    if (win_time)   { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo)   { delwin(win_todo);   win_todo = NULL; }
    if (win_input)  { delwin(win_input);  win_input = NULL; }
    endwin();
}

static void handle_winch(int sig) {
    (void)sig;
    endwin();
    refresh();
    clear();
    create_windows(1);
}

static void get_time_strings(char* local_buf, int len1,
                             char* us_buf,    int len2,
                             char* uk_buf,    int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;
    mktime(&tm_uk);
    strftime(uk_buf, len3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tm_uk);
}

static void update_time(WINDOW* w) {
    char local_buf[32], us_buf[32], uk_buf[32];
    get_time_strings(local_buf,sizeof(local_buf),
                     us_buf,sizeof(us_buf),
                     uk_buf,sizeof(uk_buf));
    int h_time, w_time;
    getmaxyx(w, h_time, w_time);
    if (h_time<5 || w_time<20) return;
    werase(w);
    box(w,0,0);
    mvwprintw(w,0,2," Time ");
    mvwprintw(w,1,2,"Local: %s", local_buf);
    mvwprintw(w,2,2,"USA  : %s", us_buf);
    mvwprintw(w,3,2,"UK   : %s", uk_buf);
    wrefresh(w);
}

static int pick_version_for_module(int max_module) {
    for (int v=1; v<=40; v++) {
        int module_size = 17 + 4*v;
        if (module_size>max_module) return v-1;
    }
    return 40;
}

/*==============================*/
/*  ncurses 윈도우 생성 함수    */
/*==============================*/
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
                                const char* lines[], int n)
{
    int row = start_y;
    for (int i=0; i<n && row<start_y+max_lines; i++) {
        const char* text = lines[i];
        int len = text ? (int)strlen(text) : 0;
        int offset = 0;
        if (len == 0) {
            if (row < start_y+max_lines) {
                mvwaddch(win, row++, 2, ' ');
            }
            continue;
        }
        while (offset < len && row<start_y+max_lines) {
            int chunk = (len-offset > max_cols ? max_cols : len-offset);
            mvwprintw(win, row++, 2, "%.*s", chunk, text+offset);
            offset += chunk;
        }
    }
}

static void create_windows(int in_lobby) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (win_time)   { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo)   { delwin(win_todo);   win_todo = NULL; }
    if (win_input)  { delwin(win_input);  win_input = NULL; }

    int left_width   = cols / 2;
    int right_width  = cols - left_width;
    int left_height  = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height  = 5;
    int custom_y     = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    win_time   = newwin(time_height,   left_width,    title_height, 0);
    win_custom = newwin(custom_height, left_width,    custom_y,     0);
    win_todo   = newwin(rows-INPUT_HEIGHT, right_width, 0,          left_width);
    win_input  = newwin(INPUT_HEIGHT,   cols,          rows-INPUT_HEIGHT, 0);

    scrollok(win_time,   FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo,   TRUE);

    box(win_time,  0, 0);
    mvwprintw(win_time, 0, 2, " Time ");
    mvwprintw(win_time, 1, 2, "Local:    --:--:--");
    mvwprintw(win_time, 2, 2, "USA  :    --:--:--");
    mvwprintw(win_time, 3, 2, "UK   :    --:--:--");
    wrefresh(win_time);

    box(win_custom, 0, 0);
    if (in_lobby) {
        int maxy, maxx;
        getmaxyx(win_custom, maxy, maxx);
        int inner_lines = maxy - 2;
        int inner_cols  = maxx - 4;
        print_wrapped_lines(win_custom,1,inner_lines,inner_cols,
                            lobby_text, lobby_lines);
    }
    wrefresh(win_custom);

    box(win_todo, 0, 0);
    mvwprintw(win_todo,1,2,"=== ToDo List ===");
    wrefresh(win_todo);

    box(win_input, 0, 0);
    mvwprintw(win_input,1,2,"Command: ");
    wrefresh(win_input);
}

/*==============================*/
/*   Serveo 터널 (Chat 서버용)   */
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
        execlp("ssh","ssh",
               "-o","StrictHostKeyChecking=no",
               "-o","ServerAliveInterval=60",
               "-N","-R", forward_arg,
               "serveo.net",
               (char*)NULL);
        perror("execlp");
        _exit(1);
    }
    else {
        close(pipe_fd[1]);
        FILE *fp = fdopen(pipe_fd[0],"r");
        if (!fp) { close(pipe_fd[0]); return -1; }
        char line[512];
        int remote_port = -1;
        while (fgets(line,sizeof(line),fp)) {
            if (sscanf(line,"Allocated port %d",&remote_port)==1) {
                printf(">> Serveo 터널 할당 완료: serveo.net:%d\n", remote_port);
                fflush(stdout);
                break;
            }
        }
        fclose(fp);
        return remote_port;
    }
}

/*==============================*/
/*      QR 코드 CLI 출력         */
/*==============================*/
static void show_qr_cli(const char *filename) {
    char cmd[512];
    snprintf(cmd,sizeof(cmd),"qrencode -t ASCII -o - '%s'", filename);
    FILE *fp = popen(cmd,"r");
    if (!fp) {
        fprintf(stderr,"Failed to run qrencode\n");
        return;
    }
    char line[512];
    while (fgets(line,sizeof(line),fp)) {
        fputs(line, stdout);
    }
    pclose(fp);
}
