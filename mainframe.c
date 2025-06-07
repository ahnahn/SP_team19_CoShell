// mainframe.c
#include <locale.h>
#include <ncurses.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "qr.h"

#define INPUT_HEIGHT   3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN 1600

// ncurses 윈도우 핸들
static WINDOW *win_time   = NULL;
static WINDOW *win_custom = NULL;
static WINDOW *win_todo   = NULL;
static WINDOW *win_input  = NULL;

// 로비 텍스트 (원본 그대로)
static const char *lobby_text[] = {
    "Welcome!",
    "CoShell, short for \"cooperation in Shell,\" is a terminal-based collaboration toolbox.",
    "With To-Do List Management, you can share plans with your team members,",
    "exchange information via real-time chat,",
    "and transmit data using QR codes for groundbreaking data transfer.",
    "",
    "Enter a command below to start collaborating:",
    "",
    "1. To-Do List Management",
    "2. Chat",
    "3. QR Code",
    "",
    "You can exit the program at any time by typing exit",
    "",
    "If the screen breaks for a moment, press the space key or minimize and then maximize the window again."
};
static const int lobby_lines = sizeof(lobby_text) / sizeof(lobby_text[0]);

// 외부 정의된 함수
void create_windows(int in_lobby);

static void cleanup_ncurses(void);
static void handle_winch(int sig);
static void get_time_strings(char *local_buf,int l1,char *us_buf,int l2,char *uk_buf,int l3);
static void print_wrapped_lines(WINDOW *win,int sy,int ml,int mc,const char **lines,int n);
static void update_time(WINDOW *w);

static void cleanup_ncurses(void) {
    if (win_time)   delwin(win_time);
    if (win_custom) delwin(win_custom);
    if (win_todo)   delwin(win_todo);
    if (win_input)  delwin(win_input);
    endwin();
}

static void handle_winch(int sig) {
    (void)sig;
    endwin(); refresh(); clear();
    create_windows(1);
}

static void get_time_strings(char *local_buf,int l1,char *us_buf,int l2,char *uk_buf,int l3){
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    strftime(local_buf, l1, "%Y-%m-%d %H:%M:%S", &tm);
    struct tm tu = tm; tu.tm_hour -= 14; mktime(&tu);
    strftime(us_buf,   l2, "%Y-%m-%d %H:%M:%S (USA ET)", &tu);
    struct tm tk = tm; tk.tm_hour -= 9;  mktime(&tk);
    strftime(uk_buf,   l3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tk);
}

static void print_wrapped_lines(WINDOW *w,int sy,int ml,int mc,const char **lines,int n){
    int row = sy;
    for(int i=0;i<n && row<sy+ml;i++){
        const char *t = lines[i];
        int len = t?strlen(t):0, off=0;
        if(len==0){
            mvwaddch(w, row++, 2, ' ');
            continue;
        }
        while(off < len && row < sy+ml){
            int chunk = len-off > mc ? mc : len-off;
            mvwprintw(w, row++, 2, "%.*s", chunk, t+off);
            off += chunk;
        }
    }
}

static void update_time(WINDOW *w) {
    char lb[32], ub[32], kb[32];
    get_time_strings(lb,sizeof(lb),ub,sizeof(ub),kb,sizeof(kb));
    int h,wth; getmaxyx(w,h,wth);
    if(h<5||wth<20) return;
    werase(w);
    box(w,0,0);
    mvwprintw(w,0,2," Time ");
    mvwprintw(w,1,2,"Local: %s", lb);
    mvwprintw(w,2,2,"USA  : %s", ub);
    mvwprintw(w,3,2,"UK   : %s", kb);
    wrefresh(w);
}

void create_windows(int in_lobby){
    int rows,cols; getmaxyx(stdscr,rows,cols);
    if(win_time)   delwin(win_time);
    if(win_custom) delwin(win_custom);
    if(win_todo)   delwin(win_todo);
    if(win_input)  delwin(win_input);

    mvprintw(0,0,"<< CoShell >> Beta"); refresh();

    int left_w = cols/2, right_w = cols-left_w;
    int top_h  = rows-INPUT_HEIGHT;
    int title_h = 1, time_h = 5;
    int custom_y = title_h+time_h;
    int custom_h = top_h - time_h - title_h;
    if(custom_h<1) custom_h=1;

    win_time   = newwin(time_h, left_w, title_h,        0);
    win_custom = newwin(custom_h, left_w, custom_y,     0);
    win_todo   = newwin(rows-INPUT_HEIGHT, right_w,     0,left_w);
    win_input  = newwin(INPUT_HEIGHT, cols, rows-INPUT_HEIGHT,0);

    keypad(win_input, TRUE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo,   TRUE);

    box(win_time,0,0);
    mvwprintw(win_time,0,2," Time ");
    mvwprintw(win_time,1,2,"Local:    --:--:--");
    mvwprintw(win_time,2,2,"USA  :    --:--:--");
    mvwprintw(win_time,3,2,"UK   :    --:--:--");
    wrefresh(win_time);

    box(win_custom,0,0);
    if(in_lobby){
        int my,mx; getmaxyx(win_custom,my,mx);
        print_wrapped_lines(win_custom,1,my-2,mx-4,
                            lobby_text,lobby_lines);
    }
    wrefresh(win_custom);

    box(win_todo,0,0);
    mvwprintw(win_todo,1,2,"=== ToDo List ===");
    wrefresh(win_todo);

    box(win_input,0,0);
    mvwprintw(win_input,1,2,"Command: ");
    wrefresh(win_input);
}

int main(void){
    setlocale(LC_ALL,"");
    atexit(cleanup_ncurses);
    signal(SIGWINCH, handle_winch);
    signal(SIGINT,  SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    create_windows(1);

    char cmdbuf[MAX_CMD_LEN+1] = {0};
    char pathbuf[MAX_PATH_LEN+1] = {0};
    int cmdlen=0, pathlen=0, mode=0;
    int iy=1, ix=10;
    time_t last=0;

    while(1){
        time_t now = time(NULL);
        if(now!=last){ last=now; update_time(win_time); }

        if(mode==3){
            werase(win_custom); box(win_custom,0,0);
            mvwprintw(win_custom,1,2,"Enter path for QR code:");
            mvwprintw(win_custom,2,2,"%s",pathbuf);
            wrefresh(win_custom);

            mvwprintw(win_input,iy,ix,"%s",pathbuf);
            wclrtoeol(win_input);
            wmove(win_input,iy,ix+pathlen);
            wrefresh(win_input);

            wtimeout(win_input,200);
            int ch = wgetch(win_input);
            if(ch==KEY_BACKSPACE||ch==127){
                if(pathlen>0) pathbuf[--pathlen]='\0';
            }
            else if(ch=='\n'||ch==KEY_ENTER){
                process_and_show_file(win_custom,pathbuf);
                create_windows(1);
                mode=0; pathlen=0; cmdlen=0;
                memset(pathbuf,0,sizeof(pathbuf));
            }
            else if(ch>=32&&ch<=126){
                if(pathlen<MAX_PATH_LEN)
                    pathbuf[pathlen++]=(char)ch;
            }
            continue;
        }

        wtimeout(win_input,200);
        int ch = wgetch(win_input);
        if(ch!=ERR){
            if(ch==KEY_BACKSPACE||ch==127){
                if(cmdlen>0) cmdbuf[--cmdlen]='\0';
                mvwprintw(win_input,iy,ix,"%s",cmdbuf);
                wclrtoeol(win_input);
                wmove(win_input,iy,ix+cmdlen);
                wrefresh(win_input);
            }
            else if(ch=='\n'||ch==KEY_ENTER){
                cmdbuf[cmdlen]='\0';
                if(strcmp(cmdbuf,"exit")==0){
                    break;
                }
                else if(cmdlen==1 && cmdbuf[0]=='3'){
                    mode=3; pathlen=0;
                    memset(pathbuf,0,sizeof(pathbuf));
                    werase(win_input); box(win_input,0,0);
                    mvwprintw(win_input,1,2,"Enter path for QR code:");
                    wrefresh(win_input);
                }
                else if(cmdlen>2 && cmdbuf[0]=='f' && cmdbuf[1]==' '){
                    process_and_show_file(win_custom,cmdbuf+2);
                    create_windows(1);
                }
                else {
                    werase(win_custom); box(win_custom,0,0);
                    mvwprintw(win_custom,1,2,"Unknown command: %s",cmdbuf);
                    wrefresh(win_custom);
                    napms(1000);
                    create_windows(1);
                }
                cmdlen=0; memset(cmdbuf,0,sizeof(cmdbuf));
                werase(win_input); box(win_input,0,0);
                mvwprintw(win_input,1,2,"Command: ");
                wrefresh(win_input);
            }
            else if(ch>=32&&ch<=126){
                if(cmdlen<MAX_CMD_LEN){
                    cmdbuf[cmdlen++]=(char)ch;
                    mvwprintw(win_input,iy,ix+cmdlen-1,"%c",ch);
                    wrefresh(win_input);
                }
            }
        }
    }

    return 0;
}
