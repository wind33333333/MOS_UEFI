#include "syscall.h"
#include "cpu.h"

INIT_TEXT void init_syscall(void){
    //执行syscall指令时 CS=IA32_STAR_MSR[bit32-bit47]&0xFFFC  SS=IA32_STAR_MSR[bit32-bit47]&0xFFFC+8
    //执行sysret执行返回64位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+16|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    //执行sysret执行返回32位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    wrmsr(IA32_STAR_MSR,(0x18UL<<48)|(0x8UL<<32));

    //RIP=IA32_LSTAR_MSR  长模式系统调用入口地址
    wrmsr(IA32_LSTAR_MSR,(UINT64)long_mode_syscall_entry);

    //RIP=IA32_CSTAR_MSR  兼容模式系统调用入口地址
    //wrmsr(IA32_CSTAR_MSR,(UINT64)compatible_mode_syscall_entry);

    //内核RFLAGS=~IA32_FMASK_MSR&RFLAGS  置1进入内核后屏蔽对应的RFLAGS
    wrmsr(IA32_FMASK_MSR,0);
}