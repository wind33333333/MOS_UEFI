#pragma once
#include "idt.h"
#include "ecx_dispatcher.h"
#include "errno.h"
#include "printk.h"


/*********************************************************** 系统异常处理函数 *****************************************************************************/

void do_divide_error(cpu_registers_t *regs) {

    PR_ERROR( "do_divide_error(0),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_debug(cpu_registers_t *regs) {

    PR_ERROR( "do_debug(1),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_nmi(cpu_registers_t *regs) {
    (void)regs;
    // 1. 读取 0x61 端口，检查是不是最致命的硬件故障
    uint8 port_61 = asm_io_in8(0x61);

    if (port_61 & 0x80) {
        PR_ERROR( "FATAL: Memory Parity Error!\n");
        while(1) {
            asm_hlt();
        }; // 致命故障，直接挂起
    }
    if (port_61 & 0x40) {
        PR_ERROR( "FATAL: Bus Error!\n");
        while(1) {
            asm_hlt();
        }; // 致命故障，直接挂起
    }

    // 2. 如果不是致命故障，大概率是 QEMU 或某些没屏蔽的定时器触发的
    // 直接忽略，让它返回，保持系统继续活下去！
    PR_WARN("Warning: Spurious NMI received. Ignoring.\n");
}


void do_int3(cpu_registers_t *regs) {

    PR_ERROR( "do_int3(3),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_overflow(cpu_registers_t *regs) {

    PR_ERROR( "do_overflow(4),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_bounds(cpu_registers_t *regs) {

    PR_ERROR( "do_bounds(5),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_undefined_opcode(cpu_registers_t *regs) {

    PR_ERROR( "do_undefined_opcode(6),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_dev_not_available(cpu_registers_t *regs) {

    PR_ERROR( "do_dev_not_available(7),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_double_fault(cpu_registers_t *regs) {

    asm_cli();
    PR_ERROR("do_double_fault(8),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    while (1) {
        asm_cli();
        asm_hlt();
    };
}


void do_coprocessor_segment_overrun(cpu_registers_t *regs) {

    PR_ERROR("do_coprocessor_segment_overrun(9),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_invalid_tss(cpu_registers_t *regs) {

    PR_ERROR( "do_invalid_TSS(10),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01) {
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");
    }

    if (regs->err_code & 0x02) {
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    }else {
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");
    }

    if ((regs->err_code & 0x02) == 0) {
        if (regs->err_code & 0x04) {
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        }else {
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");
        }
    }

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


void do_segment_not_present(cpu_registers_t *regs) {

    PR_ERROR("do_segment_not_present(11),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01) {
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");
    }

    if (regs->err_code & 0x02) {
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    }else {
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");
    }

    if ((regs->err_code & 0x02) == 0) {
        if (regs->err_code & 0x04) {
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        }else {
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");
        }
    }

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


void do_stack_segment_fault(cpu_registers_t *regs) {

    PR_ERROR("do_stack_segment_fault(12),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0) {
        if (regs->err_code & 0x04) {
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        }else {
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");
        }
    }

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


void do_general_protection(cpu_registers_t *regs) {

    PR_ERROR("do_general_protection(13),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0) {
        if (regs->err_code & 0x04) {
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        }else {
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");
        }
    }

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


void do_page_fault(cpu_registers_t *regs) {

    //uint64 *p = NULL;
    uint64 cr2 = asm_get_cr2();

    PR_ERROR( "do_page_fault(14),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (!(regs->err_code & 0x01))
        PR_ERROR( "Page Not-Present,\t");

    if (regs->err_code & 0x02)
        PR_ERROR( "Write Cause Fault,\t");
    else
        PR_ERROR( "Read Cause Fault,\t");

    if (regs->err_code & 0x04)
        PR_ERROR( "Fault in user(3)\t");
    else
        PR_ERROR( "Fault in supervisor(0,1,2)\t");

    if (regs->err_code & 0x08)
        PR_ERROR( ",Reserved Bit Cause Fault\t");

    if (regs->err_code & 0x10)
        PR_ERROR( ",Instruction fetch Cause Fault");

    PR_ERROR( "\n");

    PR_ERROR( "CR2:%#018lx\n", cr2);

        while (1) {
        asm_hlt();
    }
}


void do_x87_FPU_error(cpu_registers_t *regs) {

    PR_ERROR( "do_x87_FPU_error(16),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_alignment_check(cpu_registers_t *regs) {

    PR_ERROR( "do_alignment_check(17),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_machine_check(cpu_registers_t *regs) {

    PR_ERROR( "do_machine_check(18),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_SIMD_exception(cpu_registers_t *regs) {

    PR_ERROR( "do_SIMD_exception(19),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_virtualization_exception(cpu_registers_t *regs) {

    PR_ERROR("do_virtualization_exception(20),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}

/*******************************************************************************************************************************/


//前32个异常处理函数表
static exception_handler_f exception_table[32];

//异常分发  汇编exc_cmn_stub -> c_exception_dispatcher
void c_exception_dispatcher(cpu_registers_t *regs) {
    exception_handler_f exception = exception_table[regs->no];
    if (exception == NULL) {
        PR_ERROR("Exception Func NULL\n");
        while (1) {
            asm_hlt();
        }
    }
    exception(regs);
}



//异常函数注册函数
int32 register_exception(int32 vector, exception_handler_f handler) {
    if (vector < 0 || vector >= 32) return -EINVAL;

    // 异常是内核初始化早期设定的，通常不需要复杂的锁和状态机
    if (exception_table[vector] != NULL) {
        PR_ERROR("Exception %d is already registered!\n", vector);
        return -EBUSY;
    }

    exception_table[vector] = handler;
    return 0;
}

/**
 * @brief 卸载全局异常处理例程
 * @param vector 异常向量号 (0 ~ 31)
 */
int32 unregister_exception(int32 vector) {
    // 1. 严格的边界检查 (仅限 CPU 同步异常)
    if (vector < 0 || vector >= 32) {
        return -EINVAL;
    }

    // 2. 幂等性与安全性检查
    if (exception_table[vector] == NULL) {
        PR_ERROR("Exception %d is already NULL, nothing to unregister.\n", vector);
        return -ENODEV;
    }

    // 3. 卸载：直接清空函数指针
    // 异常是同步触发的，只要此时没有线程在故意触发该异常，直接置空即可
    exception_table[vector] = NULL;

    return 0;
}
