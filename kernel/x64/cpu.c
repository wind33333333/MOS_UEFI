#include "cpu.h"


// 定义为一个动态指针，而不是固定数组！
cpu_core_t *cpu_cores = NULL;
uint32 active_cpu_count = 0;

