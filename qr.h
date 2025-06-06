#ifndef QR_H
#define QR_H

#include <ncurses.h>

/**
 * 터미널 UI 모드(전체화면)에서, 주어진 파일(path)의 내용을 QR 코드로 보여줍니다.
 * - custom: QR 안내/메시지를 띄울 WINDOW*
 * - path: .c 또는 .txt 파일 경로
 *
 * 내부적으로:
 *  1) 파일 존재 여부와 확장자(.c/.txt) 체크
 *  2) 크기(MAX_QR_BYTES=800 바이트) 초과 시 오류 메시지 후 return
 *  3) ‘Press any key to view QR…’ 메시지 후 전체화면 QR 창(show_qrcode_fullscreen) 호출
 */
void process_and_show_file(WINDOW* custom, const char* path);

/**
 * CLI 모드에서, 주어진 문자열(filename)에 대해 ASCII 모드 QR 코드를 출력합니다.
 * - filename: QR로 만들 데이터(일반적으로 파일 경로)
 *
 * 내부적으로:
 *   qrencode -t ASCII -o - 'filename'
 */
void show_qr_cli(const char* filename);

#endif // QR_H
