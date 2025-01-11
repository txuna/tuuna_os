#pragma once

#include "common.h"

/*
trap_frame 구조체는 kernel_entry에 저장된 프로그램 상태를 나타냄. 
READ_CSR, WRITE_CSR 매크로는 CSR 레지스터를 읽고 쓰기 위한 편리한 매크로임
*/
struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));


/*
CSR(제어 및 상태 레지스터)는 CPU 설정을 저장하는 레지스터, CSR 목록은 RISC-V 권한 사양에서 확인 가능
*/
#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)

struct sbiret {
    long error;
    long value;
};

#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)

#define PROCS_MAX 8
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1

/*
커널 스택에는 저장된 CPU 레지스터, 반환 주소(호출된 위치), 로컬 변수가 포함되어 있음
각 프로세스에 대한 커널 스택을 준비하면 CPU 레지스터를 저장 및 복원하고 스택 포인터를 전환하여 컨텍스트 전환을 구현할 수 있음
*/
struct process {
    int pid;             // 프로세스 아이디
    int state;         // 프로세스 상태 
    vaddr_t sp;         // 스택 포인터
    uint8_t stack[8192]; // 커널 스택
};
/*
단일 커널 스택이라는 또다른 접근 방식존재
각 프로세스(스레드)에 커널 스택이 있는 대신 CPU당 스택이 하나만 있음
seL4는 이 방식을 채택. "where to store the program's context" 문제는 Go, Rust와 같은 프로그래밍 언어의 비동기 런타임에서 논의되는 주제
=> stackless async 검색
*/