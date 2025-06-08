// coshell.c — 리사이즈 대응 개선 버전
#define _POSIX_C_SOURCE 200809L

#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "todo.h"
#include "chat.h"
#include "qr.h"

typedef enum { MODE_MAIN, MODE_TODO, MODE_CHAT, MODE_QR } Mode;
static Mode current_mode = MODE_MAIN;

// 모드별 상태 구조체
typedef struct {
    WINDOW *list_win, *status_win;
    int selected;
} TodoUI;
static TodoUI todo_ui = {0};

typedef struct {
    WINDOW *chat_win, *input_win;
    char partial[512];
} ChatUI;
static ChatUI chat_ui = {0};

typedef struct {
    WINDOW *qr_win;
    char filepath[512];
} QrUI;
static QrUI qr_ui = {0};

// UI 생성/복원 선언
static void create_windows(void);
static void restore_current_ui(void);
static void restore_todo_ui(void);
static void restore_chat_ui(void);
static void restore_qr_ui(void);

// 리사이즈 핸들러
static void sig_winch(int signo) {
    endwin();
    refresh();
    clear();
    create_windows();
    restore_current_ui();
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    signal(SIGWINCH, sig_winch);

    // 초기 UI 구성
    create_windows();
    refresh();

    int ch;
    while ((ch = getch()) != KEY_F(1)) {
        if (ch == KEY_RESIZE) {
            sig_winch(SIGWINCH);
            continue;
        }

        switch (current_mode) {
            case MODE_MAIN:
                if (ch == '1') current_mode = MODE_TODO;
                else if (ch == '2') current_mode = MODE_CHAT;
                else if (ch == '3') current_mode = MODE_QR;
                break;

            case MODE_TODO:
                if (ch == 'q') current_mode = MODE_MAIN;
                else handle_todo_input(ch, todo_ui.list_win, todo_ui.status_win, &todo_ui.selected);
                break;

            case MODE_CHAT:
                if (ch == '/')
                    ; // handle quit in chat logic
                else handle_chat_input(ch, chat_ui.chat_win, chat_ui.input_win, chat_ui.partial);
                break;

            case MODE_QR:
                if (ch == 'q') current_mode = MODE_MAIN;
                break;
        }

        restore_current_ui();
    }

    endwin();
    return 0;
}

// 공통 윈도우 생성 함수
static void create_windows(void) {
    int h = LINES, w = COLS;
    werase(stdscr);

    switch (current_mode) {
        case MODE_MAIN:
            // 메인 메뉴 표시
            mvprintw(1, 2, "CoShell Main Menu: 1=ToDo 2=Chat 3=QR, F1=Exit");
            break;

        case MODE_TODO:
            if (todo_ui.list_win) delwin(todo_ui.list_win);
            if (todo_ui.status_win) delwin(todo_ui.status_win);
            todo_ui.list_win = newwin(h-2, w, 0, 0);
            todo_ui.status_win = newwin(2, w, h-2, 0);
            box(todo_ui.status_win, 0, 0);
            break;

        case MODE_CHAT:
            if (chat_ui.chat_win) delwin(chat_ui.chat_win);
            if (chat_ui.input_win) delwin(chat_ui.input_win);
            chat_ui.chat_win = newwin(h-3, w, 0, 0);
            chat_ui.input_win = newwin(3, w, h-3, 0);
            box(chat_ui.input_win, 0, 0);
            break;

        case MODE_QR:
            if (qr_ui.qr_win) delwin(qr_ui.qr_win);
            qr_ui.qr_win = newwin(h, w, 0, 0);
            break;
    }
    refresh();
}

// 현재 모드에 맞는 UI 복원
static void restore_current_ui(void) {
    switch (current_mode) {
        case MODE_MAIN:
            create_windows();
            break;
        case MODE_TODO:
            restore_todo_ui();
            break;
        case MODE_CHAT:
            restore_chat_ui();
            break;
        case MODE_QR:
            restore_qr_ui();
            break;
    }
}

// ToDo UI 복원
static void restore_todo_ui(void) {
    todo_enter_redraw(todo_ui.list_win, todo_ui.status_win, &todo_ui.selected);
    wrefresh(todo_ui.list_win);
    wrefresh(todo_ui.status_win);
}

// Chat UI 복원
static void restore_chat_ui(void) {
    chat_redraw(chat_ui.chat_win, chat_ui.input_win, chat_ui.partial);
    wrefresh(chat_ui.chat_win);
    wrefresh(chat_ui.input_win);
}

// QR UI 복원
static void restore_qr_ui(void) {
    show_qr_inwin(qr_ui.qr_win, qr_ui.filepath);
    wrefresh(qr_ui.qr_win);
}
