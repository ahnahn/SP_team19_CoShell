#ifndef QR_H
#define QR_H

#include <ncurses.h>

/**
 * �͹̳� UI ���(��üȭ��)����, �־��� ����(path)�� ������ QR �ڵ�� �����ݴϴ�.
 * - custom: QR �ȳ�/�޽����� ��� WINDOW*
 * - path: .c �Ǵ� .txt ���� ���
 *
 * ����������:
 *  1) ���� ���� ���ο� Ȯ����(.c/.txt) üũ
 *  2) ũ��(MAX_QR_BYTES=800 ����Ʈ) �ʰ� �� ���� �޽��� �� return
 *  3) ��Press any key to view QR���� �޽��� �� ��üȭ�� QR â(show_qrcode_fullscreen) ȣ��
 */
void process_and_show_file(WINDOW* custom, const char* path);

/**
 * CLI ��忡��, �־��� ���ڿ�(filename)�� ���� ASCII ��� QR �ڵ带 ����մϴ�.
 * - filename: QR�� ���� ������(�Ϲ������� ���� ���)
 *
 * ����������:
 *   qrencode -t ASCII -o - 'filename'
 */
void show_qr_cli(const char* filename);

#endif // QR_H