/*
 * coshell.c
 *  - CoShell: �͹̳� ��� ���� �� ���� ����
 *    1) ToDo ����Ʈ ����
 *    2) �ǽð� ä�� ����/Ŭ���̾�Ʈ
 *    3) ���� ���ۿ� QR �ڵ� ���� & ȭ�� ��� (��üȭ�� ���, �и��� qr.c/qr.h ���)
 *    4) ncurses UI: ���� â, ��ư, �κ� �� ToDo/Chat/QR ��ȯ
 *
 * ���� ����:
 *   gcc coshell.c chat.c todo_ui.c todo_core.c todo_client.c qr.c -o coshell -Wall -O2 -std=c11 -lncursesw -lpthread
 *
 * ��� ��Ű�� (Ubuntu/Debian):
 *   sudo apt update
 *   sudo apt install -y libncursesw5-dev qrencode
 *
 * ���� ���:
 *   ./coshell                  # �޴�/CLI/UI ��� ����
 *   ./coshell server           # Chat ���� (Serveo �ͳθ� ����)
 *   ./coshell add <item>       # CLI ���: ToDo �߰�
 *   ./coshell list             # CLI ���: ToDo ��� ���
 *   ./coshell del <index>      # CLI ���: ToDo ����
 *   ./coshell qr <filepath>    # CLI ���: ASCII QR ���
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

 // Chat ��Ʈ (����)
#define LOCAL_PORT    12345

// Mode constants
#define MODE_LOBBY      0
#define MODE_TODO       1
#define MODE_CHAT       2
#define MODE_QR_INPUT   3
#define MODE_QR_FULL    4

WINDOW* win_time = NULL;  // ���� ���: �ð� ǥ��
WINDOW* win_custom = NULL;  // ���� �߰�/�ϴ�: �κ�Chat��QR
WINDOW* win_todo = NULL;  // ������ ��ü: ToDo ���
WINDOW* win_input = NULL;  // �� �Ʒ�: Ŀ�ǵ� �Է�â

volatile sig_atomic_t resized = 0;   // �͹̳� �������� ���� �÷���
volatile int chat_running = 0;       // ä�� ��� Ȱ��ȭ �÷���

// �κ� �ؽ�Ʈ
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
/*        �Լ� ���� ����        */
/*==============================*/

// ����
static void cleanup_ncurses(void);
void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
    const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3);
static void* timer_thread_fn(void* arg);
void update_time(WINDOW* w);

// ��� ó��
static void handle_todo_mode(TodoState* state, int* mode);
static void handle_chat_mode(ChatState* state, int* mode);
static void handle_qr_input_mode(QRInputState* qr_state, int* mode);
static void handle_qr_full_mode(QRInputState* qr_state, int* mode);

// ����/UI/CLI ����
static void show_main_menu(void);
static void cli_main(int argc, char* argv[]);
static void ui_main(void);

// Serveo �ͳ� (Chat ������)
static int setup_serveo_tunnel(int local_port);

/*==============================*/
/*          main �Լ�           */
/*==============================*/
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");  // �ݵ�� locale ���� (wide char ����)

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
        printf(">> Serveo.net: Chat ���� ���� ��Ʈ ��û ��...\n");
        int remote_port = setup_serveo_tunnel(LOCAL_PORT);
        if (remote_port < 0) {
            fprintf(stderr, "Serveo �ͳ� ����. ���� Chat ������ ����.\n");
        }
        else {
            printf(">> Serveo Chat �ּ�: serveo.net:%d �� ���� %d ��Ʈ\n", remote_port, LOCAL_PORT);
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
/*      ���� �޴� ��� �Լ�     */
/*==============================*/
static void show_main_menu(void) {
    int choice = 0;
    while (1) {
        printf("\033[H\033[J");
        printf("\n===== CoShell Main Menu =====\n");
        printf("1. Run Chat Server (Serveo �ͳθ�)\n");
        printf("2. Run Client (ToDo + Chat UI)\n");
        printf("3. Exit\n");
        printf("Select (1-3): ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        getchar();  // ���� ����

        if (choice == 1) {
            printf("\033[H\033[J");
            printf("Chat server (Serveo �ͳθ�) ���� ��...\n");
            int remote_port = setup_serveo_tunnel(LOCAL_PORT);
            if (remote_port < 0) {
                fprintf(stderr, "Serveo �ͳθ� ����. ���� Chat ���� ����.\n");
            }
            else {
                printf(">> Serveo Chat �ּ�: serveo.net:%d\n", remote_port);
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
/*        CLI ��� �Լ�         */
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
/*         UI ��� �Լ�         */
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
    keypad(stdscr, TRUE);    // stdscr���� KEY_RESIZE �̺�Ʈ�� ����
    curs_set(1);

    // ù ȭ��: �κ�
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
    int mode = MODE_LOBBY;  // 0 = �κ�, 1 = ToDo, 2 = Chat, 3 = QR �Է�, 4 = QR ��üȭ��

    while (1) {
        // (1) ���� �ð� ������Ʈ
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // (2) KEY_RESIZE üũ
        wtimeout(win_input, 0);
        int tch = wgetch(win_input);
        if (tch == KEY_RESIZE) {
            resized = 1;
        }
        // ��������������������������������������������������������������������������������������������������
        // (3) �������� �÷��װ� �������� ��� ȭ���� �籸��
        // ��������������������������������������������������������������������������������������������������
        if (resized) {
            resized = 0;
            endwin();
            refresh();
            clear();

            // ncurses ���� �����츦 ��� �����ϰ� ���ο� ũ�⸦ ����
            create_windows(mode == MODE_LOBBY);
            if (mode == MODE_LOBBY) {
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_TODO) {
                // ToDo ���: redraw ToDo list
                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_CHAT) {
                // Chat ���: �ȳ� �޽��� �ٽ� �׷���
                werase(win_custom);
                box(win_custom, 0, 0);
                mvwprintw(win_custom, 1, 2, "Type '/quit' to end chat and return to main UI.");
                wrefresh(win_custom);

                load_todo();
                draw_todo(win_todo);
            }
            else if (mode == MODE_QR_INPUT) {
                // QR ���(��� �Է�) ������: pathbuf ������� �ٽ� �׷���
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

        // ��������������������������������������������������������������������������������������������������
        // (A) QR ��� �Է� ��� (mode == 3)
        // ��������������������������������������������������������������������������������������������������
        if (mode == MODE_QR_INPUT) {
            handle_qr_input_mode(&qr_state, &mode);
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (B) QR ��üȭ�� ��� (mode == 4)
        // ��������������������������������������������������������������������������������������������������
        if (mode == MODE_QR_FULL) {
            handle_qr_full_mode(&qr_state, &mode);
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (C) ToDo ���
        // ��������������������������������������������������������������������������������������������������
        if (mode == MODE_TODO) {
            handle_todo_mode(&todo_state, &mode);
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (D) Chat ��� (ȣ��Ʈ/��Ʈ/�г��� �Է� �� ����)
        // ��������������������������������������������������������������������������������������������������
        if (mode == MODE_CHAT) {
            handle_chat_mode(&chat_state, &mode);
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (E) ������ ���: �κ� (mode == 0)
        // ��������������������������������������������������������������������������������������������������

        // (E-1) �Է�â(Command) �׸���
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "Command: %.*s", cmdlen, cmdbuf);
        wmove(win_input, 1, 11 + cmdlen);
        wrefresh(win_input);

        // (E-2) ���������� Ű �Է� �ޱ� (200ms ���)
        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch == KEY_RESIZE) {
            resized = 1;
            continue;
        }

        if (ch != ERR) {
            // �齺���̽�
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                }
            }
            // Enter �Է�
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';

                // exit �� ���α׷� ����
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;
                }
                // 1 �� To-Do ��� ����
                else if (cmdlen > 0 && cmdbuf[0] == '1') {
                    mode = MODE_TODO;
                    todo_state.len = 0;
                    memset(todo_state.buf, 0, sizeof(todo_state.buf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // 2 �� Chat ��� ����
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
                // 3 �� QR ��� �Է� ��� ����
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
                    mode = MODE_QR_INPUT;
                    qr_state.pathlen = 0;
                    memset(qr_state.pathbuf, 0, sizeof(qr_state.pathbuf));
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    continue;
                }
                // a <item> �� ToDo �׸� �߰� (���ȭ�� ���)
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char* item = cmdbuf + 2;
                    add_todo(item);
                    draw_todo(win_todo);
                }
                // f <filepath> �� QR ��üȭ�� ��� �ٷ� ����
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char* filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                    create_windows(1);
                    load_todo();
                    draw_todo(win_todo);
                }
                else {
                    /*==============================*/
                    /*    Unknown command ó��      */
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
                    napms(3000);  // 3�ʰ� ǥ���� �� �ڵ����� �κ�� ���ư�

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

                // �Է� ���� �ʱ�ȭ
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

    endwin();  // ncurses ����
}

/* Handle ToDo mode input */
static void handle_todo_mode(TodoState* state, int* mode) {
    // (1) �� ������: ���򸻰� ToDo ����Ʈ �ٽ� �׸���
    werase(win_custom);
    box(win_custom, 0, 0);
    draw_custom_help(win_custom);      // todo.c�� ���ǵ� ���� �Լ�
    werase(win_todo);
    box(win_todo, 0, 0);
    load_todo();                       // ���� �� �޸� �ε�
    draw_todo(win_todo);               // �޸� �� ȭ�� ���
    wrefresh(win_custom);
    wrefresh(win_todo);

    // (2) �Է�â �׸���
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "%s", state->buf);
    wmove(win_input, 1, 2 + state->len);
    wrefresh(win_input);

    // (3) �Է� ó��
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
            // �κ�� ���ư���
            *mode = MODE_LOBBY;
            create_windows(1);
            load_todo();
            draw_todo(win_todo);
            return;  // ���⼭ ��� �����Ͽ� ���� UI �ʱ�ȭ ȭ�� ����
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

        // (4) ���� �� �ٽ� �׸���
        werase(win_custom);
        box(win_custom, 0, 0);
        draw_custom_help(win_custom);
        load_todo();
        draw_todo(win_todo);
        werase(win_input);
        box(win_input, 0, 0);
        wrefresh(win_input);

        // �Է� ���� �ʱ�ȭ
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
            // �����ص� ����
        }
        chat_client(state->host, state->port, state->nickname, win_custom, win_input);
        chat_running = 0;
        pthread_join(timer_thread_id, NULL);

        // ä�� ��� ���� �� �� ���� UI�� ����
        state->step = 0;
        memset(state->host, 0, sizeof(state->host));
        memset(state->port_str, 0, sizeof(state->port_str));
        memset(state->nickname, 0, sizeof(state->nickname));
        // argv[0]���� ���α׷� ��ü�� execvp�� �����
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
    // 1) QR ��üȭ�� ���
    process_and_show_file(win_custom, qr_state->pathbuf);

    // 2) ����ڰ� ��q���� ������ ���α׷��� �ٽ� ����
    endwin();   // ncurses ���� ����

    // argv[0]���� ���α׷� ��ü�� execvp�� �����
    char* argv_new[] = { "./coshell", "ui", NULL };
    execvp(argv_new[0], argv_new);

    // execvp�� �����ϸ� ���⿡ ��
    perror("execvp failed");
    exit(1);
}

/*==============================*/
/*   ncurses �ʱ�ȭ/����/�������� */
/*==============================*/
static void cleanup_ncurses(void) {
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }
    endwin();
}

/*==============================*/
/*   Time ���ڿ� ���� �Լ�     */
/*==============================*/
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;    // KST �� USA ET
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;     // KST �� UK GMT
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

// ���� �������� �ð��� ������Ʈ�� �ִ� ������ �Լ� (Chat ��忡���� ���)
static void* timer_thread_fn(void* arg) {
    (void)arg;
    while (chat_running) {
        update_time(win_time);
        sleep(1);
    }
    return NULL;
}

// �־��� ���ڿ� �迭(lines)�� max_cols ���� ���� win�� ���
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

// ��������� ���� ����/��ġ
void create_windows(int in_lobby) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (1) ���� ������ ����
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }

    // (2) ũ�� ���
    int left_width = cols / 2;
    int right_width = cols - left_width;
    int left_height = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height = 5;    // border(2) + ��(1) + �ð� 3�� = 5
    int custom_y = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) ��� Ÿ��Ʋ
    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    // (4) ���� ���: Time ǥ�� â
    win_time = newwin(time_height, left_width, title_height, 0);
    // (5) ���� �߰�/�ϴ�: �κ� �Ǵ� ��� ������
    win_custom = newwin(custom_height, left_width, custom_y, 0);
    // (6) ������ ��ü: ToDoList â
    win_todo = newwin(rows - INPUT_HEIGHT, right_width, 0, left_width);
    // (7) �� �Ʒ�: Command �Է�â
    win_input = newwin(INPUT_HEIGHT, cols, rows - INPUT_HEIGHT, 0);

    // (8) ��ũ�� ����
    scrollok(win_time, FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo, TRUE);

    // (9) Time â �ʱ�ȭ
    box(win_time, 0, 0);
    mvwprintw(win_time, 0, 2, " Time ");
    mvwprintw(win_time, 1, 2, "Local:    --:--:--");
    mvwprintw(win_time, 2, 2, "USA  :    --:--:--");
    mvwprintw(win_time, 3, 2, "UK   :    --:--:--");
    wrefresh(win_time);

    // (10) Custom â(�κ� �Ǵ� ���)
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

    // (11) ToDoList â �ʱ�ȭ
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 1, 2, "=== ToDo List ===");
    wrefresh(win_todo);

    // (12) �Է�â �ʱ�ȭ
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "Command: ");
    wrefresh(win_input);
}

/*==============================*/
/*   Serveo �ͳ� (Chat ������)  */
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
                printf(">> Serveo �ͳ� �Ҵ� �Ϸ�: serveo.net:%d\n", remote_port);
                fflush(stdout);
                break;
            }
        }
        fclose(fp);
        return remote_port;
    }
}