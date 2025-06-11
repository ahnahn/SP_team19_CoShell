#define _POSIX_C_SOURCE 200809L

#include "qr.h"

#include <ncurses.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MAX_QR_BYTES 700

// 내부 헬퍼: 터미널 화면 크기에 맞춰서 QR 버전을 자동 계산
static int pick_version_for_module(int max_module) {
    for (int v = 1; v <= 40; v++) {
        int module_size = 17 + 4 * v;
        if (module_size > max_module) {
            return v - 1;
        }
    }
    return 40;
}

// 전체화면에서 실제로 QR을 그리는 함수
static void show_qrcode_fullscreen(const char* path) {
    // SIGWINCH(창 크기 변경) 신호는 무시한 채, QR 창만 wrefresh만 수행하도록 설정
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    werase(stdscr);
    wrefresh(stdscr);

    // (1) 안전하게 그릴 수 있는 행/열 계산
    int safe_rows = rows - 2;         // 상하 여백 1줄씩
    int safe_cols_mod = cols / 2;     // 가로는 모듈 하나당 2칸 사용

    if (safe_rows < 1 || safe_cols_mod < 1) {
        // 너무 작은 터미널
        clear();
        mvprintw(rows/2, (cols-18)/2, "Terminal too small!");
        mvprintw(rows/2+1, (cols-28)/2, "Press any key to return");
        refresh();
        getch();
        signal(SIGWINCH, old_winch);
        return;
    }

    // (2) 버전 자동 계산
    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = pick_version_for_module(max_module);
    if (version < 1) version = 1;

    // (3) QR 전용 창 생성 (전체 화면 덮음)
    WINDOW *qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    // 상단 안내
    mvwprintw(qrwin, 0, 0, "Press 'q' to return (QR v%d)", version);
    wrefresh(qrwin);

    // (4) qrencode 실행 → 라인 단위로 읽어와서 qrwin에 출력
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
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q') {
            // 무시
        }
        delwin(qrwin);
        signal(SIGWINCH, old_winch);
        return;
    }

    char linebuf[1024];
    int row = 1;
    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        int len = (int)strlen(linebuf);
        if (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
            linebuf[len-1] = '\0';
        if (row >= safe_rows) break;
        mvwprintw(qrwin, row++, 0, "%s", linebuf);
        wrefresh(qrwin);
    }
    pclose(fp);

    // (5) 하단 안내: QR 보다가 'q' 누르면 종료
    mvwprintw(qrwin, rows-1, 0, "Press 'q' to return");
    wrefresh(qrwin);

    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            // 리사이즈 시: 화면 재출력만
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        }
        // 그 외 키 무시
    }

    delwin(qrwin);
    signal(SIGWINCH, old_winch);
}

// 터미널 UI 모드(전체화면)에서 호출되는 진입점
void process_and_show_file(WINDOW* custom, const char* path) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        // 파일이 없거나 일반 파일이 아닐 때
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        mvwprintw(custom, 3, 2, "Press any key to return to the main menu...");
        wrefresh(custom);
        wgetch(custom);
        return;
    }

    // 확장자 검사: .c 또는 .txt만 허용
    const char* ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".txt") != 0)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Only .c or .txt allowed: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    // (A) 파일 크기 검사
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
            // 무시
        }
        return;
    }

    // (B) 안내 후, 전체화면 모드로 넘어감
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Generating QR for: %s", path);
    mvwprintw(custom, 2, 2, "Press any key to view QR...");
    wrefresh(custom);

    napms(300);
    wgetch(custom);

    show_qrcode_fullscreen(path);
}

// CLI 모드에서 호출되는 진입점 (ASCII QR)
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
