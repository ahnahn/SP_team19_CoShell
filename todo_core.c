//=========================================
//           ToDo Core 로직 모듈
//     - 사용자 입력 처리 및 화면 출력
//      - user/team 파일 로딩 및 저장
//  - add/del/done/undo/edit 등 기본 기능
//=========================================

#define _POSIX_C_SOURCE 200809L

#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ncurses.h>
#include <pthread.h>

/* 전역 변수 */
char current_todo_file[256] = USER_TODO_FILE;
char *todos[MAX_TODO];
int todo_count = 0;
pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;

/* 팀 ToDo 서버와의 연결 및 명령 송수신을 위한 todo_clinet.c  함수 선언 */
extern  int connect_todo_server(const char *host, int port);
extern void disconnect_todo_server();
extern  int send_todo_command(const char *cmd, char *buf, size_t size);

/*==============================*/
/*    오류 메시지 출력 함수     */
/*==============================*/
void show_error(WINDOW *custom, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "[Error] ");
    vw_printw(custom, fmt, args);
    va_end(args);

    wrefresh(custom);
    napms(2000);
    draw_custom_help(custom);
}

/*==============================*/
/*       Help Window 출력       */
/*==============================*/
void draw_custom_help(WINDOW *custom) {
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "To-Do [%s] Mode", strcmp(current_todo_file, TEAM_TODO_FILE) == 0 ? "Team" : "User");
    mvwprintw(custom, 2, 2, "Enter %s to switch mode", strcmp(current_todo_file, TEAM_TODO_FILE) == 0 ? "user" : "team" );
    mvwprintw(custom, 3, 2, "add  <item>");
    mvwprintw(custom, 4, 2, "done <num>");
    mvwprintw(custom, 5, 2, "undo <num>");
    mvwprintw(custom, 6, 2, "del  <num>");
    mvwprintw(custom, 7, 2, "edit <num> <new item>");
    mvwprintw(custom, 8, 2, "q = quit");
    wrefresh(custom);
}

/*==============================*/
/*  ncurses에 ToDo 목록 그리기  */
/*==============================*/
void draw_todo(WINDOW *win_todo) {
    pthread_mutex_lock(&todo_lock);
    werase(win_todo);
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 0, 2, " ToDo List ");
    for (int i = 0; i < todo_count; i++) {
        mvwprintw(win_todo, i + 1, 2, "%d. %s", i + 1, todos[i]);
    }
    pthread_mutex_unlock(&todo_lock);
    wrefresh(win_todo);
}

/*==============================*/
/*     ToDo 모드 설정 함수      */
/*==============================*/
void set_todo_mode(int is_team_mode) {
    strcpy(current_todo_file, is_team_mode ? TEAM_TODO_FILE : USER_TODO_FILE);
}

/*==============================*/
/*     team 모드 전환 함수      */
/*==============================*/
int switch_to_team_mode(WINDOW *custom, WINDOW *todo) {
    char response[2048];

    // 1. 기존 서버 연결 종료 → 새 연결 시도
    disconnect_todo_server();
    if (connect_todo_server("localhost", 56789) != 0) {
        show_error(custom, "Failed to connect to team server");
        return -1;
    }

    // 2. ToDo 파일 모드를 팀 모드로 설정
    strcpy(current_todo_file, TEAM_TODO_FILE);

    // 3. 서버에 list 요청 → 응답 파싱
    if (send_todo_command("list", response, sizeof(response)) != 0) {
        show_error(custom, "Failed to fetch team list");
        strcpy(current_todo_file, USER_TODO_FILE);  // 실패 시 복구
        return -1;
    }

    // 4. 파싱된 목록 그리기
    parse_todo_list(response);
    draw_todo(todo);

    // 5. 사용자에게 모드 전환 안내
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Switched to [team] mode");
    wrefresh(custom);
    napms(2000);

    draw_custom_help(custom);
    return 0;
}

/*==============================*/
/*      user 모드 전환 함수     */
/*==============================*/
void switch_to_user_mode(WINDOW *custom, WINDOW *todo) {
    // 1. 팀 서버 연결 종료
    disconnect_todo_server();

    // 2. ToDo 파일 모드를 사용자 모드로 설정
    strcpy(current_todo_file, USER_TODO_FILE);

    // 3. 로컬 ToDo 파일 로딩 및 화면 출력
    load_todo();
    draw_todo(todo);

    // 4. 사용자에게 모드 전환 안내 출력
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Switched to [user] mode");
    wrefresh(custom);
    napms(2000);

    draw_custom_help(custom);
}

/*==============================*/
/*   ToDo 목록 파일 로딩 함수   */
/*==============================*/
void load_todo() {
    FILE *fp = fopen(current_todo_file, "r");
    if (!fp) return;

    pthread_mutex_lock(&todo_lock);
    
    // 1. 기존 데이터 해제
    for (int i = 0; i < MAX_TODO; i++) {
        if (todos[i]) {
            free(todos[i]);
            todos[i] = NULL;
        }
    }

    // 2. 새로운 데이터 로딩
    char line[256];
    while (fgets(line, sizeof(line), fp) && todo_count < MAX_TODO) {
        line[strcspn(line, "\r\n")] = '\0';
        todos[todo_count++] = strdup(line);
    }

    pthread_mutex_unlock(&todo_lock);
    fclose(fp);
}

/*==============================*/
/*   ToDo 목록 파일 저장 함수   */
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
    
    todos[todo_count] = NULL;
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

