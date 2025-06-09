#define _POSIX_C_SOURCE 200809L

#include "qr.h"

#include <ncurses.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MAX_QR_BYTES 800

// ���� ����: �͹̳� ȭ�� ũ�⿡ ���缭 QR ������ �ڵ� ���
static int pick_version_for_module(int max_module) {
    for (int v = 1; v <= 40; v++) {
        int module_size = 17 + 4 * v;
        if (module_size > max_module) {
            return v - 1;
        }
    }
    return 40;
}

// ��üȭ�鿡�� ������ QR�� �׸��� �Լ�
static void show_qrcode_fullscreen(const char* path) {
    // SIGWINCH(â ũ�� ����) ��ȣ�� ������ ä, QR â�� wrefresh�� �����ϵ��� ����
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    werase(stdscr);
    wrefresh(stdscr);

    // (1) �����ϰ� �׸� �� �ִ� ��/�� ���
    int safe_rows = rows - 2;         // ���� ���� 1�پ�
    int safe_cols_mod = cols / 2;     // ���δ� ��� �ϳ��� 2ĭ ���

    if (safe_rows < 1 || safe_cols_mod < 1) {
        // �ʹ� ���� �͹̳�
        clear();
        mvprintw(rows / 2, (cols - 18) / 2, "Terminal too small!");
        mvprintw(rows / 2 + 1, (cols - 28) / 2, "Press any key to return");
        refresh();
        getch();
        signal(SIGWINCH, old_winch);
        return;
    }

    // (2) ���� �ڵ� ���
    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = pick_version_for_module(max_module);
    if (version < 1) version = 1;

    // (3) QR ���� â ���� (��ü ȭ�� ����)
    WINDOW* qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    // ��� �ȳ�
    mvwprintw(qrwin, 0, 0, "Press 'q' to return (QR v%d)", version);
    wrefresh(qrwin);

    // (4) qrencode ���� �� ���� ������ �о�ͼ� qrwin�� ���
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
        path, version);

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
        mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
        wrefresh(qrwin);

        keypad(qrwin, TRUE);
        nodelay(qrwin, FALSE);
        int c;
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q') {
            // ����
        }
        delwin(qrwin);
        signal(SIGWINCH, old_winch);
        return;
    }

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

    // (5) �ϴ� �ȳ�: QR ���ٰ� 'q' ������ ����
    mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
    wrefresh(qrwin);

    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            // �������� ��: ȭ�� ����¸�
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        }
        // �� �� Ű ����
    }

    delwin(qrwin);
    signal(SIGWINCH, old_winch);
}

// �͹̳� UI ���(��üȭ��)���� ȣ��Ǵ� ������
void process_and_show_file(WINDOW* custom, const char* path) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        // ������ ���ų� �Ϲ� ������ �ƴ� ��
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        mvwprintw(custom, 3, 2, "Press any key to return to the main menu...");
        wrefresh(custom);
        wgetch(custom);
        return;
    }

    // Ȯ���� �˻�: .c �Ǵ� .txt�� ���
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
        mvwprintw(custom, 4, 2, "Press 'q' to return");
        wrefresh(custom);

        int c;
        keypad(custom, TRUE);
        nodelay(custom, FALSE);
        while ((c = wgetch(custom)) != 'q' && c != 'Q') {
            // ����
        }
        return;
    }

    // (B) �ȳ� ��, ��üȭ�� ���� �Ѿ
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Generating QR for: %s", path);
    mvwprintw(custom, 2, 2, "Press any key to view QR...");
    wrefresh(custom);

    napms(300);
    wgetch(custom);

    show_qrcode_fullscreen(path);
}

// CLI ��忡�� ȣ��Ǵ� ������ (ASCII QR)
void show_qr_cli(const char* filename) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "qrencode -t ASCII -o - '%s'", filename);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "Failed to run qrencode\n");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        fputs(line, stdout);
    }
    pclose(fp);
}