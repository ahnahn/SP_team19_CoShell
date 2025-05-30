#ifndef QR_H
#define QR_H

/*
 * show_qr:
 *   filename 경로에 있는 파일을 QR 코드로 만들어서
 *   터미널(또는 ncurses 윈도우)로 ASCII QR 출력을 수행
 *
 * 파라미터:
 *   win   : ncurses WINDOW* (NULL이면 stdout에 출력)
 *   file  : QR로 만들 원본 파일 경로
 *
 * 사용 예:
 *   show_qr(NULL, "somefile.txt");        // 터미널(printf)로 ASCII QR 출력
 *   show_qr(win_chat, "/path/to/file.pdf"); // win_chat 윈도우에 출력
 */
#include <ncurses.h>  // WINDOW

void show_qr(WINDOW *win, const char *filename);

#endif // QR_H
