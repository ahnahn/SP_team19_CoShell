#include "chat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

/* 채팅 전역변수 정의 */
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
int client_socks[MAX_CLIENTS];
int client_count = 0;

/*==============================*/
/*       Chat 서버 시작        */
/*==============================*/
void chat_server(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_sock);
        return;
    }
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_sock);
        return;
    }

    printf("Chat server listening on port %d...\n", port);

    while (1) {
        int client = accept(server_sock, NULL, NULL);
        if (client < 0) {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&clients_lock);
        if (client_count < MAX_CLIENTS) {
            client_socks[client_count++] = client;
            int *p = malloc(sizeof(int));
            *p = client;
            pthread_t tid;
            pthread_create(&tid, NULL, client_handler, p);
            pthread_detach(tid);
        } else {
            close(client);
        }
        pthread_mutex_unlock(&clients_lock);
    }
}

/*==============================*/
/*    Chat 클라이언트 핸들러     */
/*==============================*/
void *client_handler(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < client_count; i++) {
            if (client_socks[i] != sock) {
                send(client_socks[i], buf, len, 0);
            }
        }
        pthread_mutex_unlock(&clients_lock);
    }

    close(sock);

    // 연결 해제된 클라이언트를 목록에서 제거
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < client_count; i++) {
        if (client_socks[i] == sock) {
            client_socks[i] = client_socks[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    return NULL;
}

/*==============================*/
/*    Chat 클라이언트 접속       */
/*==============================*/
void chat_client(const char *host, int port) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        fprintf(stderr, "Failed to resolve host: %s\n", host);
        return;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("socket");
        freeaddrinfo(res);
        return;
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(sock);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);

    // 서버 메시지 수신 전담 스레드
    int *sock_ptr = malloc(sizeof(int));
    *sock_ptr = sock;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, recv_handler, sock_ptr);

    // 표준 입력→서버로 전송
    char msg[BUF_SIZE];
    while (fgets(msg, sizeof(msg), stdin)) {
        if (send(sock, msg, strlen(msg), 0) <= 0) break;
    }
    close(sock);
    printf("Chat client exiting.\n");
}

/*==============================*/
/*  서버로부터 수신 핸들러       */
/*==============================*/
void *recv_handler(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            printf("\nConnection closed by server.\n");
            break;
        }
        buf[len] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
    return NULL;
}
