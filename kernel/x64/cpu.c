#include "cpu.h"

uint64 cpu_possible_mask[CONFIG_NR_CPUS/64] = {0}; //可能存在的 CPU
uint64 cpu_present_mask[CONFIG_NR_CPUS/64] = {0};  //已插入的 CPU
uint64 cpu_online_mask[CONFIG_NR_CPUS/64] = {0};   //完全上线的 CPU

uint32 cpu_logicid_to_apicid[CONFIG_NR_CPUS] = {0xFFFFFFFF};  //cpu逻辑id转apicid

cpu_info_t cpu_info = {
    .ccb_ptr = NULL,      //运行时动态分配
    .run_cpu_count = 1,     //启动时只有bsp核心在运行，后面每个ap核心启动后自动修改
    .total_cpu_count = 1    //后面解析acpi表后修改cpu核心总数
};
