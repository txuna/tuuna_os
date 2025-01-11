#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void){
    for(;;);
}

void putchar(char c){

}

/*
    어플리케이션의 실행은 start함수에서 시작됨
    커널의부팅 프로세스와 비슷하게 스택 포인터를 설정하고 main함수를 호출함

    커널과 달리 bss섹션을 0으로 초기화 하지 않는 이유는 이미 커널에서 초기화를 진행했기 때문에디ㅏ.(alloc_pages)
*/
__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top] \n"
        "call main \n"
        "call exit \n"
        :: [stack_top] "r" (__stack_top)
    );
}