#include "qr.h"
#include <ncurses.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * 아래 함수들은 coshell.c나 todo.c에 정의되어 있는 것들이므로,
 * extern으로 가져다 씁니다.
 */
extern void create_windows(int in_lobby);
extern void load_todo(void);
extern void draw_todo(WINDOW *win_todo);

/* QR 전체화면 모드에서 'Press q to return' 을 처리 */
void show_qrcode_fullscreen(const char *path) {
    // SIGWINCH 신호를 무시 → 내부에서만 KEY_RESIZE 처리
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    werase(stdscr);
    wrefresh(stdscr);

    int safe_rows = rows - 2;
    int safe_cols_mod = cols / 2;  // 모듈 하나당 가로폭 2칸
    if (safe_rows < 1 || safe_cols_mod < 1) {
        // 너무 작은 터미널
        clear();
        mvprintw(rows/2, (cols - 18) / 2, "Terminal too small!");
        mvprintw(rows/2 + 1, (cols - 28) / 2, "Press any key to return");
        refresh();
        getch();
        clear();
        create_windows(1);
        load_todo();
        draw_todo(win_todo);
        signal(SIGWINCH, old_winch);
        return;
    }

    // (1) 자동 버전 선택
    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = 1;
    for (int v = 1; v <= 40; v++) {
        int module_size = 17 + 4*v;
        if (module_size > max_module) {
            version = v - 1;
            break;
        }
    }
    if (version < 1) version = 1;

    // (2) 전체 화면을 덮는 새 창 생성
    WINDOW *qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    // (3) 상단 안내
    mvwprintw(qrwin, 0, 0, "Press 'q' to return (QR v%d)", version);
    wrefresh(qrwin);

    // (4) qrencode를 popen으로 실행하고, 한 줄씩 읽어서 출력
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
             path, version);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
        mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
        wrefresh(qrwin);
        keypad(qrwin, TRUE);
        nodelay(qrwin, FALSE);
        int c;
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q');
        delwin(qrwin);
        clear();
        create_windows(1);
        load_todo();
        draw_todo(win_todo);
        signal(SIGWINCH, old_winch);
        return;
    }

    char linebuf[1024];
    int row = 1;
    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        int len = strlen(linebuf);
        if (len > 0 && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r')) {
            linebuf[len - 1] = '\0';
        }
        if (row >= safe_rows) break;
        mvwprintw(qrwin, row++, 0, "%s", linebuf);
        wrefresh(qrwin);
    }
    pclose(fp);

    // (5) 하단 안내
    mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
    wrefresh(qrwin);

    // (6) 리턴 또는 리사이즈 처리
    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            // 단순히 다시 출력만 함
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') break;
    }

    // (7) 창 정리 → 메인 UI로 복귀
    delwin(qrwin);
    clear();
    create_windows(1);
    load_todo();
    draw_todo(win_todo);
    signal(SIGWINCH, old_winch);
}

/*
 * process_and_show_file:
 *  - stat() 실패 또는 일반 파일이 아니면 “파일 없음” 메시지 후 복귀
 *  - 확장자가 ".c" 또는 ".txt" 아니면 “허용되지 않음” 메시지 후 복귀
 *  - 크기가 MAX_QR_BYTES보다 크면 “too large” 메시지 후 복귀
 *  - 정상 파일이면 안내 후 show_qrcode_fullscreen()
 */
void process_and_show_file(WINDOW* custom, const char* path) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        mvwprintw(custom, 3, 2, "Press any key to return to main UI...");
        wrefresh(custom);
        wgetch(custom);

        create_windows(1);
        load_todo();
        draw_todo(win_todo);
        return;
    }

    const char *ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".txt") != 0)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Only .c or .txt allowed: %s", path);
        wrefresh(custom);
        napms(1500);

        create_windows(1);
        load_todo();
        draw_todo(win_todo);
        return;
    }

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
        while ((c = wgetch(custom)) != 'q' && c != 'Q') { }
        create_windows(1);
        load_todo();
        draw_todo(win_todo);
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

/*
 * show_qr_cli:
 *  - CLI 모드로 실행했을 때, qrencode -t ASCII 로 QR을 콘솔 출력
 */
void show_qr_cli(const char *filename) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "qrencode -t ASCII -o - '%s'", filename);
    FILE *fp = popen(cmd, "r");
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
