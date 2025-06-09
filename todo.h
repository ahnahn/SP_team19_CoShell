#ifndef TODO_H
#define TODO_H

#include <pthread.h>
#include <ncurses.h>

#define MAX_TODO     100

//========================
//     파일 경로 상수
//========================
#define USER_TODO_FILE  "todo_user.txt"
#define TEAM_TODO_FILE  "todo_team.txt"

//========================
//     서버 기본 정보
//========================
#define TEAM_IP      "127.0.0.1"
#define TEAM_PORT    56789

//========================
//    전역 ToDo 데이터
//========================
extern pthread_mutex_t todo_lock;
extern char current_todo_file[256];
extern char *todos[MAX_TODO];
extern int todo_count;

//========================
//   UI 관련 함수 선언
//========================
void draw_todo(WINDOW *win_todo);
void draw_custom_help(WINDOW *custom);
void show_error(WINDOW *custom, const char *fmt, ...);
int  switch_to_team_mode(WINDOW *custom, WINDOW *todo);
void switch_to_user_mode(WINDOW *custom, WINDOW *todo);

//========================
//   Core 기능 함수 선언
//========================
void load_todo();
void add_todo(const char *item);
void done_todo(int index);
void undo_todo(int index);
void del_todo(int index);
void edit_todo(int index, const char *new_item);
void save_todo_to_file();
void set_todo_mode(int is_team_mode);

//========================
//  서버 통신 함수 선언
//========================
int connect_todo_server(const char *ip, int port);
void disconnect_todo_server();
int send_todo_command(const char *cmd, char *response, size_t size);
void parse_todo_list(const char *response);

#endif // TODO_H

