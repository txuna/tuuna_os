#!/bin/bash
set -xue

OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy
# QEMU file path
QEMU=/opt/homebrew/bin/qemu-system-riscv32

CC=/opt/homebrew/opt/llvm/bin/clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32 -ffreestanding -nostdlib"

# c 파일을 컴파일하고 user.ld 링커 스크립트와 연결
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf \
    shell.c user.c common.c
# 실행파일 elf를 원시 바이너리로 변환 
# 원시 바이너리는 기본 주소(0x1000000)에서 메모리로 확장되는 실제 콘텐츠. 
# OS는 원시 바이너리의 내용을 복사하는 것만으로 애플리케이션을 메모리에 준비할 수 있음
# 일반적인 OS는 메모리 내용과 매핑 정보가 분리된 ELF와 같은 형식을 사용 -> 단순화를 위해 원시 바이너리 사용
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin

# 원시 바이너리 실행 이미지를 C 언어에 임베드할 수 잇는 형식으로 변환 llvm-nm 명령을 사용하려 내부 확인 가능
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# new: Build the Kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o

# -machine virt: virt 머신을 시작
# -bios default: 기본 펌웨어(이 경우 OpenSBI)를 사용
# -nographic: GUI 창 없이 QEMU를 시작
# -serial mon:stdio: QEMU의 표준 입출력을 가상 머신의 직렬포트에 연결
# --no-reboot: 가상 머신이 충돌하면 재부팅하지 않고 에뮬레이터를 중지
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -kernel kernel.elf # new: Load the kernel