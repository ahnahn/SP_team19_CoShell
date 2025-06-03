// mainframe2.c
// ───────────────────────────────────────────────────────────────────────────
// 빌드 예시:
//   gcc mainframe2.c -o mainframe2 -lncursesw
//
// 요구 패키지 (Ubuntu/Debian):
//   sudo apt update
//   sudo apt install libncursesw5-dev qrencode
//
// 주요 수정 사항 요약:
// 1) QR 전체 화면 모드(show_qrcode_fullscreen)에서 SIGWINCH를 완전히 무시하고,
//    KEY_RESIZE 이벤트가 들어와도 화면을 다시 그려 깨짐을 방지하며,
//    'q'를 누를 때만 로비로 돌아가도록 처리.
// 2) popen()으로 qrencode를 호출해 출력 결과를 전부 읽어 와서 ncurses 윈도우에 직접 찍어 줌.
//    => 외부 쉘(stdout/stderr)로 QR이 나가지 않음.
// 3) 터미널 리사이즈 시 handle_winch()가 로비를 다시 그리도록 했다가,
//    QR 모드 진입 시에는 SIGWINCH를 SIG_IGN으로 바꿔 handle_winch가 호출되지 않도록 함.
// 4) QR 모드 진입 ― 파일 크기 검사(≤ 800바이트) → “Press any key to view QR…” → 전체 화면
//    → qrencode 결과를 ncurses 윈도우에 출력(모듈당 가로 2칸 × 세로 1칸 기준으로 자동 버전 선정)
//    → “Press any key to return to lobby”가 뜨면 아무 키나 눌러야만 로비로 돌아감.
//    → 이때 창 크기 변경(KEY_RESIZE)은 wrefresh(qrwin)만 호출, 리턴 조건이 아님.
// 5) 터미널 크기가 지나치게 작으면 “Terminal too small!” 메시지 후 바로 로비 복귀.
// 6) 800바이트 초과 파일은 “File too large… Press 'q' to return to lobby” 메시지 후
//    ‘q’ 키 입력으로만 로비로 돌아감.
//
// (※ Windows Terminal, PuTTY, GNOME Terminal 등 어디서든 동일하게 동작해야 합니다.)
// ───────────────────────────────────────────────────────────────────────────

#include <locale.h>
#include <ncurses.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#include "todo.h"

#define INPUT_HEIGHT    3
#define MAX_CMD_LEN   255
#define MAX_PATH_LEN  511

// QR 코드에 사용할 최대 파일 크기 (바이트)
#define MAX_QR_BYTES   800

static WINDOW* win_time = NULL;  // 왼쪽 상단: 시간 표시
static WINDOW* win_custom = NULL;  // 왼쪽 중간/하단: 로비 또는 모드별 콘텐츠
static WINDOW* win_todo = NULL;  // 오른쪽 전체: ToDoList 창
static WINDOW* win_input = NULL;  // 맨 아래: 커맨드 입력창

static const char* lobby_text[] = {
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
    "You can exit the program at any time by typing exit"
};
static const int lobby_lines = sizeof(lobby_text) / sizeof(lobby_text[0]);

// 전방 선언
static void cleanup_ncurses(void);
static void handle_winch(int sig);
static void create_windows(int in_lobby);
static void print_wrapped_lines(WINDOW* win, int start_y, int max_lines, int max_cols,
    const char* lines[], int n);
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3);
static void update_time(WINDOW* w);
static int pick_version_for_module(int max_module);
static void show_qrcode_fullscreen(const char* path);
static void process_and_show_file(WINDOW* custom, const char* path);

// ──────────────────────────────────────────────────────────────────────────
// 프로그램 종료 시 ncurses 정리
// ──────────────────────────────────────────────────────────────────────────
static void cleanup_ncurses(void) {
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }
    endwin();
}

// ──────────────────────────────────────────────────────────────────────────
// SIGWINCH(창 리사이즈) 핸들러: 로비 화면으로 돌아가며 창 재구성
// ──────────────────────────────────────────────────────────────────────────
static void handle_winch(int sig) {
    (void)sig;
    endwin();
    refresh();
    clear();
    create_windows(1);
}

// ──────────────────────────────────────────────────────────────────────────
// 현재 로컬/USA ET/UK GMT 시간 문자열 생성
// ──────────────────────────────────────────────────────────────────────────
static void get_time_strings(char* local_buf, int len1,
    char* us_buf, int len2,
    char* uk_buf, int len3)
{
    time_t t = time(NULL);
    struct tm tm_local = *localtime(&t);
    strftime(local_buf, len1, "%Y-%m-%d %H:%M:%S", &tm_local);

    // 미국 동부 (KST+9 → GMT-5 = -14시간)
    struct tm tm_us = tm_local;
    tm_us.tm_hour -= 14;
    mktime(&tm_us);
    strftime(us_buf, len2, "%Y-%m-%d %H:%M:%S (USA ET)", &tm_us);

    // 영국 GMT (KST+9 → GMT = -9시간)
    struct tm tm_uk = tm_local;
    tm_uk.tm_hour -= 9;
    mktime(&tm_uk);
    strftime(uk_buf, len3, "%Y-%m-%d %H:%M:%S (UK GMT)", &tm_uk);
}

// ──────────────────────────────────────────────────────────────────────────
// win 내부에서 자동 줄바꿈하여 텍스트 배열 출력
// start_y   : border 아래 첫 행
// max_lines : 내부에 출력할 수 있는 최대 줄 수(border 제외)
// max_cols  : 내부에 출력 가능한 열 수(border 제외, 좌우 여유 두 칸씩)
// lines     :  출력할 문자열 배열
// n         :  lines 배열 길이
// ──────────────────────────────────────────────────────────────────────────
static void print_wrapped_lines(WINDOW* win, int start_y,
    int max_lines, int max_cols,
    const char* lines[], int n)
{
    int row = start_y;
    for (int i = 0; i < n && row < start_y + max_lines; i++) {
        const char* text = lines[i];
        int len = (text ? (int)strlen(text) : 0);
        int offset = 0;

        // 빈 줄이면 공백 한 칸 출력 후 다음 줄
        if (len == 0) {
            if (row < start_y + max_lines) {
                mvwaddch(win, row++, 2, ' ');
            }
            continue;
        }
        // 긴 줄은 max_cols 단위로 나눠서 출력
        while (offset < len && row < start_y + max_lines) {
            int chunk = (len - offset > max_cols ? max_cols : len - offset);
            mvwprintw(win, row++, 2, "%.*s", chunk, text + offset);
            offset += chunk;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// 4개 윈도우 생성/재구성
// in_lobby == 1 → 로비 모드(환영문단 출력), else 빈 화면
// ──────────────────────────────────────────────────────────────────────────
static void create_windows(int in_lobby)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (1) 기존 윈도우 삭제
    if (win_time) { delwin(win_time);   win_time = NULL; }
    if (win_custom) { delwin(win_custom); win_custom = NULL; }
    if (win_todo) { delwin(win_todo);   win_todo = NULL; }
    if (win_input) { delwin(win_input);  win_input = NULL; }

    // (2) 크기 계산
    int left_width = cols / 2;
    int right_width = cols - left_width;
    int left_height = rows - INPUT_HEIGHT;
    int title_height = 1;
    int time_height = 5;  // border(2) + 라벨(1) + 시간 3줄 = 5
    int custom_y = title_height + time_height;
    int custom_height = left_height - custom_y;
    if (custom_height < 1) custom_height = 1;

    // (3) 최상단 타이틀
    mvprintw(0, 0, "<< CoShell >> Beta");
    refresh();

    // (4) 왼쪽 상단: Time 창
    win_time = newwin(time_height, left_width, title_height, 0);

    // (5) 왼쪽 중간/하단: 로비 or 모드 콘텐츠
    win_custom = newwin(custom_height, left_width, custom_y, 0);

    // (6) 오른쪽 전체: ToDoList 창
    win_todo = newwin(rows - INPUT_HEIGHT, right_width, 0, left_width);

    // (7) 맨 아래: Command 입력창
    win_input = newwin(INPUT_HEIGHT, cols, rows - INPUT_HEIGHT, 0);

    // (8) 스크롤 설정
    scrollok(win_time, FALSE);
    scrollok(win_custom, TRUE);
    scrollok(win_todo, TRUE);

    // (9) Time 창 초기화
    box(win_time, 0, 0);
    mvwprintw(win_time, 0, 2, " Time ");
    mvwprintw(win_time, 1, 2, "Local:    --:--:--");
    mvwprintw(win_time, 2, 2, "USA  :    --:--:--");
    mvwprintw(win_time, 3, 2, "UK   :    --:--:--");
    wrefresh(win_time);

    // (10) Custom 창(로비 또는 모드)
    box(win_custom, 0, 0);
    if (in_lobby) {
        int maxy, maxx;
        getmaxyx(win_custom, maxy, maxx);
        int inner_lines = maxy - 2;
        int inner_cols = maxx - 4;
        print_wrapped_lines(win_custom, 1, inner_lines, inner_cols,
            lobby_text, lobby_lines);
    }
    wrefresh(win_custom);

    // (11) ToDoList 창 초기화
    box(win_todo, 0, 0);
    mvwprintw(win_todo, 1, 2, "=== ToDo List ===");
    wrefresh(win_todo);

    // (12) 입력창 초기화
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "Command: ");
    wrefresh(win_input);
}

// ──────────────────────────────────────────────────────────────────────────
// win_time에 로컬/USA ET/UK GMT 시간 갱신
// ──────────────────────────────────────────────────────────────────────────
static void update_time(WINDOW* w)
{
    char local_buf[32], us_buf[32], uk_buf[32];
    get_time_strings(local_buf, sizeof(local_buf),
        us_buf, sizeof(us_buf),
        uk_buf, sizeof(uk_buf));

    int h_time, w_time;
    getmaxyx(w, h_time, w_time);
    if (h_time < 5 || w_time < 20) return;

    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Time ");
    mvwprintw(w, 1, 2, "Local: %s", local_buf);
    mvwprintw(w, 2, 2, "USA  : %s", us_buf);
    mvwprintw(w, 3, 2, "UK   : %s", uk_buf);
    wrefresh(w);
}

// ──────────────────────────────────────────────────────────────────────────
// pick_version_for_module:
//  max_module(세로 또는 가로 모듈 개수), 최소 버전 반환
//  모듈 크기 = 17 + 4*v  (v=버전), module_size <= max_module
// ──────────────────────────────────────────────────────────────────────────
static int pick_version_for_module(int max_module) {
    for (int v = 1; v <= 40; v++) {
        int module_size = 17 + 4 * v;
        if (module_size > max_module) {
            return v - 1;
        }
    }
    return 40;
}

// ──────────────────────────────────────────────────────────────────────────
// show_qrcode_fullscreen:
//  - 전체 화면을 덮는 qrwin 생성
//  - SIGWINCH 무시 → KEY_RESIZE 시 wrefresh만 호출 (화면 깨짐 방지)
//  - “한 모듈 = 가로 2칸 × 세로 1칸” 기준으로 자동 버전 선정
//  - qrencode 출력을 popen()으로 받아와서 ncurses 윈도우에 직접 출력
//  - ‘q’ 키가 아닌 다른 키를 누르면 그대로 리턴하지 않고, “Press any key to return to lobby” 안내에 따라
//    아무 키를 누를 때만 로비로 돌아감. 리사이즈(KEY_RESIZE)는 계속 wrefresh만 함.
//  - 터미널이 너무 작으면 “Terminal too small!” 메시지 후 바로 로비로 돌아감.
// ──────────────────────────────────────────────────────────────────────────
static void show_qrcode_fullscreen(const char* path)
{
    // (1) SIGWINCH 무시 → handle_winch 호출되지 않음
    void (*old_winch)(int) = signal(SIGWINCH, SIG_IGN);

    // (2) 전체 화면 크기 가져오기
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // (3) 화면 전체 지우고 리프레시
    werase(stdscr);
    wrefresh(stdscr);

    // (4) safe 영역 계산 (상단 안내 1줄 + 하단 안내 1줄 제외)
    int safe_rows = rows - 2;
    int safe_cols_mod = cols / 2;  // 한 모듈을 가로 2칸으로 봄
    if (safe_rows < 1 || safe_cols_mod < 1) {
        // 너무 작으면 바로 로비로 돌아감
        clear();
        mvprintw(rows / 2, (cols - 24) / 2, "Terminal too small!");
        mvprintw(rows / 2 + 1, (cols - 28) / 2, "Press any key to return");
        refresh();
        getch();
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    // (5) 버전 자동 선정
    int max_module = (safe_rows < safe_cols_mod ? safe_rows : safe_cols_mod);
    int version = pick_version_for_module(max_module);
    if (version < 1) version = 1;

    // (6) qrwin 생성 (전체 화면 덮음)
    WINDOW* qrwin = newwin(rows, cols, 0, 0);
    scrollok(qrwin, FALSE);
    werase(qrwin);

    // (7) 상단 안내
    mvwprintw(qrwin, 0, 0, "Press 'q' to return to lobby (QR v%d)", version);
    wrefresh(qrwin);

    // (8) qrencode를 popen으로 호출: 테두리 없음(m=0), 낮은 오류 레벨(-l L)
    //     -r 옵션을 쓰면 바이트 스트림(이미지) 출력 → CLI에서는 UTF8 텍스트 출력(-t UTF8)을 사용
    //     popen() 결과를 한 줄씩 읽어 와서 qrwin에 찍기
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "cat \"%s\" | qrencode -t UTF8 -l L -m 0 -v %d 2>/dev/null",
        path, version);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        mvwprintw(qrwin, 2, 0, "Failed to run qrencode on %s", path);
        mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return");
        wrefresh(qrwin);
        int c;
        keypad(qrwin, TRUE);
        nodelay(qrwin, FALSE);
        while ((c = wgetch(qrwin)) != 'q' && c != 'Q') {
            // 다른 키는 무시
        }
        delwin(qrwin);
        clear();
        create_windows(1);
        signal(SIGWINCH, old_winch);
        return;
    }

    // (9) QR 텍스트 출력: 첫 번째 줄부터 safe_rows만큼
    char linebuf[1024];
    int row = 1;
    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        int len = (int)strlen(linebuf);
        if (len > 0 && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
            linebuf[len - 1] = '\0';
        if (row >= safe_rows) break;
        mvwprintw(qrwin, row++, 0, "%s", linebuf);
        wrefresh(qrwin);
    }
    pclose(fp);

    // (10) 하단 안내
    mvwprintw(qrwin, rows - 1, 0, "Press 'q' to return to lobby");
    wrefresh(qrwin);

    // (11) KEY_RESIZE은 wrefresh만 호출, 'q'를 누르면 리턴
    keypad(qrwin, TRUE);
    nodelay(qrwin, FALSE);
    int ch;
    while (1) {
        ch = wgetch(qrwin);
        if (ch == KEY_RESIZE) {
            // 단순히 화면 리프레시만
            wrefresh(qrwin);
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        }
        // 그 외 키는 무시하고 계속 대기
    }

    // (12) QR 윈도우 정리 → 로비로 복귀
    delwin(qrwin);
    clear();
    create_windows(1);

    // SIGWINCH 복원
    signal(SIGWINCH, old_winch);
}

// ──────────────────────────────────────────────────────────────────────────
// process_and_show_file:
//  - 파일이 .c 또는 .txt인지 검사
//  - 크기 검사: 800바이트 초과 시 “File too large… Press 'q' to return” 대기 후 로비 복귀
//  - 그렇지 않으면 “Press any key to view QR…” 메시지 후 show_qrcode_fullscreen()
// ──────────────────────────────────────────────────────────────────────────
static void process_and_show_file(WINDOW* custom, const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File not found or not regular: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    // 확장자 검사
    const char* ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".txt") != 0)) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "Only .c or .txt allowed: %s", path);
        wrefresh(custom);
        napms(1500);
        return;
    }

    // (A) 파일 크기 검사
    if (st.st_size > MAX_QR_BYTES) {
        werase(custom);
        box(custom, 0, 0);
        mvwprintw(custom, 1, 2, "File too large (%ld bytes).", (long)st.st_size);
        mvwprintw(custom, 2, 2, "Max allowed for QR: %d bytes", MAX_QR_BYTES);
        mvwprintw(custom, 4, 2, "Press 'q' to return to lobby");
        wrefresh(custom);

        // ‘q’ 또는 ‘Q’ 키 입력 대기 → 그제야 로비로 복귀
        int c;
        keypad(custom, TRUE);
        nodelay(custom, FALSE);
        while ((c = wgetch(custom)) != 'q' && c != 'Q') {
            // 다른 키는 무시
        }
        clear();
        create_windows(1);
        return;
    }

    // (B) 크기 적당 → QR 보기 전 안내
    werase(custom);
    box(custom, 0, 0);
    mvwprintw(custom, 1, 2, "Generating QR for: %s", path);
    mvwprintw(custom, 2, 2, "Press any key to view QR...");
    wrefresh(custom);

    // 짧게 대기 후 키 대기
    napms(300);
    wgetch(custom);

    // (C) QR 전체 화면 모드
    show_qrcode_fullscreen(path);
}

// ──────────────────────────────────────────────────────────────────────────
// main: ncurses 초기화 → 로비 모드 → 이벤트 루프
// ──────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    // UTF-8 로케일 활성화
    setlocale(LC_ALL, "");

    // 프로그램 종료 시 ncurses 정리 보장
    atexit(cleanup_ncurses);

    // Ctrl-C, Ctrl-Z 무시 (exit 명령으로만 종료)
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGWINCH, handle_winch);

    // ncurses 초기화
    initscr();
    cbreak();      // 즉시 입력
    noecho();      // 키 화면에 표시 안 함
    keypad(stdscr, TRUE);
    curs_set(1);   // 커서 보이기

    // 첫 화면: 로비
    create_windows(1);

    // 명령어 버퍼
    char cmdbuf[MAX_CMD_LEN + 1] = { 0 };
    int  cmdlen = 0;

    // QR 경로 입력용 버퍼
    char pathbuf[MAX_PATH_LEN + 1] = { 0 };
    int  pathlen = 0;

    // 시간 갱신 타이머
    time_t last_time = 0;

    // 모드 상태:
    //  0=로비(커맨드 대기),
    //  1=To-Do 모드(예시),
    //  2=Chat 모드(예시),
    //  3=QR 경로 입력 모드,
    //  4=QR 전체 화면 모드
    int mode = 0;

    // 입력창 내부 좌표
    int input_y = 1, input_x = 10;

    while (1) {
        // 1초마다 시간 갱신
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            update_time(win_time);
        }

        // ─────────────────────────────────────────────────
        // (A) QR 경로 입력 모드
        // ─────────────────────────────────────────────────
        if (mode == 3) {
            // win_custom에 경로 입력 안내
            werase(win_custom);
            box(win_custom, 0, 0);
            mvwprintw(win_custom, 1, 2, "Enter path for QR code:");
            mvwprintw(win_custom, 2, 2, "%s", pathbuf);
            wrefresh(win_custom);

            // 입력창에 커서 이동
            wmove(win_input, input_y, input_x + pathlen);
            wrefresh(win_input);

            // 비차단 입력 검사(200ms)
            wtimeout(win_input, 200);
            int ch = wgetch(win_input);
            if (ch != ERR) {
                if (ch == KEY_BACKSPACE || ch == 127) {
                    if (pathlen > 0) {
                        pathlen--;
                        pathbuf[pathlen] = '\0';
                        mvwprintw(win_input, input_y, input_x + pathlen, " ");
                        wmove(win_input, input_y, input_x + pathlen);
                        wrefresh(win_input);
                    }
                }
                else if (ch == '\n' || ch == KEY_ENTER) {
                    // QR 전체 화면 모드로 전환
                    mode = 4;
                    cmdlen = 0;
                    memset(cmdbuf, 0, sizeof(cmdbuf));
                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                else if (ch >= 32 && ch <= 126) {
                    if (pathlen < MAX_PATH_LEN) {
                        pathbuf[pathlen++] = (char)ch;
                        mvwprintw(win_input, input_y, input_x + pathlen - 1, "%c", ch);
                        wmove(win_input, input_y, input_x + pathlen);
                        wrefresh(win_input);
                    }
                }
            }
            continue;
        }

        // ─────────────────────────────────────────────────
        // (B) QR 전체 화면 모드
        // ─────────────────────────────────────────────────
        if (mode == 4) {
            // 파일 처리 및 QR 그리기
            process_and_show_file(win_custom, pathbuf);
            mode = 0;  // 로비 모드로 복귀
            continue;
        }

        // ─────────────────────────────────────────────────
        // (C) 나머지 모드: 로비 / To-Do / Chat / 커맨드
        // ─────────────────────────────────────────────────
        wtimeout(win_input, 200);
        int ch = wgetch(win_input);
        if (ch != ERR) {
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmdlen > 0) {
                    cmdlen--;
                    cmdbuf[cmdlen] = '\0';
                    mvwprintw(win_input, input_y, input_x + cmdlen, " ");
                    wmove(win_input, input_y, input_x + cmdlen);
                    wrefresh(win_input);
                }
            }
            else if (ch == '\n' || ch == KEY_ENTER) {
                cmdbuf[cmdlen] = '\0';

                // exit → 프로그램 종료
                if (strcmp(cmdbuf, "exit") == 0) {
                    break;
                }
                // 1 → To-Do 모드 (예시)
                else if (cmdlen > 0 && cmdbuf[0] == '1') {
                    mode = 1;
                    todo_enter(win_input, win_todo, win_custom);
		    mode = 0;
                }
                // 2 → Chat 모드 (예시)
                else if (cmdlen > 0 && cmdbuf[0] == '2') {
                    mode = 2;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Entering Chat mode...");
                    wrefresh(win_custom);
                }
                // 3 → QR 코드 모드 (경로 입력으로 넘어감)
                else if (cmdlen > 0 && cmdbuf[0] == '3') {
                    mode = 3;
                    pathlen = 0;
                    memset(pathbuf, 0, sizeof(pathbuf));
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Enter path for QR code:");
                    wrefresh(win_custom);

                    werase(win_input);
                    box(win_input, 0, 0);
                    wrefresh(win_input);
                }
                /* // a <item> → To-Do 추가 예시: 맨 아래 줄에 추가
                else if (cmdlen > 2 && cmdbuf[0] == 'a' && cmdbuf[1] == ' ') {
                    const char* item = cmdbuf + 2;
                    int y_max, x_max;
                    getmaxyx(win_todo, y_max, x_max);
                    mvwprintw(win_todo, y_max - 2, 2, "- %s", item);
                    wrefresh(win_todo);
                } */
                // f <path> → 파일 경로로 직접 QR 모드 (800바이트 검사 포함)
                else if (cmdlen > 2 && cmdbuf[0] == 'f' && cmdbuf[1] == ' ') {
                    const char* filepath = cmdbuf + 2;
                    process_and_show_file(win_custom, filepath);
                }
                // 그 외 → Unknown command
                else {
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    mvwprintw(win_custom, 1, 2, "Unknown command: %s", cmdbuf);
                    wrefresh(win_custom);
                    napms(2000);
                    mode = 0;
                    werase(win_custom);
                    box(win_custom, 0, 0);
                    int maxy, maxx;
                    getmaxyx(win_custom, maxy, maxx);
                    int inner_lines = maxy - 2;
                    int inner_cols = maxx - 4;
                    print_wrapped_lines(win_custom, 1, inner_lines, inner_cols,
                        lobby_text, lobby_lines);
                    wrefresh(win_custom);
                }

                // 입력창 초기화
                cmdlen = 0;
                memset(cmdbuf, 0, sizeof(cmdbuf));
                werase(win_input);
                box(win_input, 0, 0);
                mvwprintw(win_input, 1, 2, "Command: ");
                wrefresh(win_input);
            }
            // 일반 문자 입력 (ASCII)
            else if (ch >= 32 && ch <= 126) {
                if (cmdlen < MAX_CMD_LEN) {
                    cmdbuf[cmdlen++] = (char)ch;
                    mvwprintw(win_input, input_y, input_x + cmdlen - 1, "%c", ch);
                    wmove(win_input, input_y, input_x + cmdlen);
                    wrefresh(win_input);
                }
            }
            // 나머지 키 무시
        }
    }

    return 0;
}
