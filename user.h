#pragma once
#include "common.h"

int syscall(int sysno, int arg0, int arg1, int arg2);
__attribute__((noreturn)) void exit(void);
void putchar(char ch);
int getchar(void);