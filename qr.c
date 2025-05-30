#include "qr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * show_qr: 
 *   기본적으로는 `qrencode` 명령을 호출하여 ASCII 모드로 QR 코드를 생성하고,
 *   그 결과를 stdout (또는 ncurses WINDOW)에 출력한다.
 *
 *   * win == NULL → printf() 로 직접 터미널에 출력
 *   * win != NULL → wprintw() 로 해당 윈도우에 출력 후 wrefresh()
 */
void show_qr(WINDOW *win, const char *filename) {
    // qrencode -t ASCII -o - 'filename'
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "qrencode -t ASCII -o - '%s'", filename);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        // 실패 시 간단히 에러 메시지
        if (win) {
            wprintw(win, "Failed to run qrencode\n");
            wrefresh(win);
        } else {
            fprintf(stderr, "Failed to run qrencode\n");
        }
        return;
    }

    char line[512];
    if (win) {
        werase(win);
        while (fgets(line, sizeof(line), fp)) {
            wprintw(win, "%s", line);
        }
        pclose(fp);
        wrefresh(win);
    } else {
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout);
        }
        pclose(fp);
    }
}
