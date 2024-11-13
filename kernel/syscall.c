#include "syscall.h"

__attribute__((section(".init_text"))) void init_syscall(void){
    WRMSR(0,((0x18UL<<16)|0x8UL),IA32_STAR);       \\edx[bit0-bit15]=ring0 64位代码段选择子cs,栈段ss+8  edx[bit16-bit31]=ring3
    WRMSR((UINT64)syscall_entry&0xFFFFFFFFUL,(UINT64)syscall_entry>>32,IA32_LSTAR);
    WRMSR(0,0,IA32_FMASK);

    return;
}