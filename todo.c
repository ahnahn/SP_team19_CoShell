#define _POSIX_C_SOURCE 200809L

#include "todo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* 전역 ToDo 파일 경로 설정 */
const char *USER_TODO_FILE = "user_todo.txt";
const char *TEAM_TODO_FILE = "team_todo.txt";

/* 현재 선택된 ToDo 파일 (동적으로 변경) */
char current_todo_file[128] = "user_todo.txt";  // default : user

/* Time 갱신 */
#include <time.h>
extern WINDOW* win_time;
void update_time(WINDOW* w);

/* Resize Flag */
#include <signal.h>
extern volatile sig_atomic_t resized;

/* ToDo 전역변수 정의 */
pthread_mutex_t todo_lock = PTHREAD_MUTEX_INITIALIZER;
char *todos[MAX_TODO];
int todo_count = 0;

/*==============================*/
/*   ToDo 리스트 로드 함수      */
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
/*   ToDo 추가(메모리+파일)     */
/*==============================*/
void add_todo(const char *item) {
    pthread_mutex_lock(&todo_lock);
    if (todo_count >= MAX_TODO) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    char formatted[256];
    snprintf(formatted, sizeof(formatted), "%s [ ]", item);  // 항목 + 완료 상태 추가
    
    char *new_item = strdup(formatted);
    if (!new_item) {
        pthread_mutex_unlock(&todo_lock);
        return;  // strdup 실패: 메모리 부족 등
    }

    todos[todo_count++] = strdup(formatted);

    FILE *fp = fopen(current_todo_file, "a");
    if (fp) {
        fprintf(fp, "%s\n", formatted); // 파일에도 [ ] 포함된 버전 저장
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*    ToDo 완료 처리 함수       */
/*==============================*/
void done_todo(int index) {
    pthread_mutex_lock(&todo_lock);

    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    int i = index - 1;
    char *item = todos[i];
    size_t len = strlen(item);

    if (len >= 4 && strcmp(item + len - 4, " [ ]") == 0) {
        item[len - 2] = 'x'; // " [ ]" → " [x]"
    }

    // 파일 갱신
    FILE *fp = fopen(current_todo_file, "w");
    if (fp) {
        for (int k = 0; k < todo_count; k++)
            fprintf(fp, "%s\n", todos[k]);
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*   ToDo 완료 취소 처리 함수   */
/*==============================*/
void undo_todo(int index) {
    pthread_mutex_lock(&todo_lock);

    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    int i = index - 1;
    char *item = todos[i];
    size_t len = strlen(item);

    if (len >= 4 && strcmp(item + len - 4, " [x]") == 0) {
        item[len - 2] = ' '; // " [x]" → " [ ]"
    }

    // 파일 갱신
    FILE *fp = fopen(current_todo_file, "w");
    if (fp) {
        for (int k = 0; k < todo_count; k++)
            fprintf(fp, "%s\n", todos[k]);
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*   ToDo 삭제 (메모리 + 파일)  */
/*==============================*/
void del_todo(int index) {
    pthread_mutex_lock(&todo_lock);

    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    int i = index - 1;
    free(todos[i]);  // 메모리 해제

    // 앞으로 한 칸씩 당김
    for (int j = i; j < todo_count - 1; j++) {
        todos[j] = todos[j + 1];
    }

    todo_count--;

    // 파일 갱신
    FILE *fp = fopen(current_todo_file, "w");
    if (fp) {
        for (int k = 0; k < todo_count; k++) {
            fprintf(fp, "%s\n", todos[k]);
        }
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*   ToDo 수정 (메모리 + 파일)  */
/*==============================*/
void edit_todo(int index, const char *new_item) {
    pthread_mutex_lock(&todo_lock);

    if (index < 1 || index > todo_count) {
        pthread_mutex_unlock(&todo_lock);
        return;
    }

    int i = index - 1;
    
    // 기존 상태 확인 (완료 여부)
    int is_done = 0;
    size_t len = strlen(todos[i]);
    if (len >= 4 && strcmp(todos[i] + len - 4, " [x]") == 0) {
        is_done = 1;
    }
    
    free(todos[i]);
    
    // 새로운 항목 구성: 뒤에 [ ] 또는 [x] 붙이기
    char formatted[256];
    snprintf(formatted, sizeof(formatted), "%s [%c]", new_item, is_done ? 'x' : ' ');

    char *new_str = strdup(formatted);
    if (!new_str) {
        pthread_mutex_unlock(&todo_lock);
        return;  // strdup 실패 시 바로 종료
    }

    todos[i] = strdup(formatted);

    // 파일 갱신
    FILE *fp = fopen(current_todo_file, "w");
    if (fp) {
        for (int k = 0; k < todo_count; k++) {
            fprintf(fp, "%s\n", todos[k]);
        }
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*       Help Window 출력       */
/*==============================*/
void draw_custom_help(WINDOW *custom) {
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "To-Do [%s] Mode",
        strcmp(current_todo_file, TEAM_TODO_FILE) == 0 ? "Team" : "User");
    mvwprintw(custom, 2, 2, "team/user → switch mode");
    mvwprintw(custom, 3, 2, "add  <item>");
    mvwprintw(custom, 4, 2, "done <num>");
    mvwprintw(custom, 5, 2, "undo <num>");
    mvwprintw(custom, 6, 2, "del  <num>");
    mvwprintw(custom, 7, 2, "edit <num> <new item>");
    mvwprintw(custom, 8, 2, "q = quit");
    wrefresh(custom);
}

/*==============================*/
/*    오류 메시지 출력 함수     */
/*==============================*/
void show_error(WINDOW *custom, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "[Error] ");
    vw_printw(custom, fmt, args);  // 포맷 적용된 메시지 출력
    va_end(args);

    wrefresh(custom);
    napms(2000);  // 2초 대기
    draw_custom_help(custom);  // 도움말 다시 출력
}

/*==============================*/
/*        ToDo 모드 진입        */
/*==============================*/
void todo_enter(WINDOW *input, WINDOW *todo, WINDOW *custom) {	
    char buf[256] = {0};
    int len = 0;
    int input_y = 1, input_x = 10;

    strcpy(current_todo_file, USER_TODO_FILE);
    load_todo();
    draw_todo(todo);
    draw_custom_help(custom);

    // 시간 갱신용 타이머
    time_t last_time = 0;
    wtimeout(input, 200);
    
    // 입력 루프
    while (1) {
	// 터미널 리사이즈 감지 시 빠져나감
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
	    
	    if (strcmp(buf, "team") == 0 || strcmp(buf, "user") == 0) {
		strcpy(current_todo_file, strcmp(buf, "team") == 0 ? TEAM_TODO_FILE : USER_TODO_FILE);
                load_todo();
                draw_todo(todo);
		
		// 모드 전환 안내 출력
                werase(custom);
                box(custom, 0, 0);
                mvwprintw(custom, 1, 2, "Switched to [%s] mode", buf);
                wrefresh(custom);
		napms(2000);
                draw_custom_help(custom);
		
		// 다시 도움말 출력
		draw_custom_help(custom);
	    }
	    else if (strncmp(buf, "add ", 4) == 0 && len > 4) {
                add_todo(buf + 4);
                draw_todo(todo);
            }
	    else if (strncmp(buf, "done ", 5) == 0) {
    		int idx = atoi(buf + 5);
		if (idx <= 0 || idx > todo_count) {
    		    show_error(custom, "Invalid index: %d", idx);
		} else {
    		    done_todo(idx);
    		    draw_todo(todo);
		}
	    }
	    else if (strncmp(buf, "undo ", 5) == 0) {
	        int idx = atoi(buf + 5);
		if (idx <= 0 || idx > todo_count) {
    		    show_error(custom, "Invalid index: %d", idx);
		} else {
    		    undo_todo(idx);
    		    draw_todo(todo);
		}
	    }
	    else if (strncmp(buf, "del ", 4) == 0) {
                int idx = atoi(buf + 4);
		if (idx <= 0 || idx > todo_count) {
    		    show_error(custom, "Invalid index: %d", idx);
   		} else {
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

    		    if (strlen(new_text) == 0) {
        	        show_error(custom, "New item text is empty.");
    		    } else if (idx <= 0 || idx > todo_count) {
        		show_error(custom, "Invalid index: %d", idx);
    		    } else {
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
		
		// 2초 기다린 후 ToDo 도움말 다시 표시
                napms(2000);
                draw_custom_help(custom);
            }
            // 입력창 리셋
            len = 0;
            memset(buf, 0, sizeof(buf));
            werase(input);
            box(input, 0, 0);
            mvwprintw(input, 1, 2, "Command: ");
            wrefresh(input);
        }
        else if (ch == KEY_BACKSPACE || ch == 127) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                mvwprintw(input, input_y, input_x + len, " ");
                wmove(input, input_y, input_x + len);
                wrefresh(input);
            }
        }
        else if (ch >= 32 && ch <= 126 && len < sizeof(buf) - 1) {
            buf[len++] = (char)ch;
            mvwprintw(input, input_y, input_x + len - 1, "%c", ch);
            wmove(input, input_y, input_x + len);
            wrefresh(input);
        }
    }

    // 입력창 종료 시 리셋
    werase(input);
    box(input, 0, 0);
    mvwprintw(input, 1, 2, "Command: ");
    wrefresh(input);
}
