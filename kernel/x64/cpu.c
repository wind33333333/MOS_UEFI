#include "cpu.h"


cpu_info_t cpu_info = {
    .per_cpus = NULL,      //运行时动态分配
    .run_cpu_count = 1,     //启动时只有bsp核心在运行，后面每个ap核心启动后自动修改
    .total_cpu_count = 1    //后面解析acpi表后修改cpu核心总数
};

