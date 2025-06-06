/*
 * coshell.c
 *  - CoShell: 터미널 기반 협업 툴 통합 구현
 *    1) ToDo 리스트 관리
 *    2) 실시간 채팅 서버/클라이언트
 *    3) 파일 전송용 QR 코드 생성 & 화면 출력 (전체화면 모드, 분리된 qr.c/qr.h 사용)
 *    4) ncurses UI: 분할 창, 버튼, 로비 → ToDo/Chat/QR 전환
 *
 * 빌드 예시:
 *   gcc coshell.c chat.c todo.c qr.c -o coshell -Wall -O2 -std=c11 -lncursesw -lpthread
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
#include "qr.h"    // 분리된 QR 헤더

#define MAX_CLIENTS   5
#define BUF_SIZE      1024
#define MAX_TODO      100
#define INPUT_HEIGHT  3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511

// Chat 포트 (로컬)
#define LOCAL_PORT    12345

/*==============================*/
/*   전역 변수 (공유 데이터)    */
/*==============================*/

// ncurses 윈도우 포인터
WINDOW *win_time    = NULL;  // 왼쪽 상단: 시간 표시
static WINDOW *win_custom  = NULL;  // 왼쪽 중간/하단: 로비·Chat·QR
static WINDOW *win_todo    = NULL;  // 오른쪽 전체: ToDo 목록
static WINDOW *win_input   = NULL;  // 맨 아래: 커맨드 입력창

volatile sig_atomic_t resized = 0;   // 터미널 리사이즈 감지 플래그
volatile int chat_running = 0;       // 채팅 모드 활성화 플래그

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
/*        함수 전방 선언        */
/*==============================*/

// 공통
static void cleanup_ncurses(void);
static void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
                                const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
                             char* us_buf, int len2,
                             char* uk_buf, int len3);
static void *timer_thread_fn(void *arg);
void update_time(WINDOW* w);

// 메인/UI/CLI 로직
static void show_main_menu(void);
static void cli_main(int argc, char *argv[]);
static void ui_main(void);

// Serveo 터널 (Chat 서버용)
static int setup_serveo_tunnel(int local_port);

/*==============================*/
/*          main 함수           */
/*==============================*/
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");  // 반드시 locale 설정 (wide char 지원)

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
/*        CLI 모드 함수         */
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
        // CLI 모드: ASCII QR 출력
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
    // SIGWINCH는 무시하고, KEY_RESIZE로만 처리
    signal(SIGWINCH, SIG_IGN);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);    // stdscr에도 KEY_RESIZE 이벤트를 전달
    curs_set(1);

    // 첫 화면: 로비
    create_windows(1);
    // ToDo 목록 표시 (항상 나타나도록 수정)
    load_todo();
    draw_todo(win_todo);

    char cmdbuf[MAX_CMD_LEN + 1] = {0};
    int cmdlen = 0;
    char pathbuf[MAX_PATH_LEN + 1] = {0};
    int pathlen = 0;
    time_t last_time = 0;
    int mode = 0;  // 0 = 로비, 1 = ToDo, 2 = Chat, 3 = QR 입력, 4 = QR 전체화면

    int input_y = 1, input_x = 11;  // “Command:” 뒤로 커서 위치 조정

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
            // ncurses 내부 윈도우를 모두 삭제하고 새로운 크기를 감지
            endwin();
            refresh();
            clear();

            // mode == 0(로비)이면 로비 메시지 포함, 아니면 빈 화면
            create_windows(mode == 0);
            if (mode == 0) {
                // 리사이즈 후 메인 UI에서도 ToDo 목록 다시 표시
                load_todo();
                draw_todo(win_todo);
            }

            if (mode == 1) {
                // ToDo 모드 재진입: interactive 모드로 들어감
                todo_enter(win_input, win_todo, win_custom);
                mode = 0;
                create_windows(1);
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == 2) {
                // Chat 모드 재진입: 안내 메시지 다시 그려줌
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                wrefresh(win_custom);

                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == 3) {
                // QR 모드(경로 입력) 재진입: pathbuf 내용까지 다시 그려줌
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Enter path for QR code (or 'q' to cancel):");
                mvwprintw(win_custom, 2, 2, "%s", pathbuf);
                wrefresh(win_custom);

                werase(win_input);
                box(win_input, 0, 0);
                mvwprintw(win_input, 1, 2, "%s", pathbuf);
                wmove(win_input, 1, 2 + pathlen);
                wrefresh(win_input);

                load_todo();
                draw_todo(win_todo);
            }
            continue;
        }

        // ─────────────────────────────────────────────────
        // (A) QR 경로 입력 모드 (mode == 3)
        // ─────────────────────────────────────────────────
        if (mode == 3) {
            // 도움말 표시
            werase(win_custom);
            box(win_custom, 0, 0);
            mvwprintw(win_custom, 1, 2, "Enter path for QR code (or 'q' to cancel):");
            mvwprintw(win_custom, 2, 2, "%s", pathbuf);
            wrefresh(win_custom);

            // win_input(입력창)에 pathbuf 표시 + 커서 이동
            werase(win_input);
            box(win_input, 0, 0);
            mvwprintw(win_input, 1, 2, "%s", pathbuf);
            wmove(win_input, 1, 2 + pathlen);
            wrefresh(win_input);

            // 비차단 입력(200ms) → 경로를 하나씩 받아서 pathbuf에 저장
            wtimeout(win_input, 200);
            int ch2 = wgetch(win_input);
            if (ch2 != ERR) {
                // 'q' 로 취소하면 바로 메인으로
                if (ch2 == 'q' || ch2 == 'Q') {
                    mode = 0;
                    create_windows(1);
                    load_todo();
                    draw_todo(win_todo);
                    continue;
                }
                if (ch2 == KEY_BACKSPACE || ch2 == 127) {
                    if (pathlen > 0) {
                        pathlen--;
                        pathbuf[pathlen] = '\0';
                    }
                }
                else if (ch2 == '\n' || ch2 == KEY_ENTER) {
                    // Enter 치면 mode = 4 → 전체화면 QR 생성
                    mode = 4;
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));

                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                else if (ch2 >= 32 && ch2 <= 126) {
                    if (pathlen < MAX_PATH_LEN) {
                        pathbuf[pathlen++] = (char)ch2;
                    }
                }
            }
            continue;
        }

        // ─────────────────────────────────────────────────
        // (B) QR 전체화면 모드 (mode == 4)
        // ─────────────────────────────────────────────────
        if (mode == 4) {
            // 분리된 qr 모듈로 이동
            process_and_show_file(win_custom, pathbuf);

            // 전체화면 QR이 닫히면, 다시 로비 UI를 복귀시켜야 한다.
            mode = 0;
            create_windows(1);
            load_todo();
            draw_todo(win_todo);
            continue;
        }

        // ─────────────────────────────────────────────────
        // (C) 나머지 모드: 로비(0) / ToDo(1) / Chat(2) / 커맨드 입력
        // ─────────────────────────────────────────────────

        // (C-1) 입력창(Command) 그리기
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "Command: %.*s", cmdlen, cmdbuf);
        wmove(win_input, input_y, input_x + cmdlen);
        wrefresh(win_input);

        // (C-2) 비차단으로 키 입력 받기 (200ms 대기)
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
                    mode = 1;
                    todo_enter(win_input, win_todo, win_custom);
                    mode = 0;
                    create_windows(1);
                    load_todo();
                    draw_todo(win_todo);
                }
                // 2 → Chat 모드 진입
                else if (cmdlen > 0 && cmdbuf[0] == '2') {
                    mode = 2;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter Chat host and port:");
                    wrefresh(win_custom);

                    // (A) 호스트 입력
                    char host[128] = {0};
                    while (1) {
                        werase(win_input);
                        box(win_input, 0, 0);
                        mvwprintw(win_input, 1, 2, "Host: ");
                        wmove(win_input, 1, 8);
                        wrefresh(win_input);

                        wtimeout(win_input, -1);
                        echo();
                        flushinp();
                        wgetnstr(win_input, host, sizeof(host) - 1);
                        noecho();

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
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter Port Number (or 'q' to cancel):");
                    wrefresh(win_custom);

                    while (1) {
                        werase(win_input);
                        box(win_input, 0, 0);
                        mvwprintw(win_input, 1, 2, "Port: ");
                        wmove(win_input, 1, 8);
                        wrefresh(win_input);

                        wtimeout(win_input, -1);
                        echo();
                        flushinp();
                        wgetnstr(win_input, port_str, sizeof(port_str) - 1);
                        noecho();

                        if (strcmp(port_str, "q") == 0 || strcmp(port_str, "Q") == 0 ||
                            strcmp(port_str, "quit") == 0) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Cancelled Chat. Returning to main UI...");
                            wrefresh(win_custom);
                            napms(1000);
                            mode = 0;
                            create_windows(1);
                            load_todo();
                            draw_todo(win_todo);
                            break;
                        }

                        if (strlen(port_str) == 0) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Port cannot be empty. Try again.");
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        port = atoi(port_str);
                        if (port <= 0 || port > 65535) {
                            werase(win_custom);
                            box(win_custom, 0, 0);
                            mvwprintw(win_custom, 1, 2, "Invalid port: %s. Try again (or 'q').", port_str);
                            wrefresh(win_custom);
                            napms(1000);
                            continue;
                        }
                        break;
                    }

                    if (mode == 0) {
                        // 사용자가 포트 입력 중 'q'로 취소했을 때
                        memset(cmdbuf, 0, sizeof(cmdbuf));
                        cmdlen = 0;
                        continue;
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

                    // (E) 채팅 모드 진입 전 안내 메시지
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                    wrefresh(win_custom);

                    // (F) 채팅 모드 진입
                    pthread_t timer_thread_id;
                    chat_running = 1;
                    if (pthread_create(&timer_thread_id, NULL, timer_thread_fn, NULL) != 0) {
                        // 실패해도 무시
                    }
                    chat_client(host, port, nickname, win_custom, win_input);
                    chat_running = 0;
                    pthread_join(timer_thread_id, NULL);

                    // (G) 채팅 모드 종료 후 → 메인 UI로 복귀
                    werase(win_custom);
                    create_windows(1);
                    mode = 0;
                    load_todo();
                    draw_todo(win_todo);
                }
                // 3 → QR 경로 입력 모드 진입
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
                    mode = 3;
                    pathlen = 0;
                    memset(pathbuf, 0, sizeof(pathbuf));
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter path for QR code (or 'q' to cancel):");
                    wrefresh(win_custom);

                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                // a <item> → ToDo 항목 추가 (비대화형 모드)
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char *item = cmdbuf + 2;
                    add_todo(item);
                    draw_todo(win_todo);
                }
                // f <filepath> → QR 전체화면 모드 바로 실행
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char *filepath = cmdbuf + 2;
                    // 직접 QR 전체화면 실행
                    process_and_show_file(win_custom, filepath);
                    // 종료 후 UI 복귀
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

                    // mode == 0 (메인 UI) 시 가능한 명령 안내
                    mvwprintw(win_custom, 3, 2, "Available commands in main UI:");
                    mvwprintw(win_custom, 4, 4, "1             : Enter To-Do mode");
                    mvwprintw(win_custom, 5, 4, "2             : Enter Chat mode");
                    mvwprintw(win_custom, 6, 4, "3             : Enter QR mode");
                    mvwprintw(win_custom, 7, 4, "a <item>      : Add To-Do (non-interactive)");
                    mvwprintw(win_custom, 8, 4, "f <filepath>  : Show QR for <filepath>");
                    mvwprintw(win_custom, 9, 4, "exit          : Exit program");
                    wrefresh(win_custom);

                    napms(3000);  // 3초간 표시한 뒤 자동으로 로비로 돌아감

                    mode = 0;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    int maxy, maxx;
                    getmaxyx(win_custom, maxy, maxx);
                    int inner_lines = maxy - 2;
                    int inner_cols  = maxx - 4;
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

/*==============================*/
/*   Time 문자열 생성 함수     */
/*==============================*/
static void get_time_strings(char* local_buf, int len1,
                             char* us_buf,    int len2,
                             char* uk_buf,    int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;    // KST → USA ET
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;     // KST → UK GMT
    mktime(&tm_uk);
    strftime(uk_buf, len3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tm_uk);
}

void update_time(WINDOW* w) {
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

// 매초 윈도우의 시간을 업데이트해 주는 스레드 함수 (Chat 모드에서만 사용)
static void *timer_thread_fn(void *arg) {
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

// 윈도우들을 새로 생성/배치
static void create_windows(int in_lobby) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (1) 기존 윈도우 삭제
    if (win_time)   { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo)   { delwin(win_todo);   win_todo = NULL; }
    if (win_input)  { delwin(win_input);  win_input = NULL; }

    // (2) 크기 계산
    int left_width   = cols / 2;
    int right_width  = cols - left_width;
    int left_height  = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height  = 5;    // border(2) + 라벨(1) + 시간 3줄 = 5
    int custom_y     = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) 상단 타이틀
    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    // (4) 왼쪽 상단: Time 표시 창
    win_time   = newwin(time_height,   left_width,    title_height, 0);
    // (5) 왼쪽 중간/하단: 로비 또는 모드 콘텐츠
    win_custom = newwin(custom_height, left_width,    custom_y,     0);
    // (6) 오른쪽 전체: ToDoList 창
    win_todo   = newwin(rows-INPUT_HEIGHT, right_width, 0,          left_width);
    // (7) 맨 아래: Command 입력창
    win_input  = newwin(INPUT_HEIGHT,   cols,          rows-INPUT_HEIGHT, 0);

    // (8) 스크롤 설정
    scrollok(win_time,   FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo,   TRUE);

    // (9) Time 창 초기화
    box(win_time,  0, 0);
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
        int inner_cols  = maxx - 4;
        print_wrapped_lines(win_custom,1,inner_lines,inner_cols,
                            lobby_text, lobby_lines);
    }
    wrefresh(win_custom);

    // (11) ToDoList 창 초기화
    box(win_todo, 0, 0);
    mvwprintw(win_todo,1,2,"=== ToDo List ===");
    wrefresh(win_todo);

    // (12) 입력창 초기화
    box(win_input, 0, 0);
    mvwprintw(win_input,1,2,"Command: ");
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
