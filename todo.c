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
    todos[todo_count++] = strdup(item);

    FILE *fp = fopen(TODO_FILE, "a");
    if (fp) {
        fprintf(fp, "%s\n", item);
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
    FILE *fp = fopen(TODO_FILE, "w");
    if (fp) {
        for (int k = 0; k < todo_count; k++) {
            fprintf(fp, "%s\n", todos[k]);
        }
        fclose(fp);
    }

    pthread_mutex_unlock(&todo_lock);
}

/*==============================*/
/*        ToDo 모드 진입        */
/*==============================*/
void todo_enter(WINDOW *input, WINDOW *todo, WINDOW *custom) {
    char buf[256] = {0};
    int len = 0;
    int input_y = 1, input_x = 10;

    load_todo();
    draw_todo(todo);

    werase(custom);
    box(custom, 0, 0);
    //mvwprintw(custom, 1, 2, "To-Do Mode: add <item> del <num> q = quit");
    mvwprintw(custom, 1, 2, "To-Do Mode:");
    mvwprintw(custom, 2, 2, "add <item>");
    mvwprintw(custom, 3, 2, "del <num>");
    mvwprintw(custom, 4, 2, "q = quit");
    wrefresh(custom);

    // 입력 루프
    while (1) {
        wmove(input, input_y, input_x + len);
        wrefresh(input);
        wtimeout(input, 200);

        int ch = wgetch(input);
        if (ch == ERR) continue;

        if (ch == '\n') {
            buf[len] = '\0';

            if (strcmp(buf, "q") == 0) break;

            if (strncmp(buf, "add ", 4) == 0 && len > 4) {
                add_todo(buf + 4);
                draw_todo(todo);
            } 
	    else if (strncmp(buf, "del ", 4) == 0) {
                int idx = atoi(buf + 4);
                if (idx <= 0) {
                    mvwprintw(custom, 6, 2, "Invalid number: %s", buf + 4);
                } else {
                    del_todo(idx);
                    draw_todo(todo);
                }
            } 
	    else {
                werase(custom);
                box(custom, 0, 0);
                mvwprintw(custom, 1, 2, "Unknown command: %s", buf);
                wrefresh(custom);
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
