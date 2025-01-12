#include "user.h"

void main(void){
    /*
        아래 주소는 페이지 테이블에 매핑된 커널 주소이다. page fault 유도
        해당 페이지에는 PAGE_U 비트가 없음 
        scause = 0xf는 Store/AMO page fault
    */
    //*((volatile int *) 0x80200000) = 0x1234;
    while(1){
prompt:
        printf("> ");
        char cmdline[128];
        for(int i=0;;i++){
            char ch = getchar();
            putchar(ch);

            if(i == sizeof(cmdline) - 1){
                printf("command line to long\n");
                goto prompt;
                // debug console에서 newline은 \r이다.
            } else if (ch == '\r'){
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if(strcmp(cmdline, "hello") == 0){
            printf("Hello World from shell!\n");
        } else if (strcmp(cmdline, "exit") == 0) {
            exit();
        } else {
            printf("unknown command: %s\n", cmdline);
        }
    }
}