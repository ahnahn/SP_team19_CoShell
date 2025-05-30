#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ToDo 전역변수 정의 */
pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;
char *todos[MAX_TODO];
int todo_count = 0;

/*==============================*/
/*   ToDo 리스트 로드 함수      */
/*==============================*/
void load_todo() {
    FILE *fp = fopen(TODO_FILE, "r");
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
/*   ncurses에 ToDo 목록 그리기   */
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
/*   ToDo 추가(메모리+파일)     */
/*==============================*/
void add_todo(const char *item) {
    pthread_mutex_lock(&todo_lock);
    if (todo_count >= MAX_TODO) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }
    todos[todo_count++] = strdup(item);

    FILE *fp = fopen(TODO_FILE, "a");
    if (fp) {
        fprintf(fp, "%s\n", item);
        fclose(fp);
    }
    pthread_mutex_unlock(&todo_lock);
}
