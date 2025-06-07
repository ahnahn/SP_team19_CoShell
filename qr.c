// qr.c
#include "qr.h"
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SIZE_THRESHOLD 800

extern void create_windows(int in_lobby);

// 터미널 크기에 맞춰 QR 버전 선택
static int pick_version_for_module(int max_module) {
    for (int v = 1; v <= 40; v++) {
        if (17 + 4 * v > max_module) return v - 1;
    }
    return 40;
}

void show_qrcode_fullscreen(const char *path) {
    void (*old_winch)(int) = signal(SIGWINCH, SIG_DFL);
    int rows, cols, ch;

    do {
        clear(); refresh();
        getmaxyx(stdscr, rows, cols);
        int safe_rows = rows - 2;
        int safe_cols = cols;
        int max_mod = safe_rows < safe_cols ? safe_rows : safe_cols;

        if (max_mod < 21) {
            mvprintw(rows/2, (cols-28)/2, "Terminal too small for QR!");
            mvprintw(rows/2+1, (cols-28)/2, "Press 'q' to return");
            refresh();
            ch = getch();
            break;
        }

        int version = pick_version_for_module(max_mod);
        if (version < 1) version = 1;

        WINDOW *qrwin = newwin(rows, cols, 0, 0);
        keypad(qrwin, TRUE);
        scrollok(qrwin, FALSE);
        werase(qrwin);

        mvwprintw(qrwin, 0, 0, "Press 'q' to return (QR v%d) and When you change the window size, press spacekey to refresh.", version);
        wrefresh(qrwin);

        char cmd[1024];
        const char *ext = strrchr(path, '.');
        if (ext && strcmp(ext, ".gz") == 0) {
            snprintf(cmd, sizeof(cmd),
                "gzip -dc \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
                path, version);
        } else {
            snprintf(cmd, sizeof(cmd),
                "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
                path, version);
        }

        FILE *fp = popen(cmd, "r");
        if (!fp) {
            mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
            mvwprintw(qrwin, rows-1, 0, "Press 'q' to return");
            wrefresh(qrwin);
            ch = wgetch(qrwin);
            delwin(qrwin);
            break;
        }

        char buf[1024];
        int row = 1;
        while (row < safe_rows && fgets(buf, sizeof(buf), fp)) {
            int l = strlen(buf);
            if (l && (buf[l-1]=='\n' || buf[l-1]=='\r')) buf[--l] = '\0';
            mvwprintw(qrwin, row++, 0, "%s", buf);
            wrefresh(qrwin);
        }
        pclose(fp);

        mvwprintw(qrwin, rows-1, 0, "Press 'q' to return to lobby");
        wrefresh(qrwin);

        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            delwin(qrwin);
            continue;
        }
        delwin(qrwin);
    } while (ch != 'q');

    clear(); refresh();
    create_windows(1);
    signal(SIGWINCH, old_winch);
}

void process_and_show_file(WINDOW *custom, const char *path) {
    struct stat st;
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    // 1) 파일 존재 및 정규 파일 확인
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        werase(custom); box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Error: File not found: %s", path);
        mvwprintw(custom, 3, 2, "Press any key to return");
        wrefresh(custom);
        wgetch(custom);
        signal(SIGWINCH, old_winch);
        return;
    }
    // 2) 크기 초과 확인
    if (st.st_size > SIZE_THRESHOLD) {
        werase(custom); box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Error: File too large (> %d bytes)", SIZE_THRESHOLD);
        mvwprintw(custom, 3, 2, "Press any key to return");
        wrefresh(custom);
        wgetch(custom);
        signal(SIGWINCH, old_winch);
        return;
    }
    // 3) 확장자 검사
    const char *ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") && strcmp(ext, ".txt"))) {
        werase(custom); box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Error: Only .c or .txt allowed");
        mvwprintw(custom, 3, 2, "Press any key to return");
        wrefresh(custom);
        wgetch(custom);
        signal(SIGWINCH, old_winch);
        return;
    }

    signal(SIGWINCH, old_winch);
    // 4) 정상 파일 → 전체화면 QR 표시
    show_qrcode_fullscreen(path);
}
