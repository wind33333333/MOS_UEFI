#include "syscall.h"
#include "cpu.h"

__attribute__((section(".init_text"))) void init_syscall(void){
    //执行syscall指令时 CS=IA32_STAR_MSR[bit32-bit47]&0xFFFC  SS=IA32_STAR_MSR[bit32-bit47]&0xFFFC+8
    //执行sysret执行返回64位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+16|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    //执行sysret执行返回32位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    WRMSR(IA32_STAR_MSR,((0x18UL<<48)|(0x8UL<<32)));

    //RIP=IA32_LSTAR_MSR  长模式系统调用入口地址
    WRMSR(IA32_LSTAR_MSR,long_mode_syscall_entry);

    //RIP=IA32_CSTAR_MSR  兼容模式系统调用入口地址
    //WRMSR(IA32_CSTAR_MSR,compatible_mode_syscall_entry);

    //内核RFLAGS=~IA32_FMASK_MSR&RFLAGS  置1进入内核后屏蔽对应的RFLAGS
    WRMSR(IA32_FMASK_MSR,0);

    return;
}