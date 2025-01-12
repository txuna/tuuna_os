#include "../include/user/user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void){
    syscall(SYS_EXIT, 0, 0, 0);
    for(;;);
}

/*
ecall 명령어는 커널에 처리를 위임하는데 사용하는 특수 명령어이다. 
ecall 명령어가 실행되면 예외 핸들러가 호출되고 제어권이 커널로 전송된다.
커널의 반환 값은 a0레지스터에 설정된다.
*/
int syscall(int sysno, int arg0, int arg1, int arg2) {
    register int a0 __asm__("a0") = arg0;
    register int a1 __asm__("a1") = arg1;
    register int a2 __asm__("a2") = arg2;
    register int a3 __asm__("a3") = sysno;

    __asm__ __volatile__("ecall"
                         : "=r"(a0)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                         : "memory");

    return a0;
}

int readfile(const char *filename, char *buf, int len){
    return syscall(SYS_READFILE, (int) filename, (int) buf, len);
}

int writefile(const char *filename, const char *buf, int len){
    return syscall(SYS_WRITEFILE, (int) filename, (int) buf, len);
}

void putchar(char ch){
    syscall(SYS_PUTCHAR, ch, 0, 0);
}

int getchar(void){
    return syscall(SYS_GETCHAR, 0, 0, 0);
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