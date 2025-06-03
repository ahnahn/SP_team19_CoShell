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
static int sockfd;                  // 채팅 소켓 디스크립터
static WINDOW *win_chat_border;     // 테두리가 그려질 채팅창 윈도우
static WINDOW *win_chat_inner;      // 테두리 안쪽(1,1 부터 쓸) 실제 메시지 출력용
static WINDOW *g_win_input;         // 채팅 입력창 윈도우
static char g_nickname[64];         // 사용자 닉네임

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

    // SO_REUSEADDR 옵션 설정
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
            // 최대 클라이언트 수 초과 시 단순히 연결 닫기
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
/*
 * host    : 서버 호스트명 또는 IP
 * port    : 서버 포트
 * nickname: 사용자가 입력한 닉네임
 * win_chat_border: ncurses에서 만든 '테두리 그릴 채팅창' 윈도우
 * win_input       : ncurses에서 만든 '입력창' 윈도우
 */
void chat_client(const char *host,
                 int port,
                 const char *nickname,
                 WINDOW *win_chat_border,
                 WINDOW *win_input)
{
    // 1) 전역 변수 초기화
    strncpy(g_nickname, nickname, sizeof(g_nickname) - 1);
    win_chat_border = win_chat_border;  // 테두리 그릴 윈도우
    g_win_input     = win_input;        // 입력창 윈도우

    // 2) DNS 해석
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        // 호스트 해석 실패: 테두리 안쪽에 에러 출력
        pthread_mutex_lock(&clients_lock);
        box(win_chat_border, 0, 0);
        mvwprintw(win_chat_border, 1, 2, "[Error] Failed to resolve host: %s", host);
        wrefresh(win_chat_border);
        pthread_mutex_unlock(&clients_lock);
        return;
    }

    // 3) 소켓 생성 및 연결
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return;
    }
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        pthread_mutex_lock(&clients_lock);
        box(win_chat_border, 0, 0);
        mvwprintw(win_chat_border, 1, 2, "[Error] Could not connect to %s:%d", host, port);
        wrefresh(win_chat_border);
        pthread_mutex_unlock(&clients_lock);
        close(sockfd);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);

    // 4) 서버 메시지 수신 전담 스레드 생성
    int *sock_ptr = malloc(sizeof(int));
    if (!sock_ptr) {
        perror("malloc");
        close(sockfd);
        return;
    }
    *sock_ptr = sockfd;
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, chat_recv_handler_internal, sock_ptr);

    // 5) 채팅창 설정: 테두리 그리기 → 내부 윈도우 생성 → 스크롤 허용
    box(win_chat_border, 0, 0);
    wrefresh(win_chat_border);

    // win_chat_border 의 크기를 구해서, 내부에 한 칸 여백을 둔 서브윈도우 생성
    int h_border, w_border;
    getmaxyx(win_chat_border, h_border, w_border);
    // 높이: 테두리 위/아래 각각 1줄 제외 → h_border - 2
    // 너비: 테두리 좌/우 각각 1열 제외 → w_border - 2
    win_chat_inner = derwin(win_chat_border,
                            h_border - 2,
                            w_border - 2,
                            1,   // y-offset: 테두리 아래쪽 1
                            1);  // x-offset: 테두리 왼쪽 1

    scrollok(win_chat_inner, TRUE);
    wrefresh(win_chat_inner);

    // 6) 사용자 입력 루프 시작
    while (1) {
        // (A) 입력창 초기화 + 프롬프트
        werase(g_win_input);
        box(g_win_input, 0, 0);
        mvwprintw(g_win_input, 1, 2, "%s> ", g_nickname);
        wrefresh(g_win_input);

        // (B) 블로킹 모드로 전환 → 사용자로부터 메시지 입력
        wtimeout(g_win_input, -1);
        echo();
        char inputbuf[BUF_SIZE];
        wgetnstr(g_win_input, inputbuf, sizeof(inputbuf) - 1);
        noecho();

        // (C) 종료 명령어 처리
        if (strcmp(inputbuf, "/exit") == 0 || strcmp(inputbuf, "/quit") == 0) {
            break;
        }
        if (strlen(inputbuf) == 0) {
            continue;
        }

        // (D) 현재 시각 문자열 생성
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", tm_info);

        // (E) 전송할 메시지 포맷: "[닉네임][HH:MM:SS] 실제메시지\n"
        char sendbuf[BUF_SIZE];
        int written = snprintf(sendbuf, sizeof(sendbuf),
                               "[%s][%s] ", g_nickname, timestr);
        strncat(sendbuf, inputbuf, sizeof(sendbuf) - written - 2);
        strcat(sendbuf, "\n");

        // (F) 서버로 전송
        send(sockfd, sendbuf, strlen(sendbuf), 0);

        // (G) 자기 메시지를 테두리 안쪽 윈도우(win_chat_inner)에 출력
        pthread_mutex_lock(&clients_lock);
        // 1) 테두리(win_chat_border) 재그리기 (테두리 문자가 사라지지 않게)
        box(win_chat_border, 0, 0);
        // 2) 내부 윈도우에 메시지 출력 (스마트하게 스크롤 처리)
        wprintw(win_chat_inner, "%s", sendbuf);
        wrefresh(win_chat_inner);
        // 3) 테두리도 다시 갱신
        wrefresh(win_chat_border);
        pthread_mutex_unlock(&clients_lock);
    }

    // 7) 소켓 닫기
    close(sockfd);

    // 8) 채팅 종료 후 테두리와 내부 윈도우 초기화
    pthread_mutex_lock(&clients_lock);
    werase(win_chat_border);
    box(win_chat_border, 0, 0);
    mvwprintw(win_chat_border, 1, 2, "Chat ended. Press any key to continue.");
    wrefresh(win_chat_border);
    // 내부 윈도우도 지워줘야 메모리 누수가 없음
    if (win_chat_inner) {
        delwin(win_chat_inner);
        win_chat_inner = NULL;
    }
    pthread_mutex_unlock(&clients_lock);
}

/*==============================*/
/*  서버로부터 수신 핸들러       */
/*==============================*/
static void *chat_recv_handler_internal(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        pthread_mutex_lock(&clients_lock);
        // 1) 테두리(win_chat_border) 재그리기
        box(win_chat_border, 0, 0);
        // 2) 내부(win_chat_inner)에 메시지 출력
        wprintw(win_chat_inner, "%s", buf);
        wrefresh(win_chat_inner);
        // 3) 테두리 갱신
        wrefresh(win_chat_border);
        pthread_mutex_unlock(&clients_lock);
    }
    return NULL;
}
