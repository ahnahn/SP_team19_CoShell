/*
 * chat.c
 *  - Chat 서버/클라이언트 구현 (최종판)
 *  - SIGWINCH 처리 및 non-blocking 입력 루프 적용
 *  - /add, /del, /done, /undo 명령을 로컬 ToDo로 즉시 처리
 */

#define _POSIX_C_SOURCE 200809L

#include "chat.h"
#include "todo.h"            // add_todo(), del_todo(), done_todo(), undo_todo(), load_todo(), draw_todo()
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

#define MAX_HISTORY 1000

 // Chat 서버 관련 externs
extern void    create_windows(int in_lobby);
extern WINDOW* win_custom;
extern WINDOW* win_input;
extern WINDOW* win_todo;

// 전역 변수 (chat.h 에서 extern)
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
int             client_socks[MAX_CLIENTS];
int             client_count = 0;

// 채팅 히스토리
static char* chat_history[MAX_HISTORY];
static int   history_count = 0;
static void add_history(const char* msg) {
    if (history_count >= MAX_HISTORY) {
        free(chat_history[0]);
        memmove(chat_history, chat_history + 1,
            sizeof(char*) * (MAX_HISTORY - 1));
        history_count--;
    }
    chat_history[history_count++] = strdup(msg);
}

// 내부 전역
static int      sockfd;
static WINDOW* win_chat_border;
static WINDOW* win_chat_inner;
static WINDOW* g_win_input;
static char     g_nickname[64];

// SIGWINCH 처리
volatile sig_atomic_t win_resized = 0;
static void handle_winch(int sig) { (void)sig; win_resized = 1; }

// 전방 선언
static void* chat_server_handler(void* arg);
static void* chat_recv_handler_internal(void* arg);

/*==============================*/
/*      Chat 서버 구현         */
/*==============================*/
void chat_server(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, MAX_CLIENTS);
    printf("Chat server listening on port %d...\n", port);

    while (1) {
        int client = accept(server_sock, NULL, NULL);
        if (client < 0) continue;
        pthread_mutex_lock(&clients_lock);
        if (client_count < MAX_CLIENTS) {
            client_socks[client_count++] = client;
            int* pclient = malloc(sizeof(int));
            *pclient = client;
            pthread_t tid;
            pthread_create(&tid, NULL, chat_server_handler, pclient);
            pthread_detach(tid);
        }
        else {
            close(client);
        }
        pthread_mutex_unlock(&clients_lock);
    }
}

/*==============================*/
/*  Chat 서버 쓰레드 핸들러    */
/*==============================*/
static void* chat_server_handler(void* arg) {
    int sock = *(int*)arg; free(arg);
    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < client_count; i++) {
            if (client_socks[i] != sock)
                send(client_socks[i], buf, len, 0);
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
void chat_client(const char* host,
    int port,
    const char* nickname,
    WINDOW* client_border,
    WINDOW* client_input)
{
    // 1) 초기화
    strncpy(g_nickname, nickname, sizeof(g_nickname) - 1);
    win_chat_border = client_border;
    g_win_input = client_input;

    // 2) SIGWINCH 핸들러
    struct sigaction sa = { .sa_handler = handle_winch };
    sigaction(SIGWINCH, &sa, NULL);

    // 3) Chat 서버 연결
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, * res;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return;
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) { freeaddrinfo(res); return; }
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(sockfd);
        return;
    }
    freeaddrinfo(res);

    // 4) 수신 스레드
    int* psock = malloc(sizeof(int));
    *psock = sockfd;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, chat_recv_handler_internal, psock);

    // 5) 윈도우 설정
    box(win_chat_border, 0, 0); wrefresh(win_chat_border);
    int h, w; getmaxyx(win_chat_border, h, w);
    win_chat_inner = derwin(win_chat_border, h - 2, w - 2, 1, 1);
    scrollok(win_chat_inner, TRUE);
    wrefresh(win_chat_inner);

    // 5.5) 입력창 키패드 모드 켜기
    keypad(g_win_input, TRUE);

    // 6) 입력 루프
    char inputbuf[BUF_SIZE] = { 0 };
    int  len = 0;
    while (1) {
        // (A) 리사이즈
        if (win_resized) {
            win_resized = 0;
            endwin(); refresh(); clear();
            create_windows(0);
            win_chat_border = win_custom;
            g_win_input = win_input;
            getmaxyx(win_chat_border, h, w);
            if (win_chat_inner) delwin(win_chat_inner);
            win_chat_inner = derwin(win_chat_border, h - 2, w - 2, 1, 1);
            scrollok(win_chat_inner, TRUE);
            box(win_chat_border, 0, 0); wrefresh(win_chat_border);
            wrefresh(win_chat_inner);
            box(g_win_input, 0, 0); wrefresh(g_win_input);
            for (int i = 0;i < history_count;i++)
                wprintw(win_chat_inner, "%s", chat_history[i]);
            wrefresh(win_chat_inner);
            load_todo();
            draw_todo(win_todo);
            continue;
        }

        // (B) 입력창 그리기
        werase(g_win_input);
        box(g_win_input, 0, 0);
        mvwprintw(g_win_input, 1, 2, "%s> %s", g_nickname, inputbuf);
        wrefresh(g_win_input);

        // (C) non-blocking 입력
        wtimeout(g_win_input, 200);
        int ch = wgetch(g_win_input);
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) { win_resized = 1; continue; }

        // (D) Enter 처리 ('\n' 또는 KEY_ENTER)
        if (ch == '\n' || ch == KEY_ENTER) {
            inputbuf[len] = '\0';

            // 종료 명령
            if (!strcmp(inputbuf, "/exit") || !strcmp(inputbuf, "/quit")) {
                break;
            }

            // To-Do 명령
            if (len > 1 && inputbuf[0] == '/') {
                char* cmd = inputbuf + 1;
                if (strncmp(cmd, "add ", 4) == 0) {
                    add_todo(cmd + 4);
                }
                else if (strncmp(cmd, "del ", 4) == 0) {
                    del_todo(atoi(cmd + 4));
                }
                else if (strncmp(cmd, "done ", 5) == 0) {
                    done_todo(atoi(cmd + 5));
                }
                else if (strncmp(cmd, "undo ", 5) == 0) {
                    undo_todo(atoi(cmd + 5));
                }
                else if (strncmp(cmd, "edit ", 5) == 0) {
                    int idx;
                    char newitem[BUF_SIZE];
                    if (sscanf(cmd + 5, "%d %[^\n]", &idx, newitem) == 2) {
                        edit_todo(idx, newitem);
                    }
                }



                draw_todo(win_todo);
            }
            // 일반 채팅
            else if (len > 0) {
                time_t now = time(NULL);
                char ts[16];
                strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
                char sb[BUF_SIZE];
                int wlen = snprintf(sb, sizeof(sb),
                    "[%s][%s] ", g_nickname, ts);
                strncat(sb, inputbuf, sizeof(sb) - wlen - 2);
                strcat(sb, "\n");
                send(sockfd, sb, strlen(sb), 0);
                pthread_mutex_lock(&clients_lock);
                wprintw(win_chat_inner, "%s", sb);
                wrefresh(win_chat_inner);
                pthread_mutex_unlock(&clients_lock);
                add_history(sb);
            }

            // 초기화
            len = 0;
            inputbuf[0] = '\0';
            continue;
        }

        // 백스페이스
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (len > 0) inputbuf[--len] = '\0';
        }
        // 일반 문자
        else if (isprint(ch) && len < BUF_SIZE - 1) {
            inputbuf[len++] = (char)ch;
            inputbuf[len] = '\0';
        }
    }

    // 7) 종료 처리
    close(sockfd);
    pthread_mutex_lock(&clients_lock);
    werase(win_chat_border);
    box(win_chat_border, 0, 0);
    mvwprintw(win_chat_border, 1, 2, "Chat ended. Press any key to continue.");
    wrefresh(win_chat_border);
    if (win_chat_inner) delwin(win_chat_inner);
    pthread_mutex_unlock(&clients_lock);
}

/*==============================*/
/*  Chat 수신 스레드 핸들러     */
/*==============================*/
static void* chat_recv_handler_internal(void* arg) {
    int sock = *(int*)arg; free(arg);
    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';
        pthread_mutex_lock(&clients_lock);
        wprintw(win_chat_inner, "%s", buf);
        wrefresh(win_chat_inner);
        pthread_mutex_unlock(&clients_lock);
        add_history(buf);
    }
    return NULL;
}
