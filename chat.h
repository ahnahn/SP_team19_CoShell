#ifndef CHAT_H
#define CHAT_H

#include <pthread.h>
#include <ncurses.h>

#define MAX_CLIENTS 5
#define BUF_SIZE    1024

/* ä�� ���������� (coshell.c�� ���ǵ�) */
extern pthread_mutex_t clients_lock;
extern int client_socks[MAX_CLIENTS];
extern int client_count;

// coshell.c ���� �����ϴ� UI �������� �Լ�
extern void create_windows(int in_lobby);
// �������� �� ������� ������
extern WINDOW* win_custom;
extern WINDOW* win_input;

/*==============================*/
/*    Chat ���� �� Ŭ���̾�Ʈ   */
/*==============================*/

/**
 * Chat ������ �����մϴ�.
 * - ������ ��Ʈ�� ���ε�(bind) �� listen�� �մϴ�.
 * - �ִ� MAX_CLIENTS Ŭ���̾�Ʈ�� ����մϴ�.
 */
void chat_server(int port);

/**
 * Chat Ŭ���̾�Ʈ�� �����մϴ�.
 * - host: ������ ���� ȣ��Ʈ �̸�(�Ǵ� IP).
 * - port: ������ ��Ʈ ��ȣ.
 * - nickname: ������� �г���(�޽��� ���� �� ���).
 * - win_chat: ncurses �󿡼� ä�� �޽����� ����� WINDOW*.
 * - win_input: ncurses �󿡼� ����� �Է��� ���� WINDOW*.
 *
 * �� �Լ��� ȣ���ϸ� ����������:
 * 1) ������ ����(connect)�ϰ�,
 * 2) recv_handler �����带 �����Ͽ� win_chat�� ���� �޽����� ����ϸ�,
 * 3) ����ڰ� win_input���� �ؽ�Ʈ�� �Է��ϸ� "[�г���][HH:MM:SS] �޽���" �������� �����ϰ�
 *    ���ÿ� �ڱ� �޽����� win_chat�� ����մϴ�.
 */
void chat_client(const char* host,
    int port,
    const char* nickname,
    WINDOW* win_chat,
    WINDOW* win_input);

#endif // CHAT_H