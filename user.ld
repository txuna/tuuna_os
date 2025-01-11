/* 애플리케이션을 메모리에 배치할 위치를 정의 */

ENTRY(start)

/*
    커널 링커 스크립트와 비슷하지만 차이점은 커널의 주소 공간과 겹치지 않도록 기본 주소(0x1000000)가 있음

    ASSERT는 첫 번째 인수와 조건이 충족되지 않으면 링컬르 중단
    애플리케이션 메모리의 끝인 .bss 섹션의 끝이 0x18000000을 초과하지 않는지 확인
    실행 파일 커지는것을 방지
*/
SECTIONS {
    . = 0x1000000;

    /* machine code */
    .text :{
        KEEP(*(.text.start));
        *(.text .text.*)
    }

    /* read only data */
    .rodata : ALIGN(4){
        *(.rodata .rodata.*);
    }

    /* data with initial values */
    .data : ALIGN(4){
        *(.data .data.*);
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(4){
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

        ASSERT(. < 0x1800000, "too large executable");
    }
}