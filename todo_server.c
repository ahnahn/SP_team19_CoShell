//========================================
//          ToDo 서버 프로그램
//    - 클라이언트 명령 수신 및 처리
//     - 파일 기반 공유 리스트 저장
//========================================

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 56789
#define MAX_CONN 10
#define BUF_SIZE 1024
#define TODO_FILE "team_todo.txt"
#define MAX_TODO 100

char* todos[MAX_TODO];
int todo_count = 0;
pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;

/*==============================*/
/*    ToDo 파일에서 불러오기    */
/*==============================*/
void load_todos() {
    FILE* fp = fopen(TODO_FILE, "r");
    if (!fp) return;

    pthread_mutex_lock(&todo_lock);
    todo_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && todo_count < MAX_TODO) {
        line[strcspn(line, "\n")] = '\0';
        todos[todo_count++] = strdup(line);
    }
    pthread_mutex_unlock(&todo_lock);
    fclose(fp);
}

/*==============================*/
/*     ToDo 파일로 저장하기     */
/*==============================*/
void save_todos() {
    FILE* fp = fopen(TODO_FILE, "w");
    if (!fp) return;

    for (int i = 0; i < todo_count; i++) {
        fprintf(fp, "%s\n", todos[i]);
    }
    fclose(fp);
}

/*==============================*/
/*   클라이언트 명령 처리 함수  */
/*==============================*/
void handle_command(int client_fd, const char* cmdline) {
    char response[BUF_SIZE] = { 0 };
    char cmd[16], arg1[256], arg2[512];
    int idx;

    pthread_mutex_lock(&todo_lock);

    if (sscanf(cmdline, "%15s %255[^\n]", cmd, arg1) < 1) {
        snprintf(response, sizeof(response), "ERROR: Invalid input\n");
    }
    else if (strcmp(cmd, "add") == 0) {
        if (strlen(arg1) == 0) {
            snprintf(response, sizeof(response), "ERROR: Missing item text\n");
        }
        else if (todo_count >= MAX_TODO) {
            snprintf(response, sizeof(response), "ERROR: Max limit reached\n");
        }
        else {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s [ ]", arg1);
            todos[todo_count++] = strdup(buf);
            save_todos();
            snprintf(response, sizeof(response), "OK\n");

            printf("[server] add 처리 완료, 응답 준비됨: %s", response);
        }
    }
    else if (strcmp(cmd, "del") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        }
        else {
            free(todos[idx]);
            for (int i = idx; i < todo_count - 1; i++)
                todos[i] = todos[i + 1];
            todo_count--;
            save_todos();
            snprintf(response, sizeof(response), "OK\n");
        }
    }
    else if (strcmp(cmd, "done") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        }
        else {
            int len = strlen(todos[idx]);
            if (len >= 4 && strcmp(todos[idx] + len - 4, " [ ]") == 0) {
                todos[idx][len - 2] = 'x';
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            }
            else {
                snprintf(response, sizeof(response), "ERROR: Already done\n");
            }
        }
    }
    else if (strcmp(cmd, "undo") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        }
        else {
            int len = strlen(todos[idx]);
            if (len >= 4 && strcmp(todos[idx] + len - 4, " [x]") == 0) {
                todos[idx][len - 2] = ' ';
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            }
            else {
                snprintf(response, sizeof(response), "ERROR: Not marked as done\n");
            }
        }
    }
    else if (strcmp(cmd, "edit") == 0) {
        if (sscanf(arg1, "%d %255[^\n]", &idx, arg2) < 2) {
            snprintf(response, sizeof(response), "ERROR: Usage: edit <num> <new item>\n");
        }
        else {
            idx -= 1;
            if (idx < 0 || idx >= todo_count) {
                snprintf(response, sizeof(response), "ERROR: Invalid index\n");
            }
            else {
                int is_done = 0;
                int len = strlen(todos[idx]);
                if (len >= 4 && strcmp(todos[idx] + len - 4, " [x]") == 0)
                    is_done = 1;
                free(todos[idx]);
                char buf[1024];
                snprintf(buf, sizeof(buf), "%s [%c]", arg2, is_done ? 'x' : ' ');
                todos[idx] = strdup(buf);
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            }
        }
    }
    else if (strcmp(cmd, "list") == 0) {
        if (todo_count == 0) {
            snprintf(response, sizeof(response), "No todos.\n");
        }
        else {
            for (int i = 0; i < todo_count; i++) {
                char line[300];
                snprintf(line, sizeof(line), "%d. %s\n", i + 1, todos[i]);
                strncat(response, line, sizeof(response) - strlen(response) - 1);
            }
        }
    }
    else {
        snprintf(response, sizeof(response), "ERROR: Unknown command\n");
    }

    pthread_mutex_unlock(&todo_lock);

    // 디버그 로그 추가
    printf("[server] 응답 전송 시도: %s", response);
    ssize_t n = write(client_fd, response, strlen(response));
    if (n < 0) {
        perror("[server] write 실패");
    }
    else {
        printf("[server] 응답 전송 완료 (%zd bytes)\n", n);
    }

}

/*==============================*/
/*    클라이언트 스레드 함수    */
/*==============================*/
void* client_thread(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char buf[BUF_SIZE] = { 0 };
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        handle_command(client_fd, buf);
    }
    close(client_fd);
    return NULL;
}

/*==============================*/
/*     메인 함수 (서버 실행)    */
/*==============================*/
int main(int argc, char* argv[]) {
    int port = PORT;
    if (argc == 2)
        port = atoi(argv[1]);

    load_todos();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, MAX_CONN);
    printf("ToDo 서버 시작 (포트 %d)...\n", port);

    while (1) {
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
