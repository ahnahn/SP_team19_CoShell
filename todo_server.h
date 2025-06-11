// todo_server.h
#ifndef TODO_SERVER_H
#define TODO_SERVER_H

#define TODO_SERVER_PORT 56789   // To-Do 서버 포트

// coshell 내부에서 백그라운드로 To-Do 서버를 띄우는 함수
void start_todo_server(int port);

#endif // TODO_SERVER_H

