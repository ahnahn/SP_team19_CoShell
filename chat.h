#ifndef CHAT_H
#define CHAT_H

#include <pthread.h>

#define MAX_CLIENTS 5
#define BUF_SIZE    1024

/* 채팅 전역데이터 */
extern pthread_mutex_t clients_lock;
extern int client_socks[MAX_CLIENTS];
extern int client_count;

/* Chat 서버 시작 (port에 바인딩하여 listen) */
void chat_server(int port);

/* Chat 서버로 연결된 각 클라이언트 처리 스레드 */
void *client_handler(void *arg);

/* 채팅 클라이언트(외부 서버에 접속) */
void chat_client(const char *host, int port);

/* 채팅 클라이언트에서 서버 메시지 수신 스레드 */
void *recv_handler(void *arg);

#endif // CHAT_H
