#!/bin/bash
set -xue

# QEMU file path
QEMU=/opt/homebrew/bin/qemu-system-riscv32

CC=/opt/homebrew/opt/llvm/bin/clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32 -ffreestanding -nostdlib"

# new: Build the Kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c

# -machine virt: virt 머신을 시작
# -bios default: 기본 펌웨어(이 경우 OpenSBI)를 사용
# -nographic: GUI 창 없이 QEMU를 시작
# -serial mon:stdio: QEMU의 표준 입출력을 가상 머신의 직렬포트에 연결
# --no-reboot: 가상 머신이 충돌하면 재부팅하지 않고 에뮬레이터를 중지
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -kernel kernel.elf # new: Load the kernel