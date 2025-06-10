/*========================================
 *          ToDo 클라이언트 모듈
 *     - 서버 연결 및 명령 전송 처리
 *========================================*/

#define _POSIX_C_SOURCE 200809L

#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>




int send_todo_command(const char* cmd, char* response, size_t size) {
    // 1) 새 소켓 열기
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(response, size, "ERROR: socket failed: %s", strerror(errno));
        return -1;
    }

    // 2) 서버에 연결
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TEAM_PORT),   // todo.h 에 정의된 포트
    };
    inet_pton(AF_INET, TEAM_IP, &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(response, size, "ERROR: connect failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    // 3) 명령 전송 (cmd + '\n')
    if (write(sock, cmd, strlen(cmd)) < 0 ||
        write(sock, "\n", 1) < 0) {
        snprintf(response, size, "ERROR: write failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    // 4) 응답 읽기
    ssize_t len = read(sock, response, size - 1);
    if (len <= 0) {
        snprintf(response, size, "ERROR: no response");
        close(sock);
        return -1;
    }
    response[len] = '\0';

    // 5) 닫기
    close(sock);
    return 0;
}


/*==============================*/
/*  서버 응답 → todo 배열 파싱  */
/*==============================*/
void parse_todo_list(const char* response) {
    pthread_mutex_lock(&todo_lock);

    // 이전에 저장된 ToDo 항목 해제
    for (int i = 0; i < MAX_TODO; i++) {
        free(todos[i]);
        todos[i] = NULL;
    }
    todo_count = 0;

    if (response == NULL || *response == '\0') {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    char* copy = strdup(response);
    if (copy) {
        char* line = strtok(copy, "\n");
        while (line && todo_count < MAX_TODO) {
            todos[todo_count++] = strdup(line);
            line = strtok(NULL, "\n");
        }
        free(copy);
    }

    pthread_mutex_unlock(&todo_lock);
}

