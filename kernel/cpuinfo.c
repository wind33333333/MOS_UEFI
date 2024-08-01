#include "cpuinfo.h"


void get_cpuinfo(unsigned int *cpu_id,unsigned char *bsp_flags) {

    // 获取当前CPU id号
    __asm__ __volatile__ (
            "movl       $0x802,%%ecx   \n\t"
            "rdmsr                     \n\t"
            :"=a"(*cpu_id)::"%rcx", "%rdx");

    // 获取当前CPU的flags
    __asm__ __volatile__( \
            "movl    $0x1B,%%ecx  \n\t"          /*IA32_APIC_BASE=0x1b 寄存器*/
            "rdmsr                \n\t"         //bit10 X2APIC使能   bit11 APIC全局使能
            "shr     $8,%%eax      \n\t"
            "and     $1,%%eax      \n\t"
            :"=a"(*bsp_flags)::);

    if (*bsp_flags) {
        // 获取CPU厂商
        __asm__ __volatile__(
                "xor    %%eax, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%ebx, (%%rdi) \n\t"
                "mov    %%edx, 4(%%edi)\n\t"
                "mov    %%ecx, 8(%%edi) \n\t"
                "movb   $0, 12(%%edi) \n\t"
                ::"D"(&cpu_info.manufacturer_name):"%rax", "%rbx", "%rcx", "%rdx");

        // 获取CPU核心数量
        __asm__ __volatile__(
                "mov        $1,%%eax    \n\t"
                "cpuid                  \n\t"
                "shr        $16,%%ebx   \n\t"         //右移16位得到cpu数量
                :"=b"(cpu_info.cores_num)::"%rax", "%rcx", "%rdx");

        // 获取CPU型号
        __asm__ __volatile__(
                "mov    $0x80000002, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, (%%rdi)   \n\t"
                "mov    %%ebx, 4(%%rdi)  \n\t"
                "mov    %%ecx, 8(%%edi)  \n\t"
                "mov    %%edx, 12(%%edi) \n\t"

                "mov    $0x80000003, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, 16(%%rdi)   \n\t"
                "mov    %%ebx, 20(%%rdi)  \n\t"
                "mov    %%ecx, 24(%%edi)  \n\t"
                "mov    %%edx, 28(%%edi) \n\t"

                "mov    $0x80000004, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, 32(%%rdi)   \n\t"
                "mov    %%ebx, 36(%%rdi)  \n\t"
                "mov    %%ecx, 40(%%edi)  \n\t"
                "mov    %%edx, 44(%%edi) \n\t"

                "mov    $0, 48(%%edi) \n\t"
                ::"D"(&cpu_info.model_name):"%rax", "%rbx", "%rcx", "%rdx");

        // 获取CPU频率
        __asm__ __volatile__(
                "mov    $0x16, %%eax \n\t"
                "cpuid         \n\t"
                "shl    $32,%%rdx  \n\t"
                "or     %%rdx,%%rax \n\t"
                :"=a"(cpu_info.fundamental_frequency), "=b"(cpu_info.maximum_frequency), "=c"(cpu_info.bus_frequency)::"%rdx");

        // 获取CPU TSC频率
        __asm__ __volatile__(
                "mov    $0x15,%%eax  \n\t"
                "cpuid               \n\t"
                "test   %%ecx,%%ecx  \n\t"
                "jz     .1           \n\t"            //如果ecx等于0则获取到的tsc频率无效
                "xchg   %%rax,%%rbx  \n\t"
                "mul    %%rcx        \n\t"
                "div    %%rbx        \n\t"
                ".1:                 \n\t"
                :"=a"(cpu_info.tsc_frequency)::"%rcx", "%rbx", "%rdx");

    }

    return;
}

