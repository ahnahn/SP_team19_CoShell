/*
 * chat.c
 *  - Chat 서버/클라이언트 구현 (최종판)
 *  - SIGWINCH 처리 및 non-blocking 입력 루프 적용
 */

#define _POSIX_C_SOURCE 200809L

#include "chat.h"
#include "todo.h"

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
#include <signal.h>
#include <ctype.h>

/* coshell.c 에서 제공하는 윈도우 재생성 함수 및 전역 윈도우 */
extern void   create_windows(int in_lobby);
extern WINDOW *win_custom;
extern WINDOW *win_input;
extern WINDOW   *win_todo;
/*==============================*/
/*   전역 변수 정의 (chat.h에서 extern)  */
/*==============================*/
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
int client_socks[MAX_CLIENTS];
int client_count = 0;

/*==============================*/
/*   채팅 히스토리 버퍼           */
/*==============================*/
#define MAX_HISTORY 1000
static char *chat_history[MAX_HISTORY];
static int history_count = 0;

static void add_history(const char *msg) {
    if (history_count >= MAX_HISTORY) {
        free(chat_history[0]);
        memmove(chat_history, chat_history + 1, sizeof(char*) * (MAX_HISTORY - 1));
        history_count--;
    }
    chat_history[history_count++] = strdup(msg);
}

/*==============================*/
/*   내부 전역 변수              */
/*==============================*/
static int sockfd;                  // 채팅 소켓 디스크립터
static WINDOW *win_chat_border;     // 테두리가 그려질 채팅창 윈도우
static WINDOW *win_chat_inner;      // 테두리 안쪽 실제 메시지 출력용
static WINDOW *g_win_input;         // 채팅 입력창 윈도우
static char g_nickname[64];         // 사용자 닉네임

/*==============================*/
/*   SIGWINCH 처리용 전역 플래그  */
/*==============================*/
volatile sig_atomic_t win_resized = 0;
static void handle_winch(int sig) { (void)sig; win_resized = 1; }

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
                 WINDOW *client_border,
                 WINDOW *client_input)
{
    // 1) 전역 변수 초기화
    strncpy(g_nickname, nickname, sizeof(g_nickname)-1);
    win_chat_border = client_border;
    g_win_input     = client_input;

    // 2) SIGWINCH 핸들러 등록
    struct sigaction sa = {0};
    sa.sa_handler = handle_winch;
    sigaction(SIGWINCH, &sa, NULL);

    // 3) DNS 해석 및 연결
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        box(win_chat_border, 0, 0);
        mvwprintw(win_chat_border,1,2,"[Error] Failed to resolve host: %s", host);
        wrefresh(win_chat_border);
        napms(2000);
        werase(win_chat_border); wrefresh(win_chat_border);
        werase(win_input);      wrefresh(win_input);
        return;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) { perror("socket"); freeaddrinfo(res); return; }
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        werase(win_chat_border);
        box(win_chat_border,0,0);
        mvwprintw(win_chat_border,1,2,"[Error] Could not connect to %s:%d", host, port);
        wrefresh(win_chat_border);
        close(sockfd);
        freeaddrinfo(res);
        napms(2000);
        werase(win_chat_border); wrefresh(win_chat_border);
        werase(win_input);      wrefresh(win_input);
        return;
    }
    freeaddrinfo(res);

    // 4) 서버 수신 스레드
    int *sock_ptr = malloc(sizeof(int)); *sock_ptr = sockfd;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, chat_recv_handler_internal, sock_ptr);

    // 5) 초기 채팅창 설정
    box(win_chat_border,0,0); wrefresh(win_chat_border);
    int h_border, w_border;
    getmaxyx(win_chat_border,h_border,w_border);
    win_chat_inner = derwin(win_chat_border, h_border-2, w_border-2, 1, 1);
    scrollok(win_chat_inner, TRUE);
    wrefresh(win_chat_inner);

    // 6) 입력 루프
    char inputbuf[BUF_SIZE] = {0};
    int len = 0;
    while (1) {
        // (A) 리사이즈 처리
        if (win_resized) {
            win_resized = 0;
            endwin(); refresh(); clear();
            create_windows(0);
            win_chat_border = win_custom;
            g_win_input     = win_input;
            int h,w; getmaxyx(win_chat_border,h,w);
            if (win_chat_inner) delwin(win_chat_inner);
            win_chat_inner = derwin(win_chat_border, h-2, w-2, 1, 1);
            scrollok(win_chat_inner, TRUE);
            box(win_chat_border,0,0); wrefresh(win_chat_border);
            wrefresh(win_chat_inner);
            box(g_win_input,0,0);     wrefresh(g_win_input);
            // 히스토리 다시 그리기
            for (int i = 0; i < history_count; i++) {
                wprintw(win_chat_inner, "%s", chat_history[i]);
            }
            wrefresh(win_chat_inner);
            load_todo();
            draw_todo(win_todo);
            continue;
        }

        // (B) 입력창 그리기
        werase(g_win_input);
        box(g_win_input,0,0);
        mvwprintw(g_win_input,1,2,"%s> %s", g_nickname, inputbuf);
        wrefresh(g_win_input);

        // (C) non-blocking 입력
        wtimeout(g_win_input,200);
        int ch = wgetch(g_win_input);
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) { win_resized = 1; continue; }
        if (ch == '\n') {
            inputbuf[len] = '\0';
            if (strcmp(inputbuf,"/exit")==0 || strcmp(inputbuf,"/quit")==0) break;
            if (len>0) {
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                char timestr[16]; strftime(timestr,sizeof(timestr),"%H:%M:%S",tm_info);
                char sendbuf[BUF_SIZE];
                int w = snprintf(sendbuf,sizeof(sendbuf),"[%s][%s] ",g_nickname,timestr);
                strncat(sendbuf,inputbuf,sizeof(sendbuf)-w-2);
                strcat(sendbuf,"\n");
                send(sockfd, sendbuf, strlen(sendbuf),0);
                pthread_mutex_lock(&clients_lock);
                wprintw(win_chat_inner,"%s",sendbuf);
                wrefresh(win_chat_inner);
                wrefresh(win_chat_border);
                pthread_mutex_unlock(&clients_lock);
                add_history(sendbuf);
            }
            len=0; inputbuf[0]='\0';
            continue;
        }
        else if (ch==KEY_BACKSPACE||ch==127) {
            if (len>0) inputbuf[--len]='\0';
        }
        else if (isprint(ch) && len<BUF_SIZE-1) {
            inputbuf[len++]=ch;
            inputbuf[len]='\0';
        }
    }

    // 7) 종료 처리
    close(sockfd);
    pthread_mutex_lock(&clients_lock);
    werase(win_chat_border);
    box(win_chat_border,0,0);
    mvwprintw(win_chat_border,1,2,"Chat ended. Press any key to continue.");
    wrefresh(win_chat_border);
    if (win_chat_inner) delwin(win_chat_inner);
    pthread_mutex_unlock(&clients_lock);
}

static void *chat_recv_handler_internal(void *arg) {
    int sock = *(int*)arg;
    free(arg);
    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock,buf,sizeof(buf)-1,0);
        if (len<=0) break;
        buf[len]='\0';
        pthread_mutex_lock(&clients_lock);
        wprintw(win_chat_inner,"%s",buf);
        wrefresh(win_chat_inner);
        wrefresh(win_chat_border);
        pthread_mutex_unlock(&clients_lock);
        add_history(buf);
    }
    return NULL;
}
