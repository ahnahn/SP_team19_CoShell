/*
 * main_ui.c
 *  - 전체 메뉴(Main Menu / UI / CLI 등)
 *  - ToDo/Chat/QR 모듈 호출
 */

#include <locale.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include "todo.h"
#include "chat.h"
#include "qr.h" 

#define BUF_SIZE    1024
#define LOCAL_PORT  12345

/* 전역 변수 (ncurses 창 포인터) */
WINDOW *win_chat, *win_todo, *win_input;

/* 함수 선언 */
void show_main_menu();
void cli_main(int argc, char *argv[]);
void ui_main();
void show_qr(const char *filename);

/*==============================*/
/*       Serveo 터널 설정      */
/*==============================*/
/* chat.c에도 동일하게 있지만, 메인 메뉴에서만 사용하는 print용으로 재정의 */
int setup_serveo_tunnel(int local_port) {
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        perror("pipe");
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }
    else if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        char forward_arg[64];
        snprintf(forward_arg, sizeof(forward_arg), "0:localhost:%d", local_port);
        execlp(
            "ssh", "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "ServerAliveInterval=60",
            "-N",
            "-R", forward_arg,
            "serveo.net",
            (char*)NULL
        );
        perror("execlp");
        _exit(1);
    }
    else {
        close(pipe_fd[1]);
        FILE *fp = fdopen(pipe_fd[0], "r");
        if (!fp) {
            perror("fdopen");
            close(pipe_fd[0]);
            return -1;
        }
        char line[512];
        int remote_port = -1;
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "Allocated port %d", &remote_port) == 1) {
                printf(">> Serveo 터널 할당 완료: serveo.net:%d\n", remote_port);
                fflush(stdout);
                break;
            }
        }
        fclose(fp);
        return remote_port;
    }
}

/*==============================*/
/*            main              */
/*==============================*/
int main(int argc, char *argv[]) {
    // (한글 UI를 다시 쓰려면 아래 주석 해제)
    // setlocale(LC_ALL, "");
    // initscr(); endwin();

    if (argc == 1) {
        show_main_menu();
    }
    else if (
        strcmp(argv[1], "add") == 0 ||
        strcmp(argv[1], "list") == 0 ||
        strcmp(argv[1], "del") == 0 ||
        strcmp(argv[1], "qr") == 0
    ) {
        cli_main(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "ui") == 0) {
        ui_main();
    }
    else if (strcmp(argv[1], "server") == 0) {
        /* 서버 모드(Serveo 터널 자동 설정 + Chat 서버 실행) */
        printf(">> Serveo.net을 통해 원격 포트를 요청하는 중...\n");
        int remote_port = setup_serveo_tunnel(LOCAL_PORT);
        if (remote_port < 0) {
            fprintf(stderr, "Serveo 터널 설정 실패. 로컬 서버를 바로 실행합니다.\n");
            chat_server(LOCAL_PORT);
        } else {
            printf(">> Serveo 원격 주소: serveo.net:%d\n", remote_port);
            printf(">> 로컬 채팅 서버를 %d번 포트에 바인딩하여 실행합니다.\n", LOCAL_PORT);
            chat_server(LOCAL_PORT);
        }
    }
    else if (strcmp(argv[1], "client") == 0 && argc == 4) {
        chat_client(argv[2], atoi(argv[3]));
    }
    else {
        fprintf(stderr, "Invalid mode or missing arguments.\n");
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  coshell [add|list|del|qr] ...\n");
        fprintf(stderr, "  coshell            # 메뉴 모드\n");
        fprintf(stderr, "  coshell ui         # UI 모드 (ToDo + Chat)\n");
        fprintf(stderr, "  coshell server     # Serveo 터널 + Chat 서버\n");
        fprintf(stderr, "  coshell client <host> <port>\n");
        return 1;
    }
    return 0;
}

/*==============================*/
/*   메인 메뉴 출력 함수       */
/*==============================*/
void show_main_menu() {
    int choice = 0;
    while (1) {
        printf("\033[H\033[J");
        printf("\n===== CoShell Main Menu =====\n");
        printf("1. Run Chat Server (Serveo 터널 자동 설정)\n");
        printf("2. Run Client (ToDo + Chat UI)\n");
        printf("3. Exit\n");
        printf("Select (1-3): ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Input error. Exiting.\n");
            return;
        }
        getchar();  // 개행문자 제거

        if (choice == 1) {
            printf("\033[H\033[J");
            printf("Chat server (Serveo 터널 자동 설정) 실행 중...\n");
            int remote_port = setup_serveo_tunnel(LOCAL_PORT);
            if (remote_port < 0) {
                fprintf(stderr, "Serveo 터널 설정 실패. 로컬 서버 실행.\n");
                chat_server(LOCAL_PORT);
            } else {
                printf(">> Serveo 원격 주소: serveo.net:%d\n", remote_port);
                printf(">> 로컬 채팅 서버를 %d번 포트에 바인딩하여 실행합니다.\n", LOCAL_PORT);
                chat_server(LOCAL_PORT);
            }
            break;
        }
        else if (choice == 2) {
            ui_main();
            break;
        }
        else if (choice == 3) {
            printf("Exiting program.\n");
            break;
        }
        else {
            printf("Invalid selection. Try again.\n");
            sleep(1);
        }
    }
}

/*==============================*/
/*        CLI 모드 함수         */
/*==============================*/
void cli_main(int argc, char *argv[]) {
    if (argc == 0) {
        printf("Usage:\n");
        printf("  coshell list\n");
        printf("  coshell add <item>\n");
        printf("  coshell del <index>\n");
        printf("  coshell qr <filepath>\n");
        return;
    }

    load_todo();

    if (strcmp(argv[0], "list") == 0) {
        for (int i = 0; i < todo_count; i++) {
            printf("%d. %s\n", i + 1, todos[i]);
        }
    }
    else if (strcmp(argv[0], "add") == 0 && argc >= 2) {
        char buf[512] = {0};
        for (int i = 1; i < argc; i++) {
            strcat(buf, argv[i]);
            if (i < argc - 1) strcat(buf, " ");
        }
        pthread_mutex_lock(&todo_lock);
        add_todo(buf);
        pthread_mutex_unlock(&todo_lock);
        printf("Added: %s\n", buf);
    }
    else if (strcmp(argv[0], "del") == 0 && argc == 2) {
        int idx = atoi(argv[1]) - 1;
        if (idx < 0 || idx >= todo_count) {
            printf("Invalid index.\n");
            return;
        }
        pthread_mutex_lock(&todo_lock);
        free(todos[idx]);
        for (int i = idx; i < todo_count - 1; i++) {
            todos[i] = todos[i + 1];
        }
        todo_count--;
        // 파일 다시 저장
        FILE *fp = fopen(TODO_FILE, "w");
        if (fp) {
            for (int i = 0; i < todo_count; i++) {
                fprintf(fp, "%s\n", todos[i]);
            }
            fclose(fp);
        }
        pthread_mutex_unlock(&todo_lock);
        printf("Deleted todo #%d\n", idx + 1);
    }
    else if (strcmp(argv[0], "qr") == 0 && argc == 2) {
        show_qr(argv[1]);
    }
    else {
        fprintf(stderr, "Unknown CLI command.\n");
    }
}

/*==============================*/
/*        UI 모드 함수          */
/*==============================*/
void ui_main() {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int h, w;
    getmaxyx(stdscr, h, w);

    // 채팅+ToDo 분할 창 생성
    win_chat  = newwin(h - 3, w / 2, 0, 0);
    win_todo  = newwin(h - 3, w - w / 2, 0, w / 2);
    win_input = newwin(3, w, h - 3, 0);

    scrollok(win_chat, TRUE);

    // ToDo 목록 로드→화면에 표시
    load_todo();
    draw_todo(win_todo);

    while (1) {
        werase(win_input);
        mvwprintw(win_input, 1, 1, "Command: (a)Add (q)QR (c)Chat (x)Exit > ");
        wrefresh(win_input);

        int ch = wgetch(win_input);
        if (ch == 'x') {
            break;
        }
        else if (ch == 'a') {
            werase(win_input);
            mvwprintw(win_input, 1, 1, "Add ToDo: ");
            wrefresh(win_input);

            echo();
            char buf[256];
            mvwgetnstr(win_input, 1, 12, buf, 200);
            noecho();

            pthread_mutex_lock(&todo_lock);
            add_todo(buf);
            draw_todo(win_todo);
            pthread_mutex_unlock(&todo_lock);
        }
        else if (ch == 'q') {
            werase(win_input);
            mvwprintw(win_input, 1, 1, "Enter file path for QR: ");
            wrefresh(win_input);

            echo();
            char buf[256];
            mvwgetnstr(win_input, 1, 23, buf, 200);
            noecho();

            show_qr(buf);
        }
        else if (ch == 'c') {
            werase(win_input);
            mvwprintw(win_input, 1, 1, "Chat server host (e.g., localhost): ");
            wrefresh(win_input);

            echo();
            char host[128];
            mvwgetnstr(win_input, 1, 36, host, 100);
            noecho();

            werase(win_input);
            mvwprintw(win_input, 1, 1, "Port: ");
            wrefresh(win_input);

            echo();
            char port_str[16];
            mvwgetnstr(win_input, 1, 6, port_str, 10);
            noecho();
            int port = atoi(port_str);

            endwin();  // ncurses 모드 종료
            chat_client(host, port);
            return;
        }
    }

    endwin();
}
