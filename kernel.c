#include "kernel.h"
#include "common.h"
typedef unsigned char uint8_t;
typedef unsigned int uint32_t; 
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];


/*
Context Switching. 
이는 프로세스간 실행 컨텍스를 스위칭

호출자가 저장한 레지스터를 스택에 저장하고 스택 포인터를 전환한 다음 호출자가 저장한 레지스터를 스택에서 복원한다. 

피호출자의 저장된 레지스터는 호출된 함수가 반환하기 전에 복원해야 하는 레지스터임
RISC-V에서 s0 ~ s11은 호출자 저장 레지스터이다.
a0과 같은 다른 레지스터는 호출자 저장 레지스터로, 호출자가 스택에 이미 저장해 둔다.
https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
*/
__attribute__((naked)) void switch_context(uint32_t *prev_sp, uint32_t *next_sp){
    __asm__ __volatile__(
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
        "sw ra, 0 * 4(sp)\n" // 피호출자의 레지스터를 저장한다.
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"
        "sw sp, (a0)\n" // *prev_sp = sp;
        "lw sp, (a1)\n" // switch stack pointer (sp) here
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n" // 13 * 4 바이트 스택에서 정리
        "ret\n" // 여기서 ret가 없어지면 ? 어케?
    );
}

struct process procs[PROCS_MAX]; //All process control structures

struct process *create_process(uint32_t pc){
    printf("call create_process\n");
    // Find an unused process control structure.
    struct process *proc = NULL; 
    int i; 
    for (i = 0;i < PROCS_MAX; i++){
        if (procs[i].state == PROC_UNUSED){
            proc = &procs[i];
            break;
        }
    }

    if(!proc){
        PANIC("no free process slots");
    }

    uint32_t *sp = (uint32_t *)&proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t )pc;          // ra

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t )sp;
    return proc;
}

/*
Exception의 라이프
1. CPU는 medelg 레지스터를 확인하여 예외를 처리할 동작 모드를 결정한다. 
OpenSBI는 이미 S-Mode의 핸들러에서 U-Mode / S-Mode 예외를 처리하도록 구성한다.

2. CPU는 자신의 상태(레지스터)를 다양한 CSR에 저장한다.
scause : 예외 유형, 커널은 이를 읽고 예외 유형을 식별한다.
stval : 예외에 대한 추가 정보(예외를 발생시킨 메모리 주소), 예외 유형에 따라 다름
sepc : 예외가 발생한 지점의 프로그램 카운터
sstatus : 예외가 발생한 경우 작동 모드(u or s)

3. stvec 레지스터의 값은 프로그램 카운터로 설정되어 커널의 예외 처리기로 이동한다.

4. 예외 처리기는 범용 레지스터(즉, 프로그램 상태)를 저장하고 예외를 처리함

5. 완료되면 예외 처리기는 저장된 실행 상태를 복원하고 sret 명령을 호출하여 예외가 발생한 지점부터 실행을 재개한다. 
*/

/*
sscratch 레지스터는 예외 발생시 스택포인터를 저장하는 임시 저장소로 사용, 나중에 복원

부동소수점 레지스터는 커널 내에서 사용되지 않으므로 여기에 저장할 필요가 없음. 일반적으로 스레드 전환 중에 저장되고 복원됨

스택포인터가 a0 레지스터에 설정되고 handle_trap 함수가 호출됨. 이떄 스택포인터가 가리키는 주소에는 나중에 설명하는 trap_frame 구조와 동일한 구조에 저장된 레지스터 값이 포함됨

__attribute((align(4)))을 추가하면 함수의 시작 주소가 4바이트 경계에 정렬됨
이는 stvec 레지스터가 예외 핸들러의 주소를 보유할 뿐만 아니라 하단 2비트에 모드를 나타내는 플래그를 가지고 있음

기존의 예외처리기는 실행 상태를 스택에 저장함. 
하지만 이제 각 프로세스마다 별도의 커널 스택을 사용하기 떄문에 프로세스 전환 시 sscratch 레지스터에 현재 실행중인 프로세스의 커널 스택 초기값을 설정
*/

/*
    csrrw 명령어
    tmp = sp; 
    sp = sscratch;
    sscratch = tmp;

    sp는 현재 실행 중인 프로세스의 커널(사용자 스택이 아닌) 스택을 가리킨다. 예외가 발생한 시점의 sp(사용자 스택)의 원래 값을 sscratch가 보유한다.
    다른 레지스터를 커널 스택에 저장한 후, 원래 sp 값을 복원하고 이를 커널 스택에 저장. 그런 다음 sscratch의 초기 값을 계산하여 복원.
    여기서 중요한 점은 프로세스가 독립적인 커널 스택을 가진다는 것. 컨텍스트 전환 중에 sscratch의 내용을 바꾸면 아무 일도 없었다는 것처럼 프로세스가 중단된 지점부터 실행을 재개할 수 있음
*/

/*
    sscratch를 조정하여 커널 스택으로 전환해야 하는 이유
    예외 발생 시 스택포인터를 신뢰해서는 안된다. 예외 처리기에는 다음 3가지 패턴을 고려해야딤
    1. 커널 모드에서 예외가 발생한 경우
    2. 커널모드에서 다른 예외(중첩 예외)를 처리할 때 예외가 발생한 경우
    3. 사용자 모드에서 예외가 발생한 경우 

    1번의 경우 스택 포인터를 재설정하지 않아도 문제 없음
    2번의 경우 저장된 영역을 덮어쓰지만, 중첩 예외에 대한 커널 패닉을 트리거 하는 구현이므로 괜찮음

    3번의 경우 sp는 사용자(어플리케이션) 스택 영역을 가리킴. sp를 그대로 사용(신뢰)하도록 구현하면 커널이 충돌하는 취약점이 발생할 수 있음
    만약 sscratch를 조정하지 않는다면 예외 발생시 스택 포인터가 매핑되지 않은 주소(userland)를 가리키기 때문에 이에 대한 또다른 예외를 불러옴
    -> 무한루프로 커널 중단 -> 즉, 신뢰할 수 있는 스택 영역을 처음부터 다시 가져와야 함
*/
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void){
    __asm__ __volatile__(
        // 실행 중인 프로세스의 커널 스택을 처음부터 다시 가져온다.
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra, 4 * 0(sp)\n"
        "sw gp, 4 * 1(sp)\n"
        "sw tp, 4 * 2(sp)\n"
        "sw t0, 4 * 3(sp)\n"
        "sw t1, 4 * 4(sp)\n"
        "sw t2, 4 * 5(sp)\n"
        "sw t3, 4 * 6(sp)\n"
        "sw t4, 4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra, 4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}
// sret:: 트랩 핸들러에서 반환(프로그램 카운터, 작동 모드 등 복원)
/*
https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400
The RISC-V Reader 참조
Supervisor-Mode Exception handler에서 Return한다. 
PC(Program Counter)를 CSRs(sepc)에 저장하고 privilege mode를 CSRs[sstatus].SPP, 
CSRs(sstatus).SIE를 CSRs[sstatus].SPIE에 저장, CSR[sstatus].SPIE를 1로 세팅 그리고 CSPs[sstatus].SPP를 0으로 세팅
QEMU에서는 mstatus를 sstatus대신 사용
*/


void handle_trap(struct trap_frame *f){
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    /*
        scause가 2이면 프로그램이 잘못된 명령어를 실행하려고 시도했음을 의미 unimp의 예상동작
        sepc의 값은 unimp 명령어가 호출되는 줄을 가리킴
    */
    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}

/*
링커 스크립트의 ALIGN(4096)으로 인해 __free_ram은 4KB 경계에 배치됨.
따라서 alloc_pages 함수는 항상 4KB에 정렬된 주솔르 반환한다. __free_ram_end를 초과해서 할당을 시도하면, 메모리 부족으로 커널 패닉 발생

메모리 해제를 위해서는 비트맵 기반 알고리즘이나 버디시스템 알고리즘 사용
 */
paddr_t alloc_pages(uint32_t n){
    // 정적 변수로 정의함. 전역 변수 처럼 동작
    // 할당할 다음 영역의 시작 주소를 가리킨다. 할당할 때 next_paddr은 할당되는 크기만큼 전진
    // next_paddr은 처음 __free_ram의 주소를 보유. 즉, 메모리는 __free_ram 부터 순차적으로 할당된다.
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if(next_paddr > (paddr_t) __free_ram_end){
        PANIC("out of memory");
    }

    memset((void *)paddr, 0, n * PAGE_SIZE);
    return paddr;
}

/*
모든 SBI 함수는 하나의 바이너리 인코딩을 공유하므로 SBI 확장을 쉽게 혼합할 수 있다. 
SBI 사양은 아래의 호출 규칙을 따른다.

ECALL은 컨트롤 트랜스퍼 명령어로 쓰이며 supervisor와 SEE사이에서 쓰임
a7은 SBI extension ID (EID)
a6는 SBI v0.2 이후에 정의된 모든 SBI 확장에 대해 a7로 인코딩된 주어진 확장 ID에 대한 SBI 기능 ID(FID)를 인코딩

https://tools.cloudbear.ru/docs/riscv-sbi-2.0-20231006.pdf- Chapter 3
SBI 함수는 a0, a1 페어값을 반환한다. a0은 에러코드
*/
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid){
    /* 컴파이러에 지정된 레지스터에 값을 배치하도록 요청 */
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3; 
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    /* 
        ecall 명령어 실행
        CPU 실행 모드가 커널모드(S-MODE)에서 OpenSBI모드(M-MODE)로 전환됨.
        OpenSBI의 처리 핸들러가 호출됨. 이 작업이 완료되면 다시 커널모드로 전환되고 ecall 명령어 이후 실행이 재개
     */

    /* ecall 명령은 어플리케이션이 커널을 호출할 때(syscall) 사용되기도 함. 이 명령어는 더 높은 권한의 CPU 모드로의 함수 호출처럼 동작 */
    __asm__ __volatile__("ecall"
                        : "=r"(a0), "=r"(a1)
                        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                        "r"(a6), "r"(a7)
                        : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}
/*
    long sbi_console_putchar(int ch)
    ch에 있는 데이터를 콘솔 디버깅에 쓴다. sbi_console_getchar()와 달리 이 SBI 호출은 전송할 대기 중인 문자가 남아 있거나 수신 터미널이 아직 바이트를 수신할 준비가 되지 않은 경우 차단된다.
    그러나 콘솔이 전혀 존재하지 않으면 문자가 버려진다. 
    이 SBI 호출은 성공시 0을 반환하거나 구현에 따라 에러 코드를 반환
    -> 디버그 콘솔에 전송
*/
void putchar(char ch){
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

struct process *proc_a; 
struct process *proc_b;

/* 
    Scheduler
    switch_context 함수를 직접 호출하여 "다음에 실행할 프로세스"를 지정함.
    프로세스 수가 많아지면 선택에 있어 골치아픔 -> 스케줄러 구현
*/
struct process *current_proc; // Current running process
struct process *idle_proc; // Idle process (실행 가능한 프로세스가 없을 때 실행할 유휴 프로세스)

void yield(void){
    struct process *next = idle_proc;
    for(int i=0;i < PROCS_MAX; i++){
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if(proc->state == PROC_RUNNABLE && proc->pid > 0){
            next = proc; 
            break;
        }
    }

    // 실행할 수 있는 프로세스가 없다면
    if(next == current_proc){
        return;
    }

    // 스택포인터는 낮은 주소로 확장되므로 커널 스택의 초기 값으로 sizeof(next->stack)번째 바이트의 주소를 설정한다.
    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // Context Switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}

void proc_a_entry(void){
    printf("starting process A\n");
    while(1){
        putchar('A');
        yield();
    
        for(int i=0;i<30000000;i++){
            __asm__ __volatile__("nop");
        }
    }
}

void proc_b_entry(void){
    printf("starting process B\n");
    while(1){
        putchar('B');
        yield();

        for(int i=0;i<30000000;i++){
            __asm__ __volatile__("nop");
        }
    }
}

/*
Hello World의 라이프 
1. 커널이 ecall을 실행함. CPU는 M-mode 트랩 핸들러(mtvec 레지스터)로 점프한다.
2. 레지스터 저장후 트랩 핸들러 호출 (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263)
3. eid기반으로 상응하는 SBI 함수 호출 (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)
4. 디바이스 드라이버(8250 UART)는 QEMU에 문자를 전송한다. (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77)
5. QEMU의 8250 UART 에뮬레이션 구현은 문자를 수신하여 표준 출력으로 전송
*/

void kernel_main(void){
   /*
    printf("\n\nHello %s\n", "Printf!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);


    for (;;){
        __asm__ __volatile__("wfi");
    }
    */

    memset(__bss, 0, (size_t)__bss_end - (size_t) __bss);
    printf("\n\nHello Kernel\n");

    // stvec 레지스터에 예외 처리기의 주소를 저장한다.
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    /*
        https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases
        csrrw x0, cycle, x0. 
        cycle은 read-only CSR임, 이 CSR의 존재 여부와 관계없이 여기에 쓰기를 시도하면 잘못된 명령어 예외가 생성됨 
    */
 
   //__asm__ __volatile__("unimp"); 

    /*
        current_proc = idle_proc를 통해 부팅 프로세스의 실행 컨텍스트가 유휴 프로세스의 실행 컨텍스트로 저장되고 복원된다.
        반환 함수를 처음 호출하는 동안 유휴 프로세스에서 프로세스 A로 전환하고, 다시 유휴 프로세스로 전환할 때는 이 반환 함수 호출에서 반환하는 것처럼 동작한다.
    */
    idle_proc = create_process((uint32_t)NULL);
    idle_proc->pid = -1; // IDLE
    current_proc = idle_proc;

    paddr_t paddr0 = alloc_pages(2);
    paddr_t paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=%x\n", paddr0);
    printf("alloc_pages test: paddr1=%x\n", paddr1);

    proc_a = create_process((uint32_t)proc_a_entry);
    proc_b = create_process((uint32_t)proc_b_entry);

    yield();
    PANIC("booted!");
}


/*
커널 실행은 링커 스크립트에서 진입 지점으로 지정된 부팅 함수에서 시작된다. 
이 함수에서 스택포인터(sp)는 링커 스크립트에 정의된 스택 영역의 끝 주소로 설정됨
그리고 kernel_main함수로 이동 
스택은 0을 향해 증가하므로 사용할 수록 줄어듬
즉, 스택 영역의 끝 주소(시작 주소가 아님)를 설정해야 함
 */

/*
부팅함수에는 링커 스크립트에서 함수의 위치를 제어하는 속성이 존재
OpenSBI는 진입점을 알 지 못한채 0x80200000으로 점프하기 때문에 부팅함수는 0x80200000에 배치해야 됨
*/

/*
컴파일러가 함수 본문 앞뒤에 반환 명령어와 같은 불필요한 코드를 생성하지 않도록 지시
*/
__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void){
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n" // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // stack top 주소를 %[stack_top]에 넘김
    );
}