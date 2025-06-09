/*
 * coshell.c
 *  - CoShell: ÅÍ¹Ì³Î ±â¹Ý Çù¾÷ Åø ÅëÇÕ ±¸Çö
 *    1) ToDo ¸®½ºÆ® °ü¸®
 *    2) ½Ç½Ã°£ Ã¤ÆÃ ¼­¹ö/Å¬¶óÀÌ¾ðÆ®
 *    3) ÆÄÀÏ Àü¼Û¿ë QR ÄÚµå »ý¼º & È­¸é Ãâ·Â (ÀüÃ¼È­¸é ¸ðµå, ºÐ¸®µÈ qr.c/qr.h »ç¿ë)
 *    4) ncurses UI: ºÐÇÒ Ã¢, ¹öÆ°, ·Îºñ ¡æ ToDo/Chat/QR ÀüÈ¯
 *
 * ºôµå ¿¹½Ã:
 *   gcc coshell.c chat.c todo_ui.c todo_core.c todo_client.c qr.c -o coshell -Wall -O2 -std=c11 -lncursesw -lpthread
 *
 * »ç¿ë ÆÐÅ°Áö (Ubuntu/Debian):
 *   sudo apt update
 *   sudo apt install -y libncursesw5-dev qrencode
 *
 * ½ÇÇà ¹æ½Ä:
 *   ./coshell                  # ¸Þ´º/CLI/UI ¸ðµå ¼±ÅÃ
 *   ./coshell server           # Chat ¼­¹ö (Serveo ÅÍ³Î¸µ Æ÷ÇÔ)
 *   ./coshell add <item>       # CLI ¸ðµå: ToDo Ãß°¡
 *   ./coshell list             # CLI ¸ðµå: ToDo ¸ñ·Ï Ãâ·Â
 *   ./coshell del <index>      # CLI ¸ðµå: ToDo »èÁ¦
 *   ./coshell qr <filepath>    # CLI ¸ðµå: ASCII QR Ãâ·Â
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

#define MAX_CLIENTS   5
#define BUF_SIZE      1024
#define INPUT_HEIGHT  3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511

 // Chat Æ÷Æ® (·ÎÄÃ)
#define LOCAL_PORT    12345

// Mode constants
#define MODE_LOBBY      0
#define MODE_TODO       1
#define MODE_CHAT       2
#define MODE_QR_INPUT   3
#define MODE_QR_FULL    4

WINDOW* win_time = NULL;  // ¿ÞÂÊ »ó´Ü: ½Ã°£ Ç¥½Ã
WINDOW* win_custom = NULL;  // ¿ÞÂÊ Áß°£/ÇÏ´Ü: ·Îºñ¡¤Chat¡¤QR
WINDOW* win_todo = NULL;  // ¿À¸¥ÂÊ ÀüÃ¼: ToDo ¸ñ·Ï
WINDOW* win_input = NULL;  // ¸Ç ¾Æ·¡: Ä¿¸Çµå ÀÔ·ÂÃ¢

volatile sig_atomic_t resized = 0;   // ÅÍ¹Ì³Î ¸®»çÀÌÁî °¨Áö ÇÃ·¡±×
volatile int chat_running = 0;       // Ã¤ÆÃ ¸ðµå È°¼ºÈ­ ÇÃ·¡±×

// ·Îºñ ÅØ½ºÆ®
static const char* lobby_text[] = {
    "Welcome!",
    "CoShell: terminal-based collaboration toolbox.",
    "1. To-Do List Management",
    "2. Chat",
    "3. QR Code",
    "",
    "Type 'exit' to quit at any time."
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
/*        ÇÔ¼ö Àü¹æ ¼±¾ð        */
/*==============================*/

// °øÅë
static void cleanup_ncurses(void);
void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
    const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3);
static void* timer_thread_fn(void* arg);
void update_time(WINDOW* w);

// ¸ðµå Ã³¸®
static void handle_todo_mode(TodoState* state, int* mode);
static void handle_chat_mode(ChatState* state, int* mode);
static void handle_qr_input_mode(QRInputState* qr_state, int* mode);
static void handle_qr_full_mode(QRInputState* qr_state, int* mode);

// ¸ÞÀÎ/UI/CLI ·ÎÁ÷
static void show_main_menu(void);
static void cli_main(int argc, char* argv[]);
static void ui_main(void);

// Serveo ÅÍ³Î (Chat ¼­¹ö¿ë)
static int setup_serveo_tunnel(int local_port);

/*==============================*/
/*          main ÇÔ¼ö           */
/*==============================*/
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");  // ¹Ýµå½Ã locale ¼³Á¤ (wide char Áö¿ø)

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
        ui_main();
    }
    else if (strcmp(argv[1], "server") == 0) {
        printf(">> Serveo.net: Chat ¼­¹ö ¿ø°Ý Æ÷Æ® ¿äÃ» Áß...\n");
        int remote_port = setup_serveo_tunnel(LOCAL_PORT);
        if (remote_port < 0) {
            fprintf(stderr, "Serveo ÅÍ³Î ½ÇÆÐ. ·ÎÄÃ Chat ¼­¹ö¸¸ ½ÇÇà.\n");
        }
        else {
            printf(">> Serveo Chat ÁÖ¼Ò: serveo.net:%d ¡æ ³»ºÎ %d Æ÷Æ®\n", remote_port, LOCAL_PORT);
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
/*      ¸ÞÀÎ ¸Þ´º Ãâ·Â ÇÔ¼ö     */
/*==============================*/
static void show_main_menu(void) {
    int choice = 0;
    while (1) {
        printf("\033[H\033[J");
        printf("\n===== CoShell Main Menu =====\n");
        printf("1. Run Chat Server (Serveo ÅÍ³Î¸µ)\n");
        printf("2. Run Client (ToDo + Chat UI)\n");
        printf("3. Exit\n");
        printf("Select (1-3): ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        getchar();  // °³Çà Á¦°Å

        if (choice == 1) {
            printf("\033[H\033[J");
            printf("Chat server (Serveo ÅÍ³Î¸µ) ½ÇÇà Áß...\n");
            int remote_port = setup_serveo_tunnel(LOCAL_PORT);
            if (remote_port < 0) {
                fprintf(stderr, "Serveo ÅÍ³Î¸µ ½ÇÆÐ. ·ÎÄÃ Chat ¼­¹ö ½ÇÇà.\n");
            }
            else {
                printf(">> Serveo Chat ÁÖ¼Ò: serveo.net:%d\n", remote_port);
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
/*        CLI ¸ðµå ÇÔ¼ö         */
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
/*         UI ¸ðµå ÇÔ¼ö         */
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
    keypad(stdscr, TRUE);    // stdscr¿¡µµ KEY_RESIZE ÀÌº¥Æ®¸¦ Àü´Þ
    curs_set(1);

    // Ã¹ È­¸é: ·Îºñ
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

    char cmdbuf[MAX_CMD_LEN + 1] = { 0 };
    int cmdlen = 0;
    time_t last_time = 0;
    int mode = MODE_LOBBY;  // 0 = ·Îºñ, 1 = ToDo, 2 = Chat, 3 = QR ÀÔ·Â, 4 = QR ÀüÃ¼È­¸é

    while (1) {
        // (1) ¸ÅÃÊ ½Ã°£ ¾÷µ¥ÀÌÆ®
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // (2) KEY_RESIZE Ã¼Å©
        wtimeout(win_input, 0);
        int tch = wgetch(win_input);
        if (tch == KEY_RESIZE) {
            resized = 1;
        }
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (3) ¸®»çÀÌÁî ÇÃ·¡±×°¡ ¼¼¿öÁö¸é Áï½Ã È­¸éÀ» Àç±¸¼º
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        if (resized) {
            resized = 0;
            endwin();
            refresh();
            clear();

            // ncurses ³»ºÎ À©µµ¿ì¸¦ ¸ðµÎ »èÁ¦ÇÏ°í »õ·Î¿î Å©±â¸¦ °¨Áö
            create_windows(mode == MODE_LOBBY);
            if (mode == MODE_LOBBY) {
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_TODO) {
                // ToDo ¸ðµå: redraw ToDo list
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_CHAT) {
                // Chat ¸ðµå: ¾È³» ¸Þ½ÃÁö ´Ù½Ã ±×·ÁÁÜ
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                wrefresh(win_custom);

                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_QR_INPUT) {
                // QR ¸ðµå(°æ·Î ÀÔ·Â) ÀçÁøÀÔ: pathbuf ³»¿ë±îÁö ´Ù½Ã ±×·ÁÁÜ
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

        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (A) QR °æ·Î ÀÔ·Â ¸ðµå (mode == 3)
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        if (mode == MODE_QR_INPUT) {
            handle_qr_input_mode(&qr_state, &mode);
            continue;
        }

        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (B) QR ÀüÃ¼È­¸é ¸ðµå (mode == 4)
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        if (mode == MODE_QR_FULL) {
            handle_qr_full_mode(&qr_state, &mode);
            continue;
        }

        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (C) ToDo ¸ðµå
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        if (mode == MODE_TODO) {
            handle_todo_mode(&todo_state, &mode);
            continue;
        }

        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (D) Chat ¸ðµå (È£½ºÆ®/Æ÷Æ®/´Ð³×ÀÓ ÀÔ·Â ¹× ½ÇÇà)
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        if (mode == MODE_CHAT) {
            handle_chat_mode(&chat_state, &mode);
            continue;
        }

        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡
        // (E) ³ª¸ÓÁö ¸ðµå: ·Îºñ (mode == 0)
        // ¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡¦¡

        // (E-1) ÀÔ·ÂÃ¢(Command) ±×¸®±â
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "Command: %.*s", cmdlen, cmdbuf);
        wmove(win_input, 1, 11 + cmdlen);
        wrefresh(win_input);

        // (E-2) ºñÂ÷´ÜÀ¸·Î Å° ÀÔ·Â ¹Þ±â (200ms ´ë±â)
        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) {
            resized = 1;
            continue;
        }

        if (ch != ERR) {
            // ¹é½ºÆäÀÌ½º
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                }
            }
            // Enter ÀÔ·Â
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';

                // exit ¡æ ÇÁ·Î±×·¥ Á¾·á
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;
                }
                // 1 ¡æ To-Do ¸ðµå ÁøÀÔ
                else if (cmdlen > 0 && cmdbuf[0] == '1') {
                    mode = MODE_TODO;
                    todo_state.len = 0;
                    memset(todo_state.buf, 0, sizeof(todo_state.buf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // 2 ¡æ Chat ¸ðµå ÁøÀÔ
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
                // 3 ¡æ QR °æ·Î ÀÔ·Â ¸ðµå ÁøÀÔ
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
                    mode = MODE_QR_INPUT;
                    qr_state.pathlen = 0;
                    memset(qr_state.pathbuf, 0, sizeof(qr_state.pathbuf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // a <item> ¡æ ToDo Ç×¸ñ Ãß°¡ (ºñ´ëÈ­Çü ¸ðµå)
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char* item = cmdbuf + 2;
                    add_todo(item);
                    draw_todo(win_todo);
                }
                // f <filepath> ¡æ QR ÀüÃ¼È­¸é ¸ðµå ¹Ù·Î ½ÇÇà
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char* filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                    create_windows(1);
                    load_todo();
                    draw_todo(win_todo);
                }
                else {
                    /*==============================*/
                    /*    Unknown command Ã³¸®      */
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
                    napms(3000);  // 3ÃÊ°£ Ç¥½ÃÇÑ µÚ ÀÚµ¿À¸·Î ·Îºñ·Î µ¹¾Æ°¨

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

                // ÀÔ·Â ¹öÆÛ ÃÊ±âÈ­
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

    endwin();  // ncurses Á¾·á
}

/* Handle ToDo mode input */
static void handle_todo_mode(TodoState* state, int* mode) {
    // (1) ¸Å ÇÁ·¹ÀÓ: µµ¿ò¸»°ú ToDo ¸®½ºÆ® ´Ù½Ã ±×¸®±â
    werase(win_custom);
    box(win_custom, 0, 0);
    draw_custom_help(win_custom);      // todo.c¿¡ Á¤ÀÇµÈ µµ¿ò¸» ÇÔ¼ö
    werase(win_todo);
    box(win_todo, 0, 0);
    load_todo();                       // ÆÄÀÏ ¡æ ¸Þ¸ð¸® ·Îµå
    draw_todo(win_todo);               // ¸Þ¸ð¸® ¡æ È­¸é Ãâ·Â
    wrefresh(win_custom);
    wrefresh(win_todo);

    // (2) ÀÔ·ÂÃ¢ ±×¸®±â
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", state->buf);
    wmove(win_input, 1, 2 + state->len);
    wrefresh(win_input);

    // (3) ÀÔ·Â Ã³¸®
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
            // ·Îºñ·Î µ¹¾Æ°¡±â
            *mode = MODE_LOBBY;
            create_windows(1);
            load_todo();
            draw_todo(win_todo);
            return;  // ¿©±â¼­ Áï½Ã ¸®ÅÏÇÏ¿© ¸ÞÀÎ UI ÃÊ±âÈ­ È­¸é À¯Áö
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

        // (4) º¯°æ ÈÄ ´Ù½Ã ±×¸®±â
        werase(win_custom);
        box(win_custom, 0, 0);
        draw_custom_help(win_custom);
        load_todo();
        draw_todo(win_todo);
        werase(win_input);
        box(win_input, 0, 0);
        wrefresh(win_input);

        // ÀÔ·Â ¹öÆÛ ÃÊ±âÈ­
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
            // ½ÇÆÐÇØµµ ¹«½Ã
        }
        chat_client(state->host, state->port, state->nickname, win_custom, win_input);
        chat_running = 0;
        pthread_join(timer_thread_id, NULL);

        // Ã¤ÆÃ ¸ðµå Á¾·á ÈÄ ¡æ ¸ÞÀÎ UI·Î º¹±Í
        state->step = 0;
        memset(state->host, 0, sizeof(state->host));
        memset(state->port_str, 0, sizeof(state->port_str));
        memset(state->nickname, 0, sizeof(state->nickname));
        // argv[0]ºÎÅÍ ÇÁ·Î±×·¥ ÀüÃ¼¸¦ execvp·Î µ¤¾î¾´´Ù
        char* argv_new[] = { "./coshell", "ui", NULL };
        execvp(argv_new[0], argv_new);

    }
}

/* Handle QR path input mode */
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

/* Handle QR full-screen mode */
static void handle_qr_full_mode(QRInputState* qr_state, int* mode) {
    // 1) QR ÀüÃ¼È­¸é Ãâ·Â
    process_and_show_file(win_custom, qr_state->pathbuf);

    // 2) »ç¿ëÀÚ°¡ ¡®q¡¯¸¦ ´©¸£¸é ÇÁ·Î±×·¥À» ´Ù½Ã ½ÇÇà
    endwin();   // ncurses ¼¼¼Ç Á¾·á

    // argv[0]ºÎÅÍ ÇÁ·Î±×·¥ ÀüÃ¼¸¦ execvp·Î µ¤¾î¾´´Ù
    char* argv_new[] = { "./coshell", "ui", NULL };
    execvp(argv_new[0], argv_new);

    // execvp°¡ ½ÇÆÐÇÏ¸é ¿©±â¿¡ ¿È
    perror("execvp failed");
    exit(1);
}

/*==============================*/
/*   ncurses ÃÊ±âÈ­/Á¤¸®/¸®»çÀÌÁî */
/*==============================*/
static void cleanup_ncurses(void) {
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }
    endwin();
}

/*==============================*/
/*   Time ¹®ÀÚ¿­ »ý¼º ÇÔ¼ö     */
/*==============================*/
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;    // KST ¡æ USA ET
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;     // KST ¡æ UK GMT
    mktime(&tm_uk);
    strftime(uk_buf, len3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tm_uk);
}

void update_time(WINDOW* w) {
    char local_buf[32], us_buf[32], uk_buf[32];
    get_time_strings(local_buf, sizeof(local_buf),
        us_buf, sizeof(us_buf),
        uk_buf, sizeof(uk_buf));
    int h_time, w_time;
    getmaxyx(w, h_time, w_time);
    if (h_time < 5 || w_time < 20) return;
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Time ");
    mvwprintw(w, 1, 2, "Local: %s", local_buf);
    mvwprintw(w, 2, 2, "USA  : %s", us_buf);
    mvwprintw(w, 3, 2, "UK   : %s", uk_buf);
    wrefresh(w);
}

// ¸ÅÃÊ À©µµ¿ìÀÇ ½Ã°£À» ¾÷µ¥ÀÌÆ®ÇØ ÁÖ´Â ½º·¹µå ÇÔ¼ö (Chat ¸ðµå¿¡¼­¸¸ »ç¿ë)
static void* timer_thread_fn(void* arg) {
    (void)arg;
    while (chat_running) {
        update_time(win_time);
        sleep(1);
    }
    return NULL;
}

// ÁÖ¾îÁø ¹®ÀÚ¿­ ¹è¿­(lines)¸¦ max_cols Æø¿¡ ¸ÂÃç win¿¡ Ãâ·Â
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

// À©µµ¿ìµéÀ» »õ·Î »ý¼º/¹èÄ¡
void create_windows(int in_lobby) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (1) ±âÁ¸ À©µµ¿ì »èÁ¦
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }

    // (2) Å©±â °è»ê
    int left_width = cols / 2;
    int right_width = cols - left_width;
    int left_height = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height = 5;    // border(2) + ¶óº§(1) + ½Ã°£ 3ÁÙ = 5
    int custom_y = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) »ó´Ü Å¸ÀÌÆ²
    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    // (4) ¿ÞÂÊ »ó´Ü: Time Ç¥½Ã Ã¢
    win_time = newwin(time_height, left_width, title_height, 0);
    // (5) ¿ÞÂÊ Áß°£/ÇÏ´Ü: ·Îºñ ¶Ç´Â ¸ðµå ÄÜÅÙÃ÷
    win_custom = newwin(custom_height, left_width, custom_y, 0);
    // (6) ¿À¸¥ÂÊ ÀüÃ¼: ToDoList Ã¢
    win_todo = newwin(rows - INPUT_HEIGHT, right_width, 0, left_width);
    // (7) ¸Ç ¾Æ·¡: Command ÀÔ·ÂÃ¢
    win_input = newwin(INPUT_HEIGHT, cols, rows - INPUT_HEIGHT, 0);

    // (8) ½ºÅ©·Ñ ¼³Á¤
    scrollok(win_time, FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo, TRUE);

    // (9) Time Ã¢ ÃÊ±âÈ­
    box(win_time, 0, 0);
    mvwprintw(win_time, 0, 2, " Time ");
    mvwprintw(win_time, 1, 2, "Local:    --:--:--");
    mvwprintw(win_time, 2, 2, "USA  :    --:--:--");
    mvwprintw(win_time, 3, 2, "UK   :    --:--:--");
    wrefresh(win_time);

    // (10) Custom Ã¢(·Îºñ ¶Ç´Â ¸ðµå)
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

    // (11) ToDoList Ã¢ ÃÊ±âÈ­
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 1, 2, "=== ToDo List ===");
    wrefresh(win_todo);

    // (12) ÀÔ·ÂÃ¢ ÃÊ±âÈ­
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "Command: ");
    wrefresh(win_input);
}

/*==============================*/
/*   Serveo ÅÍ³Î (Chat ¼­¹ö¿ë)  */
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
                printf(">> Serveo ÅÍ³Î ÇÒ´ç ¿Ï·á: serveo.net:%d\n", remote_port);
                fflush(stdout);
                break;
            }
        }
        fclose(fp);
        return remote_port;
    }
}