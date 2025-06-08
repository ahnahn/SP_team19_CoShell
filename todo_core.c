//=========================================
//          ToDo Core 로직 모듈
//  - add/del/done/undo/edit 등 기본 기능
//     - user/team 파일 로딩 및 저장
//=========================================

#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

char current_todo_file[256] = USER_TODO_FILE;
char *todos[MAX_TODO];
int todo_count = 0;
pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;

/*==============================*/
/*     ToDo 모드 설정 함수      */
/*==============================*/
void set_todo_mode(int is_team_mode) {
    strcpy(current_todo_file, is_team_mode ? TEAM_TODO_FILE : USER_TODO_FILE);
}

/*==============================*/
/*   ToDo 목록 파일 로딩 함수   */
/*==============================*/
void load_todo() {
    FILE *fp = fopen(current_todo_file, "r");
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
/*    ToDo 목록 파일 저장 함수  */
/*==============================*/
void save_todo_to_file() {
    FILE *fp = fopen(current_todo_file, "w");
    if (!fp) return;
    for (int i = 0; i < todo_count; i++) {
        fprintf(fp, "%s\n", todos[i]);
    }
    fclose(fp);
}

/*==============================*/
/*        ToDo 추가 함수        */
/*==============================*/
void add_todo(const char *item) {
    pthread_mutex_lock(&todo_lock);
    if (todo_count >= MAX_TODO) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    char formatted[256];
    snprintf(formatted, sizeof(formatted), "%s [ ]", item);
    todos[todo_count++] = strdup(formatted);

    save_todo_to_file();
    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*      ToDo 완료 처리 함수     */
/*==============================*/
void done_todo(int index) {
    pthread_mutex_lock(&todo_lock);
    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }
    int i = index - 1;
    size_t len = strlen(todos[i]);
    if (len >= 4 && strcmp(todos[i] + len - 4, " [ ]") == 0) {
        todos[i][len - 2] = 'x';
    }
    save_todo_to_file();
    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*     ToDo 완료 취소 함수      */
/*==============================*/
void undo_todo(int index) {
    pthread_mutex_lock(&todo_lock);
    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }
    int i = index - 1;
    size_t len = strlen(todos[i]);
    if (len >= 4 && strcmp(todos[i] + len - 4, " [x]") == 0) {
        todos[i][len - 2] = ' ';
    }
    save_todo_to_file();
    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*        ToDo 삭제 함수        */
/*==============================*/
void del_todo(int index) {
    pthread_mutex_lock(&todo_lock);
    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }
    int i = index - 1;
    free(todos[i]);
    for (int j = i; j < todo_count - 1; j++) {
        todos[j] = todos[j + 1];
    }
    todo_count--;
    save_todo_to_file();
    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*        ToDo 수정 함수        */
/*==============================*/
void edit_todo(int index, const char *new_item) {
    pthread_mutex_lock(&todo_lock);
    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }
    int i = index - 1;
    int is_done = 0;
    size_t len = strlen(todos[i]);
    if (len >= 4 && strcmp(todos[i] + len - 4, " [x]") == 0) {
        is_done = 1;
    }
    free(todos[i]);
    char formatted[256];
    snprintf(formatted, sizeof(formatted), "%s [%c]", new_item, is_done ? 'x' : ' ');
    todos[i] = strdup(formatted);
    save_todo_to_file();
    pthread_mutex_unlock(&todo_lock);
}

