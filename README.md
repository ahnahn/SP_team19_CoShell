
qr코드를 보여주는 화면에서 창의 크기를 변화하면 화면출력이 깨지는 현상을 고쳤습니다 
qr코드 위에 When you change the window size, press spacekey to refresh 라는 문구를 추가하여 화면이 깨졌을때 스페이스바를 누르면 다시 화면이 정상적으로 나오도록 수정하였고(다른키도 상관없습니다) q를 누르면 나갑니다

또한 로비등에서 창변화에 따른 화면이 깨지는 현상을 spacebar를 누르거나 창을 최소화하고 다시 최대화 하면 복구되도록 수정했습니다

mainframe.c qr.c qr.h ui.h를 모두 생성후
컴파일 커맨드
gcc -o mainframe mainframe.c qr.c -lncursesw -lz
