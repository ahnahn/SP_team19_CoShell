// qr.h
#ifndef QR_H
#define QR_H

#include <ncurses.h>

/**
 * 전체 화면으로 QR 코드를 생성·표시합니다.
 * @param path .c/.txt 파일 또는 .gz 압축 파일 경로
 */
void show_qrcode_fullscreen(const char *path);

/**
 * custom 윈도우에서 path 파일을 검사하여,
 * - 존재하지 않으면 에러 -> 로비 복귀
 * - 800바이트 초과 시 에러 -> 로비 복귀
 * - 확장자(.c/.txt)가 아니면 에러 -> 로비 복귀
 * - 그 외 정상 파일이면 전체화면 QR 출력
 */
void process_and_show_file(WINDOW *custom, const char *path);

#endif // QR_H
