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

/* Todo 목록에서 index번째 항목을 완료 처리 [ ] → [x] */
void done_todo(int index);

/* Todo 목록에서 index번째 항목을 완료 취소 [x] → [ ] */
void undo_todo(int index);

/* ToDo 목록에서 index번째 항목을 삭제하고 파일을 갱신 */
void del_todo(int index);

/* Todo 목록에서 index번째 항목을 new_item으로 수정 */
void edit_todo(int index, const char *new_item);

/* Help Window 출력 */ 
void draw_custom_help(WINDOW *custom);

/* user/team 모드 설정 */
void choose_todo_mode(WINDOW *input, WINDOW *custom);

/* 모듈화된 ToDo 모드 진입 */
void todo_enter(WINDOW *input, WINDOW *todo, WINDOW *custom);

#endif // TODO_H
