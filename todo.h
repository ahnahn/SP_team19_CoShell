#ifndef TODO_H
#define TODO_H

#include <pthread.h>
#include <ncurses.h>   // draw_todo에서 WINDOW*를 사용하므로 포함

#define MAX_TODO    100
#define TODO_FILE   "tasks_personal.txt"

/* ToDo 전역 데이터 */
extern pthread_mutex_t todo_lock;
extern char *todos[MAX_TODO];
extern int todo_count;

/* 파일에서 ToDo 목록을 읽어서 todos[], todo_count에 채워넣는다 */
void load_todo();

/* ncurses 창(win_todo)에 todos[] 내용을 출력 */
void draw_todo(WINDOW *win_todo);

/* 메모리 및 파일에 새로운 ToDo(item)를 추가 */
void add_todo(const char *item);

#endif // TODO_H
