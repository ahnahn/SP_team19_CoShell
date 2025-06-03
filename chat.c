#define _POSIX_C_SOURCE 200809L

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
static int sockfd;
static WINDOW *g_win_chat;
static WINDOW *g_win_input;
static char g_nickname[64];

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

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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

        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < client_count; i++) {
            if (client_socks[i] != sock) {
                send(client_socks[i], buf, len, 0);
            }
        }
        pthread_mutex_unlock(&clients_lock);
    }

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
void chat_client(const char *host, int port, const char *nickname, WINDOW *win_chat, WINDOW *win_input) {
    strncpy(g_nickname, nickname, sizeof(g_nickname)-1);
    g_win_chat = win_chat;
    g_win_input = win_input;

    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
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

    int *sock_ptr = malloc(sizeof(int));
    if (!sock_ptr) {
        perror("malloc");
        close(sockfd);
        return;
    }
    *sock_ptr = sockfd;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, chat_recv_handler_internal, sock_ptr);

    while (1) {
        werase(g_win_input);
        box(g_win_input, 0, 0);
        mvwprintw(g_win_input, 1, 2, "%s> ", g_nickname);
        wrefresh(g_win_input);

        wtimeout(g_win_input, -1);
        echo();
        char inputbuf[BUF_SIZE];
        wgetnstr(g_win_input, inputbuf, sizeof(inputbuf) - 1);
        noecho();

        if (strcmp(inputbuf, "/exit") == 0) break;
        if (strlen(inputbuf) == 0) continue;

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", tm_info);

        char sendbuf[BUF_SIZE];
        int written = snprintf(sendbuf, sizeof(sendbuf), "[%s][%s] ", g_nickname, timestr);
        strncat(sendbuf, inputbuf, sizeof(sendbuf) - written - 2);
        strcat(sendbuf, "\n");

        send(sockfd, sendbuf, strlen(sendbuf), 0);

        pthread_mutex_lock(&clients_lock);
        wprintw(g_win_chat, "%s", sendbuf);
        wrefresh(g_win_chat);
        pthread_mutex_unlock(&clients_lock);
    }

    close(sockfd);
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
