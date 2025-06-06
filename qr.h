#ifndef QR_H
#define QR_H

#include <ncurses.h>

/*
 * 전체 화면으로 QR 코드를 보여주는 함수.
 *   - path: QR로 변환할 파일 경로 (".c" 또는 ".txt"만 허용)
 *   - 내부에서 터미널 크기를 재검사하고, 작으면 바로 메인 UI로 복귀
 */
void show_qrcode_fullscreen(const char *path);

/*
 * 파일 경로를 받아서:
 *   1) 파일이 없으면 에러 메시지 후 메인 UI로 돌아감
 *   2) 확장자가 잘못되었으면 간단 메시지 후 복귀
 *   3) 크기가 너무 크면 메시지 후 복귀
 *   4) 정상이라면 안내 메시지 후 show_qrcode_fullscreen 호출
 *   - custom: 왼쪽 중간/하단에 해당하는 ncurses 창
 */
void process_and_show_file(WINDOW *custom, const char *path);

/*
 * CLI 모드에서 QR 코드를 ASCII로 터미널에 출력하는 함수.
 *   - filename: QR을 생성할 파일 경로
 */
void show_qr_cli(const char *filename);

#endif // QR_H
