//========================================
//              ToDo UI 모듈
//        - ncurses 기반 UI 구성
//    - 사용자 입력 처리 및 화면 출력
//    - todo_core 및 client 연동 포함
//========================================

#define _POSIX_C_SOURCE 200809L

#include "todo.h"
#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>

/* Time 갱신*/
extern WINDOW *win_time;
extern void update_time(WINDOW *);

/* Resize Flag */
extern volatile sig_atomic_t resized;

/* 팀 ToDo 서버와의 연결 및 명령 송수신을 위한 todo_clinet.c  함수 선언 */
extern  int connect_todo_server(const char *host, int port); // 지정된 호스트와 포트로 서버 연결
extern void disconnect_todo_server(); // 현재 서버 연결 종료
extern  int send_todo_command(const char *cmd, char *buf, size_t size); // 서버에 명령어 전송 후 응답을 buf에 저장


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
/*        ToDo 모드 진입        */
/*==============================*/
void todo_enter(WINDOW *input, WINDOW *todo, WINDOW *custom) {
    // UTF-8 로케일 활성화
    setlocale(LC_ALL, "");
	
    char buf[256] = {0};
    int len = 0;
    int input_y = 1, input_x = 10;
    char response[2048];

    strcpy(current_todo_file, USER_TODO_FILE);
    load_todo();
    draw_todo(todo);
    draw_custom_help(custom);
    
    // 시간 갱신용 타이머
    time_t last_time = 0;
    wtimeout(input, 200);

    // 입력 루프
    while (1) {
	if (resized) {
	    break;
	}
	
        wmove(input, input_y, input_x + len);
        wrefresh(input);

	// 1초마다 시계 갱신
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        int ch = wgetch(input);
        if (ch == ERR) continue;

        if (ch == '\n') {
            buf[len] = '\0';

            if (strcmp(buf, "q") == 0) break;

            if (strcmp(buf, "team") == 0) {
		disconnect_todo_server();
                if (connect_todo_server("localhost", 56789) == 0) {		    
    		    // 현재 todo 파일을 팀 todo 파일로 전환
    		    strcpy(current_todo_file, TEAM_TODO_FILE);

    		    // 서버에 'list' 명령어 전송하여 todo 목록 요청
    		    send_todo_command("list", response, sizeof(response));

    		    // 응답으로 받은 todo 목록 문자열을 파싱하여 내부 리스트에 저장
    		    parse_todo_list(response);

    		    // 파싱된 내용을 todo 창에 출력
    		    draw_todo(todo);

    		    // 사용자에게 모드 전환을 알리는 메시지를 custom 창에 출력
    		    werase(custom);
    		    box(custom, 0, 0);
    		    mvwprintw(custom, 1, 2, "Switched to [team] mode");
    		    wrefresh(custom);

    		    // 잠시 대기하여 메시지를 사용자에게 보여줌 (2초)
    		    napms(2000);
                } else {
                    show_error(custom, "Failed to connect to team server");
                    strcpy(current_todo_file, USER_TODO_FILE);
                }
                draw_custom_help(custom);
            }
            else if (strcmp(buf, "user") == 0) {
                disconnect_todo_server();
                strcpy(current_todo_file, USER_TODO_FILE);
                load_todo();
                draw_todo(todo);
                
		// 모드 전환 안내 출력
		werase(custom);
                box(custom, 0, 0);
                mvwprintw(custom, 1, 2, "Switched to [user] mode");
                wrefresh(custom);
                napms(2000);
                
		// 다시 도움말 출력
		draw_custom_help(custom);
            }
            else {
                if (strcmp(current_todo_file, TEAM_TODO_FILE) == 0) {
                    if (send_todo_command(buf, response, sizeof(response)) != 0) {
			show_error(custom, "서버 통신 실패");
        		continue;  // 입력 루프 계속
		    }
                        
		    // 응답이 ERROR로 시작하면 에러 출력
        	    if (strncmp(response, "ERROR", 5) == 0) {
            		show_error(custom, "%s", response);
        	    } else {
            		// 정상 응답인 경우에도 사용자에게 일단 메시지 보여줌
            		werase(custom);
            		box(custom, 0, 0);
            		mvwprintw(custom, 1, 2, "%s", response);
            		wrefresh(custom);
            		napms(800);
			    
			// 그리고 list 명령어로 다시 목록 받아서 그려줌
            		if (send_todo_command("list", response, sizeof(response)) == 0) {
                	    parse_todo_list(response);
                	    draw_todo(todo);
            		} else {
                	    show_error(custom, "Failed to update todo list");
            		}
        	    }	    
		} else {
                    if (strncmp(buf, "add ", 4) == 0 && len > 4) {
                        add_todo(buf + 4);
                        draw_todo(todo);
                    }
                    else if (strncmp(buf, "done ", 5) == 0) {
                        int idx = atoi(buf + 5);
                        if (idx <= 0 || idx > todo_count)
                            show_error(custom, "Invalid index: %d", idx);
                        else {
                            done_todo(idx);
                            draw_todo(todo);
                        }
                    }
                    else if (strncmp(buf, "undo ", 5) == 0) {
                        int idx = atoi(buf + 5);
                        if (idx <= 0 || idx > todo_count)
                            show_error(custom, "Invalid index: %d", idx);
                        else {
                            undo_todo(idx);
                            draw_todo(todo);
                        }
                    }
                    else if (strncmp(buf, "del ", 4) == 0) {
                        int idx = atoi(buf + 4);
                        if (idx <= 0 || idx > todo_count)
                            show_error(custom, "Invalid index: %d", idx);
                        else {
                            del_todo(idx);
                            draw_todo(todo);
                        }
                    }
                    else if (strncmp(buf, "edit ", 5) == 0) {
                        char *space = strchr(buf + 5, ' ');
                        if (space) {
                            *space = '\0';
                            int idx = atoi(buf + 5);
                            char *new_text = space + 1;
                            if (strlen(new_text) == 0)
                                show_error(custom, "New item text is empty.");
                            else if (idx <= 0 || idx > todo_count)
                                show_error(custom, "Invalid index: %d", idx);
                            else {
                                edit_todo(idx, new_text);
                                draw_todo(todo);
                            }
                        } else {
                            show_error(custom, "Usage: edit <num> <new text>");
                        }
                    }
                    else {
                        werase(custom);
                        box(custom, 0, 0);
                        mvwprintw(custom, 1, 2, "Unknown command: %s", buf);
                        wrefresh(custom);
                        
			napms(2000);
                        draw_custom_help(custom);
                    }
                }
            }
	    // 입력창 리셋
            len = 0;
            memset(buf, 0, sizeof(buf));
            werase(input);
            box(input, 0, 0);
            mvwprintw(input, 1, 2, "Command: ");
            wrefresh(input);
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                mvwprintw(input, input_y, input_x + len, " ");
                wmove(input, input_y, input_x + len);
                wrefresh(input);
            }
        } else if (ch >= 32 && ch <= 126 && len < sizeof(buf) - 1) {
            buf[len++] = (char)ch;
            mvwprintw(input, input_y, input_x + len - 1, "%c", ch);
            wmove(input, input_y, input_x + len);
            wrefresh(input);
        }
    }
    // 종료 시 입력창 리셋
    werase(input);
    box(input, 0, 0);
    mvwprintw(input, 1, 2, "Command: ");
    wrefresh(input);
}
