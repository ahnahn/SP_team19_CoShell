#ifndef TODO_H
#define TODO_H

#include <pthread.h>
#include <ncurses.h>

#define MAX_TODO     100

//========================
//     ���� ��� ���
//========================
#define USER_TODO_FILE  "todo_user.txt"
#define TEAM_TODO_FILE  "todo_team.txt"

//========================
//     ���� �⺻ ����
//========================
#define TEAM_IP      "127.0.0.1"
#define TEAM_PORT    56789

//========================
//    ���� ToDo ������
//========================
extern pthread_mutex_t todo_lock;
extern char current_todo_file[256];
extern char* todos[MAX_TODO];
extern int todo_count;

//========================
//   UI ���� �Լ� ����
//========================
void draw_todo(WINDOW* win_todo);
void draw_custom_help(WINDOW* custom);
void show_error(WINDOW* custom, const char* fmt, ...);
int  switch_to_team_mode(WINDOW* custom, WINDOW* todo);
void switch_to_user_mode(WINDOW* custom, WINDOW* todo);

//========================
//   Core ��� �Լ� ����
//========================
void load_todo();
void add_todo(const char* item);
void done_todo(int index);
void undo_todo(int index);
void del_todo(int index);
void edit_todo(int index, const char* new_item);
void save_todo_to_file();
void set_todo_mode(int is_team_mode);

//========================
//  ���� ��� �Լ� ����
//========================
int connect_todo_server(const char* ip, int port);
void disconnect_todo_server();
int send_todo_command(const char* cmd, char* response, size_t size);
void parse_todo_list(const char* response);

#endif // TODO_H
