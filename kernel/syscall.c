#include "syscall.h"
#include "cpu.h"

__attribute__((section(".init_text"))) void init_syscall(void){
    //执行syscall指令时 CS=IA32_STAR_MSR[bit32-bit47]&0xFFFC  SS=IA32_STAR_MSR[bit32-bit47]&0xFFFC+8
    //执行sysret执行返回64位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+16|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    //执行sysret执行返回32位代码段时 CS=IA32_STAR_MSR[bit48-bit63]&0xFFFC|3  SS=IA32_STAR_MSR[bit48-bit63]&0xFFFC+8|3
    WRMSR(0,((0x18UL<<16)|0x8UL),IA32_STAR_MSR);

    //RIP=IA32_LSTAR_MSR  64用户程序系统调用入口地址
    WRMSR((UINT64)syscall_entry&0xFFFFFFFFUL,(UINT64)syscall_entry>>32,IA32_LSTAR_MSR);

    //RIP=IA32_CSTAR_MSR  32用户程序系统调用入口地址
    WRMSR((UINT64)syscall_entry&0xFFFFFFFFUL,(UINT64)syscall_entry>>32,IA32_CSTAR_MSR);

    //内核RFLAGS=~IA32_FMASK_MSR&RFLAGS  置1进入内核后屏蔽对应的RFLAGS
    WRMSR(0,0,IA32_FMASK_MSR);

    return;
}