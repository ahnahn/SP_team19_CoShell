// mainframe2.c
// ������������������������������������������������������������������������������������������������������������������������������������������������������
// ���� ����:
//   gcc mainframe2.c -o mainframe2 -lncursesw
//
// �䱸 ��Ű�� (Ubuntu/Debian):
//   sudo apt update
//   sudo apt install libncursesw5-dev qrencode
//
// �ֿ� ���� ���� ���:
// 1) QR ��ü ȭ�� ���(show_qrcode_fullscreen)���� SIGWINCH�� ������ �����ϰ�,
//    KEY_RESIZE �̺�Ʈ�� ���͵� ȭ���� �ٽ� �׷� ������ �����ϸ�,
//    'q'�� ���� ���� �κ�� ���ư����� ó��.
// 2) popen()���� qrencode�� ȣ���� ��� ����� ���� �о� �ͼ� ncurses �����쿡 ���� ��� ��.
//    => �ܺ� ��(stdout/stderr)�� QR�� ������ ����.
// 3) �͹̳� �������� �� handle_winch()�� �κ� �ٽ� �׸����� �ߴٰ�,
//    QR ��� ���� �ÿ��� SIGWINCH�� SIG_IGN���� �ٲ� handle_winch�� ȣ����� �ʵ��� ��.
// 4) QR ��� ���� �� ���� ũ�� �˻�(�� 800����Ʈ) �� ��Press any key to view QR���� �� ��ü ȭ��
//    �� qrencode ����� ncurses �����쿡 ���(���� ���� 2ĭ �� ���� 1ĭ �������� �ڵ� ���� ����)
//    �� ��Press any key to return to lobby���� �߸� �ƹ� Ű�� �����߸� �κ�� ���ư�.
//    �� �̶� â ũ�� ����(KEY_RESIZE)�� wrefresh(qrwin)�� ȣ��, ���� ������ �ƴ�.
// 5) �͹̳� ũ�Ⱑ ����ġ�� ������ ��Terminal too small!�� �޽��� �� �ٷ� �κ� ����.
// 6) 800����Ʈ �ʰ� ������ ��File too large�� Press 'q' to return to lobby�� �޽��� ��
//    ��q�� Ű �Է����θ� �κ�� ���ư�.
//
// (�� Windows Terminal, PuTTY, GNOME Terminal �� ��𼭵� �����ϰ� �����ؾ� �մϴ�.)
// ������������������������������������������������������������������������������������������������������������������������������������������������������

#include <locale.h>
#include <ncurses.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#define INPUT_HEIGHT    3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511

// QR �ڵ忡 ����� �ִ� ���� ũ�� (����Ʈ)
#define MAX_QR_BYTES   800

static WINDOW* win_time = NULL;  // ���� ���: �ð� ǥ��
static WINDOW* win_custom = NULL;  // ���� �߰�/�ϴ�: �κ� �Ǵ� ��庰 ������
static WINDOW* win_todo = NULL;  // ������ ��ü: ToDoList â
static WINDOW* win_input = NULL;  // �� �Ʒ�: Ŀ�ǵ� �Է�â

static const char* lobby_text[] = {
    "Welcome!",
    "CoShell, short for \"cooperation in Shell,\" is a terminal-based collaboration toolbox.",
    "With To-Do List Management, you can share plans with your team members,",
    "exchange information via real-time chat,",
    "and transmit data using QR codes for groundbreaking data transfer.",
    "",
    "Enter a command below to start collaborating:",
    "",
    "1. To-Do List Management",
    "2. Chat",
    "3. QR Code",
    "",
    "You can exit the program at any time by typing exit"
};
static const int lobby_lines = sizeof(lobby_text) / sizeof(lobby_text[0]);

// ���� ����
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

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// ���α׷� ���� �� ncurses ����
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void cleanup_ncurses(void) {
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }
    endwin();
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// SIGWINCH(â ��������) �ڵ鷯: �κ� ȭ������ ���ư��� â �籸��
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void handle_winch(int sig) {
    (void)sig;
    endwin();
    refresh();
    clear();
    create_windows(1);
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// ���� ����/USA ET/UK GMT �ð� ���ڿ� ����
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    // �̱� ���� (KST+9 �� GMT-5 = -14�ð�)
    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    // ���� GMT (KST+9 �� GMT = -9�ð�)
    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;
    mktime(&tm_uk);
    strftime(uk_buf, len3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tm_uk);
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// win ���ο��� �ڵ� �ٹٲ��Ͽ� �ؽ�Ʈ �迭 ���
// start_y   : border �Ʒ� ù ��
// max_lines : ���ο� ����� �� �ִ� �ִ� �� ��(border ����)
// max_cols  : ���ο� ��� ������ �� ��(border ����, �¿� ���� �� ĭ��)
// lines     :  ����� ���ڿ� �迭
// n         :  lines �迭 ����
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void print_wrapped_lines(WINDOW* win, int start_y,
    int max_lines, int max_cols,
    const char* lines[], int n)
{
    int row = start_y;
    for (int i = 0; i < n && row < start_y + max_lines; i++) {
        const char* text = lines[i];
        int len = (text ? (int)strlen(text) : 0);
        int offset = 0;

        // �� ���̸� ���� �� ĭ ��� �� ���� ��
        if (len == 0) {
            if (row < start_y + max_lines) {
                mvwaddch(win, row++, 2, ' ');
            }
            continue;
        }
        // �� ���� max_cols ������ ������ ���
        while (offset < len && row < start_y + max_lines) {
            int chunk = (len - offset > max_cols ? max_cols : len - offset);
            mvwprintw(win, row++, 2, "%.*s", chunk, text + offset);
            offset += chunk;
        }
    }
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// 4�� ������ ����/�籸��
// in_lobby == 1 �� �κ� ���(ȯ������ ���), else �� ȭ��
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void create_windows(int in_lobby)
{
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
    int time_height = 5;  // border(2) + ��(1) + �ð� 3�� = 5
    int custom_y = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) �ֻ�� Ÿ��Ʋ
    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    // (4) ���� ���: Time â
    win_time = newwin(time_height, left_width, title_height, 0);

    // (5) ���� �߰�/�ϴ�: �κ� or ��� ������
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

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// win_time�� ����/USA ET/UK GMT �ð� ����
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void update_time(WINDOW* w)
{
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

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// pick_version_for_module:
//  max_module(���� �Ǵ� ���� ��� ����), �ּ� ���� ��ȯ
//  ��� ũ�� = 17 + 4*v  (v=����), module_size <= max_module
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static int pick_version_for_module(int max_module) {
    for (int v = 1; v <= 40; v++) {
        int module_size = 17 + 4 * v;
        if (module_size > max_module) {
            return v - 1;
        }
    }
    return 40;
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// show_qrcode_fullscreen:
//  - ��ü ȭ���� ���� qrwin ����
//  - SIGWINCH ���� �� KEY_RESIZE �� wrefresh�� ȣ�� (ȭ�� ���� ����)
//  - ���� ��� = ���� 2ĭ �� ���� 1ĭ�� �������� �ڵ� ���� ����
//  - qrencode ����� popen()���� �޾ƿͼ� ncurses �����쿡 ���� ���
//  - ��q�� Ű�� �ƴ� �ٸ� Ű�� ������ �״�� �������� �ʰ�, ��Press any key to return to lobby�� �ȳ��� ����
//    �ƹ� Ű�� ���� ���� �κ�� ���ư�. ��������(KEY_RESIZE)�� ��� wrefresh�� ��.
//  - �͹̳��� �ʹ� ������ ��Terminal too small!�� �޽��� �� �ٷ� �κ�� ���ư�.
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void show_qrcode_fullscreen(const char* path)
{
    // (1) SIGWINCH ���� �� handle_winch ȣ����� ����
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    // (2) ��ü ȭ�� ũ�� ��������
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (3) ȭ�� ��ü ����� ��������
    werase(stdscr);
    wrefresh(stdscr);

    // (4) safe ���� ��� (��� �ȳ� 1�� + �ϴ� �ȳ� 1�� ����)
    int safe_rows = rows - 2;
    int safe_cols_mod = cols / 2;  // �� ����� ���� 2ĭ���� ��
    if (safe_rows < 1 || safe_cols_mod < 1) {
        // �ʹ� ������ �ٷ� �κ�� ���ư�
        clear();
        mvprintw(rows / 2, (cols - 24) / 2, "Terminal too small!");
        mvprintw(rows / 2 + 1, (cols - 28) / 2, "Press any key to return");
        refresh();
        getch();
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    // (5) ���� �ڵ� ����
    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = pick_version_for_module(max_module);
    if (version < 1) version = 1;

    // (6) qrwin ���� (��ü ȭ�� ����)
    WINDOW* qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    // (7) ��� �ȳ�
    mvwprintw(qrwin, 0, 0, "Press 'q' to return to lobby (QR v%d)", version);
    wrefresh(qrwin);

    // (8) qrencode�� popen���� ȣ��: �׵θ� ����(m=0), ���� ���� ����(-l L)
    //     -r �ɼ��� ���� ����Ʈ ��Ʈ��(�̹���) ��� �� CLI������ UTF8 �ؽ�Ʈ ���(-t UTF8)�� ���
    //     popen() ����� �� �پ� �о� �ͼ� qrwin�� ���
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
        path, version);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
        mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
        wrefresh(qrwin);
        int c;
        keypad(qrwin, TRUE);
        nodelay(qrwin, FALSE);
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q') {
            // �ٸ� Ű�� ����
        }
        delwin(qrwin);
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    // (9) QR �ؽ�Ʈ ���: ù ��° �ٺ��� safe_rows��ŭ
    char linebuf[1024];
    int row = 1;
    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        int len = (int)strlen(linebuf);
        if (len > 0 && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
            linebuf[len - 1] = '\0';
        if (row >= safe_rows) break;
        mvwprintw(qrwin, row++, 0, "%s", linebuf);
        wrefresh(qrwin);
    }
    pclose(fp);

    // (10) �ϴ� �ȳ�
    mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return to lobby");
    wrefresh(qrwin);

    // (11) KEY_RESIZE�� wrefresh�� ȣ��, 'q'�� ������ ����
    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            // �ܼ��� ȭ�� �������ø�
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        }
        // �� �� Ű�� �����ϰ� ��� ���
    }

    // (12) QR ������ ���� �� �κ�� ����
    delwin(qrwin);
    clear();
    create_windows(1);

    // SIGWINCH ����
    signal(SIGWINCH, old_winch);
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// process_and_show_file:
//  - ������ .c �Ǵ� .txt���� �˻�
//  - ũ�� �˻�: 800����Ʈ �ʰ� �� ��File too large�� Press 'q' to return�� ��� �� �κ� ����
//  - �׷��� ������ ��Press any key to view QR���� �޽��� �� show_qrcode_fullscreen()
// ����������������������������������������������������������������������������������������������������������������������������������������������������
static void process_and_show_file(WINDOW* custom, const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    // Ȯ���� �˻�
    const char* ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".txt") != 0)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Only .c or .txt allowed: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    // (A) ���� ũ�� �˻�
    if (st.st_size > MAX_QR_BYTES) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File too large (%ld bytes).", (long)st.st_size);
        mvwprintw(custom, 2, 2, "Max allowed for QR: %d bytes", MAX_QR_BYTES);
        mvwprintw(custom, 4, 2, "Press 'q' to return to lobby");
        wrefresh(custom);

        // ��q�� �Ǵ� ��Q�� Ű �Է� ��� �� ������ �κ�� ����
        int c;
        keypad(custom, TRUE);
        nodelay(custom, FALSE);
        while ((c = wgetch(custom)) != 'q' && c != 'Q') {
            // �ٸ� Ű�� ����
        }
        clear();
        create_windows(1);
        return;
    }

    // (B) ũ�� ���� �� QR ���� �� �ȳ�
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Generating QR for: %s", path);
    mvwprintw(custom, 2, 2, "Press any key to view QR...");
    wrefresh(custom);

    // ª�� ��� �� Ű ���
    napms(300);
    wgetch(custom);

    // (C) QR ��ü ȭ�� ���
    show_qrcode_fullscreen(path);
}

// ����������������������������������������������������������������������������������������������������������������������������������������������������
// main: ncurses �ʱ�ȭ �� �κ� ��� �� �̺�Ʈ ����
// ����������������������������������������������������������������������������������������������������������������������������������������������������
int main(int argc, char* argv[])
{
    // UTF-8 ������ Ȱ��ȭ
    setlocale(LC_ALL, "");

    // ���α׷� ���� �� ncurses ���� ����
    atexit(cleanup_ncurses);

    // Ctrl-C, Ctrl-Z ���� (exit ������θ� ����)
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGWINCH, handle_winch);

    // ncurses �ʱ�ȭ
    initscr();
    cbreak();      // ��� �Է�
    noecho();      // Ű ȭ�鿡 ǥ�� �� ��
    keypad(stdscr, TRUE);
    curs_set(1);   // Ŀ�� ���̱�

    // ù ȭ��: �κ�
    create_windows(1);

    // ��ɾ� ����
    char cmdbuf[MAX_CMD_LEN + 1] = { 0 };
    int  cmdlen = 0;

    // QR ��� �Է¿� ����
    char pathbuf[MAX_PATH_LEN + 1] = { 0 };
    int  pathlen = 0;

    // �ð� ���� Ÿ�̸�
    time_t last_time = 0;

    // ��� ����:
    //  0=�κ�(Ŀ�ǵ� ���),
    //  1=To-Do ���(����),
    //  2=Chat ���(����),
    //  3=QR ��� �Է� ���,
    //  4=QR ��ü ȭ�� ���
    int mode = 0;

    // �Է�â ���� ��ǥ
    int input_y = 1, input_x = 10;

    while (1) {
        // 1�ʸ��� �ð� ����
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // ��������������������������������������������������������������������������������������������������
        // (A) QR ��� �Է� ���
        // ��������������������������������������������������������������������������������������������������
        if (mode == 3) {
            // win_custom�� ��� �Է� �ȳ�
            werase(win_custom);
            box(win_custom, 0, 0);
            mvwprintw(win_custom, 1, 2, "Enter path for QR code:");
            mvwprintw(win_custom, 2, 2, "%s", pathbuf);
            wrefresh(win_custom);

            // �Է�â�� Ŀ�� �̵�
            wmove(win_input, input_y, input_x + pathlen);
            wrefresh(win_input);

            // ������ �Է� �˻�(200ms)
            wtimeout(win_input, 200);
            int ch = wgetch(win_input);
            if (ch != ERR) {
                if (ch == KEY_BACKSPACE || ch == 127) {
                    if (pathlen > 0) {
                        pathlen--;
                        pathbuf[pathlen] = '\0';
                        mvwprintw(win_input, input_y, input_x + pathlen, " ");
                        wmove(win_input, input_y, input_x + pathlen);
                        wrefresh(win_input);
                    }
                }
                else if (ch == '\n' || ch == KEY_ENTER) {
                    // QR ��ü ȭ�� ���� ��ȯ
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
                        mvwprintw(win_input, input_y, input_x + pathlen - 1, "%c", ch);
                        wmove(win_input, input_y, input_x + pathlen);
                        wrefresh(win_input);
                    }
                }
            }
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (B) QR ��ü ȭ�� ���
        // ��������������������������������������������������������������������������������������������������
        if (mode == 4) {
            // ���� ó�� �� QR �׸���
            process_and_show_file(win_custom, pathbuf);
            mode = 0;  // �κ� ���� ����
            continue;
        }

        // ��������������������������������������������������������������������������������������������������
        // (C) ������ ���: �κ� / To-Do / Chat / Ŀ�ǵ�
        // ��������������������������������������������������������������������������������������������������
        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch != ERR) {
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                    mvwprintw(win_input, input_y, input_x + cmdlen, " ");
                    wmove(win_input, input_y, input_x + cmdlen);
                    wrefresh(win_input);
                }
            }
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';

                // exit �� ���α׷� ����
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;
                }
                // 1 �� To-Do ��� (����)
                else if (cmdlen > 0 && cmdbuf[0] == '1') {
                    mode = 1;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Entering To-Do List mode...");
                    wrefresh(win_custom);
                }
                // 2 �� Chat ��� (����)
                else if (cmdlen > 0 && cmdbuf[0] == '2') {
                    mode = 2;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Entering Chat mode...");
                    wrefresh(win_custom);
                }
                // 3 �� QR �ڵ� ��� (��� �Է����� �Ѿ)
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
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
                // a <item> �� To-Do �߰� ����: �� �Ʒ� �ٿ� �߰�
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char* item = cmdbuf + 2;
                    int y_max, x_max;
                    getmaxyx(win_todo, y_max, x_max);
                    mvwprintw(win_todo, y_max - 2, 2, "- %s", item);
                    wrefresh(win_todo);
                }
                // f <path> �� ���� ��η� ���� QR ��� (800����Ʈ �˻� ����)
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char* filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                }
                // �� �� �� Unknown command
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
                    print_wrapped_lines(win_custom, 1, inner_lines, inner_cols,
                        lobby_text, lobby_lines);
                    wrefresh(win_custom);
                }

                // �Է�â �ʱ�ȭ
                cmdlen = 0;
                memset(cmdbuf, 0, sizeof(cmdbuf));
                werase(win_input);
                box(win_input, 0, 0);
                mvwprintw(win_input, 1, 2, "Command: ");
                wrefresh(win_input);
            }
            // �Ϲ� ���� �Է� (ASCII)
            else if (ch >= 32 && ch <= 126) {
                if (cmdlen < MAX_CMD_LEN) {
                    cmdbuf[cmdlen++] = (char)ch;
                    mvwprintw(win_input, input_y, input_x + cmdlen - 1, "%c", ch);
                    wmove(win_input, input_y, input_x + cmdlen);
                    wrefresh(win_input);
                }
            }
            // ������ Ű ����
        }
    }

    return 0;
}
