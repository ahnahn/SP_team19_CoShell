#ifndef TODO_CLIENT_H
#define TODO_CLIENT_H

#include <stddef.h>

#define BUF_SIZE 1024
#define TODO_SERVER_HOST "127.0.0.1"
#define TODO_SERVER_PORT 12345

int  connect_todo_server(const char *host, int port);
int  send_todo_command(const char *cmd, char *resp, size_t resp_sz);
void parse_todo_list(const char *list_str);
void draw_todo(WINDOW *win);

#endif // TODO_CLIENT_H

