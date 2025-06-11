터미널에서 make 명령어로 컴파일

./coshell 명령어로 실행

메인 ui 진입 전 :

1 입력 : serveo.net 서버 포트 열기

2 입력 : 메인 ui 클라이언트 진입

<CLI 모드>

./coshell server 
명령어로 서버 포트 열기

./coshell ui 
명령어로 즉시 메인 ui 클라이언트 진입

./coshell add <item> 
todo_user.txt 파일에 할 일 item 추가

./coshell done <num> 
todo_user.txt 파일에 해당하는 num 할 일에 체크 표시

./coshell undo <num> 
todo_user.txt 파일에 해당하는 num 할 일에 체크 해제

./coshell del <num> 
todo_user.txt 파일에 해당하는 num 할 일 삭제

./coshell edit <num> <new_item> 
todo_user.txt 파일에 해당하는 num 할 일을 new_item으로 수정
