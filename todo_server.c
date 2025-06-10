/*========================================
 *          ToDo 서버 모듈
 *    - 클라이언트 명령 수신 및 처리
 *    - 파일 기반 공유 리스트 저장
 *    - pthread로 coshell 내에서 백그라운드 실행 가능
 *========================================*/

#define _POSIX_C_SOURCE 200809L

#include "todo_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#ifndef MAX_TODO
#define MAX_TODO        100
#endif
#ifndef TEAM_TODO_FILE
#define TEAM_TODO_FILE  "team_todo.txt"
#endif
#define BUF_SIZE        1024
#define MAX_CONN        10

static pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;
static char* todos[MAX_TODO];
static int   todo_count = 0;

/*==============================*/
/*    파일 기반 ToDo 로드      */
/*==============================*/
static void load_todos(void) {
    FILE *fp = fopen(TEAM_TODO_FILE, "r");
    if (!fp) return;

    pthread_mutex_lock(&todo_lock);
    todo_count = 0;
    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), fp) && todo_count < MAX_TODO) {
        line[strcspn(line, "\r\n")] = '\0';
        todos[todo_count++] = strdup(line);
    }
    pthread_mutex_unlock(&todo_lock);
    fclose(fp);
}

/*==============================*/
/*    파일 기반 ToDo 저장      */
/*==============================*/
static void save_todos(void) {
    FILE *fp = fopen(TEAM_TODO_FILE, "w");
    if (!fp) return;

    pthread_mutex_lock(&todo_lock);
    for (int i = 0; i < todo_count; i++) {
        fprintf(fp, "%s\n", todos[i]);
    }
    pthread_mutex_unlock(&todo_lock);
    fclose(fp);
}

/*==============================*/
/*   클라이언트 명령 처리 함수  */
/*==============================*/
static void handle_command(int client_fd) {
    dprintf(client_fd, "DBG_SERVER: got connection\n");
	char buf[BUF_SIZE];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';

	fprintf(stderr, "[DBG_SERVER] cmd='%s'\n", buf);

    char response[BUF_SIZE] = {0};
    char cmd[16]          = {0};
    char arg1[BUF_SIZE]   = {0};
    int  idx;

    pthread_mutex_lock(&todo_lock);

    // 명령어 파싱: 첫 단어=cmd, 나머지=arg1
    if (sscanf(buf, "%15s %1023[^\"]", cmd, arg1) < 1) {
        snprintf(response, sizeof(response), "ERROR: Invalid input\n");
    }
    else if (strcmp(cmd, "add") == 0) {
        if (arg1[0] == '\0') {
            snprintf(response, sizeof(response), "ERROR: Missing item text\n");
        } else if (todo_count >= MAX_TODO) {
            snprintf(response, sizeof(response), "ERROR: Max limit reached\n");
        } else {
            char tmp[BUF_SIZE];
            snprintf(tmp, sizeof(tmp), "%s [ ]", arg1);
            todos[todo_count++] = strdup(tmp);
            save_todos();
            snprintf(response, sizeof(response), "OK\n");
        }
    }
    else if (strcmp(cmd, "del") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        } else {
            free(todos[idx]);
            memmove(&todos[idx], &todos[idx+1],
                    (todo_count - idx - 1) * sizeof(char*));
            todo_count--;
            save_todos();
            snprintf(response, sizeof(response), "OK\n");
        }
    }
    else if (strcmp(cmd, "done") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        } else {
            size_t len = strlen(todos[idx]);
            if (len >= 4 && strcmp(todos[idx] + len - 4, " [ ]") == 0) {
                todos[idx][len - 2] = 'x';
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            } else {
                snprintf(response, sizeof(response), "ERROR: Already done\n");
            }
        }
    }
    else if (strcmp(cmd, "undo") == 0) {
        idx = atoi(arg1) - 1;
        if (idx < 0 || idx >= todo_count) {
            snprintf(response, sizeof(response), "ERROR: Invalid index\n");
        } else {
            size_t len = strlen(todos[idx]);
            if (len >= 4 && strcmp(todos[idx] + len - 4, " [x]") == 0) {
                todos[idx][len - 2] = ' ';
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            } else {
                snprintf(response, sizeof(response), "ERROR: Not marked as done\n");
            }
        }
    }
    else if (strcmp(cmd, "edit") == 0) {
        char arg2[BUF_SIZE];
        if (sscanf(arg1, "%d %1023[^\"]", &idx, arg2) < 2) {
            snprintf(response, sizeof(response),
                     "ERROR: Usage: edit <num> <new item>\n");
        } else {
            idx--;
            if (idx < 0 || idx >= todo_count) {
                snprintf(response, sizeof(response), "ERROR: Invalid index\n");
            } else {
                int done = (strstr(todos[idx], "[x]") != NULL);
                free(todos[idx]);
                char tmp[BUF_SIZE];
                snprintf(tmp, sizeof(tmp), "%s [%c]", arg2, done ? 'x':' ');
                todos[idx] = strdup(tmp);
                save_todos();
                snprintf(response, sizeof(response), "OK\n");
            }
        }
    }
    else if (strcmp(cmd, "list") == 0) {
        if (todo_count == 0) {
            snprintf(response, sizeof(response), "No todos.\n");
        } else {
            for (int i = 0; i < todo_count; i++) {
                char line[BUF_SIZE];
                snprintf(line, sizeof(line), "%d. %s\n", i+1, todos[i]);
                size_t rem = sizeof(response) - strlen(response) - 1;
                strncat(response, line, rem);
            }
        }
    }
    else {
        snprintf(response, sizeof(response), "ERROR: Unknown command\n");
    }

    pthread_mutex_unlock(&todo_lock);

    (void)write(client_fd, response, strlen(response));
    close(client_fd);
}

/*==============================*/
/*   ToDo 서버 스레드 진입부    */
/*==============================*/
static void* todo_server_thread(void* arg) {
    int port = *(int*)arg;
    free(arg);

    load_todos();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return NULL;
    }
    listen(server_fd, MAX_CONN);

    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL,
            (void*(*)(void*))handle_command,
            client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return NULL;
}

/*==============================*/
/*   ToDo 서버 스레드 시작 API   */
/*==============================*/
void start_todo_server(int port) {
    int *p = malloc(sizeof(int));
    *p = port;
    pthread_t tid;
    pthread_create(&tid, NULL, todo_server_thread, p);
    pthread_detach(tid);
}

#ifdef TODO_SERVER_MAIN
int main(int argc, char* argv[]) {
    int port = (argc == 2) ? atoi(argv[1]) : DEFAULT_PORT;
    printf("ToDo 서버 시작 (포트 %d)...\n", port);
    start_todo_server(port);
    pause();  // 메인 스레드는 대기
    return 0;
}
#endif

