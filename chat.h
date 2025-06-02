#ifndef CHAT_H
#define CHAT_H

#include <pthread.h>
#include <ncurses.h>

#define MAX_CLIENTS 5
#define BUF_SIZE    1024

/* 채팅 전역데이터 (coshell.c에 정의됨) */
extern pthread_mutex_t clients_lock;
extern int client_socks[MAX_CLIENTS];
extern int client_count;

/*==============================*/
/*    Chat 서버 및 클라이언트   */
/*==============================*/

/**
 * Chat 서버를 시작합니다.
 * - 지정한 포트에 바인딩(bind) 후 listen을 합니다.
 * - 최대 MAX_CLIENTS 클라이언트를 허용합니다.
 */
void chat_server(int port);

/**
 * Chat 클라이언트를 실행합니다.
 * - host: 접속할 서버 호스트 이름(또는 IP).
 * - port: 접속할 포트 번호.
 * - nickname: 사용자의 닉네임(메시지 전송 시 사용).
 * - win_chat: ncurses 상에서 채팅 메시지를 출력할 WINDOW*.
 * - win_input: ncurses 상에서 사용자 입력을 받을 WINDOW*.
 *
 * 이 함수를 호출하면 내부적으로:
 * 1) 서버에 연결(connect)하고,
 * 2) recv_handler 스레드를 생성하여 win_chat에 수신 메시지를 출력하며,
 * 3) 사용자가 win_input에서 텍스트를 입력하면 "[닉네임][HH:MM:SS] 메시지" 형식으로 전송하고
 *    동시에 자기 메시지를 win_chat에 출력합니다.
 */
void chat_client(const char *host,
                 int port,
                 const char *nickname,
                 WINDOW *win_chat,
                 WINDOW *win_input);

#endif // CHAT_H
