#define _POSIX_C_SOURCE 200809L   // getaddrinfo 등 활성화

#include "chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <ncurses.h>

/*==============================*/
/*   전역 변수 정의 (chat.h에서 extern)  */
/*==============================*/
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
int client_socks[MAX_CLIENTS];
int client_count = 0;

/*==============================*/
/*   내부 전역 변수              */
/*==============================*/
static int sockfd;             // 연결된 소켓 디스크립터
static WINDOW *g_win_chat;     // ncurses 채팅 출력 창
static WINDOW *g_win_input;    // ncurses 입력 창
static char g_nickname[64];     // 사용자 닉네임

/*==============================*/
/*    함수 전방 선언             */
/*==============================*/
static void *chat_server_handler(void *arg);
static void *chat_recv_handler_internal(void *arg);

/*==============================*/
/*    Chat 서버 구현            */
/*==============================*/
void chat_server(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return;
    }
    // SO_REUSEADDR 설정
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
            int *pclient = malloc(sizeof(int));
            if (!pclient) {
                perror("malloc");
                close(client);
                pthread_mutex_unlock(&clients_lock);
                continue;
            }
            *pclient = client;
            pthread_t tid;
            pthread_create(&tid, NULL, chat_server_handler, pclient);
            pthread_detach(tid);
        } else {
            // 최대 클라이언트 초과 시 바로 닫기
            close(client);
        }
        pthread_mutex_unlock(&clients_lock);
    }
}

static void *chat_server_handler(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf)-1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        // 받은 메시지를 다른 클라이언트들에게 브로드캐스트
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < client_count; i++) {
            if (client_socks[i] != sock) {
                send(client_socks[i], buf, len, 0);
            }
        }
        pthread_mutex_unlock(&clients_lock);
    }

    // 연결 종료 시 목록에서 제거
    close(sock);
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
/*    Chat 클라이언트 구현       */
/*==============================*/
void chat_client(const char *host,
                 int port,
                 const char *nickname,
                 WINDOW *win_chat,
                 WINDOW *win_input)
{
    // 닉네임과 ncurses 창을 전역에 저장
    strncpy(g_nickname, nickname, sizeof(g_nickname)-1);
    g_win_chat = win_chat;
    g_win_input = win_input;

    // 1) DNS 해석
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        pthread_mutex_lock(&clients_lock);
        wprintw(win_chat, "[Error] Failed to resolve host: %s\n", host);
        wrefresh(win_chat);
        pthread_mutex_unlock(&clients_lock);
        return;
    }

    // 2) 소켓 생성 및 연결
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return;
    }
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        pthread_mutex_lock(&clients_lock);
        wprintw(win_chat, "[Error] Could not connect to %s:%d\n", host, port);
        wrefresh(win_chat);
        pthread_mutex_unlock(&clients_lock);
        close(sockfd);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);

    // 3) 서버로부터 오는 메시지 수신용 스레드 생성
    int *sock_ptr = malloc(sizeof(int));
    if (!sock_ptr) {
        perror("malloc");
        close(sockfd);
        return;
    }
    *sock_ptr = sockfd;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, chat_recv_handler_internal, sock_ptr);

    // 4) 사용자 입력 → 서버로 전송 루프
    while (1) {
        // (1) 입력창에 프롬프트 표시
        werase(g_win_input);
        box(g_win_input, 0, 0);
        mvwprintw(g_win_input, 1, 2, "%s> ", g_nickname);
        wrefresh(g_win_input);

        // (2) 블로킹 모드로 입력 대기
        wtimeout(g_win_input, -1);
        echo();
        char inputbuf[BUF_SIZE];
        wgetnstr(g_win_input, inputbuf, sizeof(inputbuf)-1);
        noecho();

        if (strcmp(inputbuf, "/exit") == 0) {
            break;
        }
        if (strlen(inputbuf) == 0) {
            continue;
        }

        // (3) 현재 시각 문자열 생성
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", tm_info);

        // (4) 전송 포맷: [닉네임][HH:MM:SS] 메시지\n
        char sendbuf[BUF_SIZE];
        snprintf(sendbuf, sizeof(sendbuf), "[%s][%s] %s\n", g_nickname, timestr, inputbuf);
        send(sockfd, sendbuf, strlen(sendbuf), 0);

        // (5) 자기 메시지를 미리 출력
        pthread_mutex_lock(&clients_lock);
        wprintw(g_win_chat, "%s", sendbuf);
        wrefresh(g_win_chat);
        pthread_mutex_unlock(&clients_lock);
    }

    close(sockfd);
    // recv 스레드는 소켓 닫힘으로 인해 종료됨
}

static void *chat_recv_handler_internal(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf)-1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        pthread_mutex_lock(&clients_lock);
        wprintw(g_win_chat, "%s", buf);
        wrefresh(g_win_chat);
        pthread_mutex_unlock(&clients_lock);
    }
    return NULL;
}
