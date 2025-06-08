//========================================
//          ToDo 클라이언트 모듈
//     - 서버 연결 및 명령 전송 처리
//========================================

#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

static int server_fd = -1;

/*==============================*/
/*      서버 연결 시도 함수     */
/*==============================*/
int connect_todo_server(const char *ip, int port) {
    if (server_fd != -1) return 0; // 이미 연결되어 있음

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        server_fd = -1;
        return -1;
    }
    return 0;
}

/*==============================*/
/*      서버 연결 해제 함수     */
/*==============================*/
void disconnect_todo_server() {
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

/*==============================*/
/*    명령 전송 및 응답 수신    */
/*==============================*/
int send_todo_command(const char *cmd, char *response, size_t size) {
    if (server_fd == -1) {
        fprintf(stderr, "[send] 서버 연결 안 됨\n");
        return -1;
    }

    fprintf(stderr, "[send] → %s\n", cmd);

    if (write(server_fd, cmd, strlen(cmd)) < 0) {
        perror("[send] write 실패");
        return -1;
    }
    if (write(server_fd, "\n", 1) < 0) {
        perror("[send] write 줄바꿈 실패");
        return -1;
    }

    int len = read(server_fd, response, size - 1);
    if (len <= 0) {
        perror("[send] read 실패 or 응답 없음");
	response[0] = '\0';
        return -1;
    }

    response[len] = '\0';
    fprintf(stderr, "[recv] ← %s\n", response);
    return 0;
}

/*==============================*/
/*  서버 응답 → todo 배열 파싱  */
/*==============================*/
void parse_todo_list(const char *response) {
    pthread_mutex_lock(&todo_lock);

    // 이전에 저장된 ToDo 항목 해제
    for (int i = 0; i < MAX_TODO; i++) {
        if (todos[i]) {
            free(todos[i]);
            todos[i] = NULL;
        }
    }

    todo_count = 0;

    if (!response || strlen(response) == 0) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    char *copy = strdup(response);
    if (!copy) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    char *line = strtok(copy, "\n");
    while (line && todo_count < MAX_TODO) {
        todos[todo_count++] = strdup(line);
        line = strtok(NULL, "\n");
    }

    free(copy);  // 복사본 해제
    pthread_mutex_unlock(&todo_lock);
}

