#include "interrupt.h"

#include "cpu.h"
#include "printk.h"
#include "moslib.h"
#include "../init/apic_init.h"
#include "errno.h"

/*********************************************************** 系统异常处理函数 *****************************************************************************/

int32 do_divide_error(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_divide_error(0),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_debug(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_debug(1),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


void do_nmi(cpu_registers_t *regs) {
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


int32 do_int3(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_int3(3),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_overflow(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_overflow(4),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_bounds(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_bounds(5),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_undefined_opcode(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_undefined_opcode(6),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_dev_not_available(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_dev_not_available(7),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_double_fault(cpu_registers_t *regs, void *dev_id) {
    asm_cli();
    PR_ERROR("do_double_fault(8),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    while (1) {
        asm_cli();
        asm_hlt();
    };
}


int32 do_coprocessor_segment_overrun(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR("do_coprocessor_segment_overrun(9),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_invalid_TSS(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_invalid_TSS(10),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        else
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


int32 do_segment_not_present(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR("do_segment_not_present(11),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        else
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


int32 do_stack_segment_fault(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR("do_stack_segment_fault(12),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        else
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


int32 do_general_protection(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR("do_general_protection(13),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        PR_ERROR("The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        PR_ERROR( "Refers to a gate descriptor in the IDT;\n");
    else
        PR_ERROR( "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            PR_ERROR( "Refers to a segment or gate descriptor in the LDT;\n");
        else
            PR_ERROR( "Refers to a descriptor in the current GDT;\n");

    PR_ERROR( "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

        while (1) {
        asm_hlt();
    }
}


int32 do_page_fault(cpu_registers_t *regs, void *dev_id) {
    uint64 *p = NULL;
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


int32 do_x87_FPU_error(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_x87_FPU_error(16),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_alignment_check(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_alignment_check(17),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_machine_check(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_machine_check(18),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_SIMD_exception(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR( "do_SIMD_exception(19),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_virtualization_exception(cpu_registers_t *regs, void *dev_id) {
    PR_ERROR("do_virtualization_exception(20),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}

/*******************************************************************************************************************************/


//MSI/MSI-X 专属 O(1) 独占路由表 (静态分配，快如闪电)
static irq_desc_t irq_table[256] = {0};

void exception_init() {
    register_isr(0,do_divide_error,NULL,"Divide Error");
    register_isr(1,do_debug,NULL,"Debug Exception");
    //2号 NMI: 不可屏蔽中断 (Non-Maskable Interrupt)，单独的中断入口
    register_isr(3,do_int3,NULL,"Breakpoint/INT3");
    register_isr(4,do_overflow,NULL,"Overflow/INTO");
    register_isr(5,do_bounds,NULL,"BOUND Range Exceeded");
    register_isr(6,do_undefined_opcode,NULL,"Undefined Opcode");
    register_isr(7,do_dev_not_available,NULL,"Device Not Available");
    register_isr(8,do_double_fault,NULL,"Double Fault");
    register_isr(9,do_coprocessor_segment_overrun,NULL,"Coprocessor Segment Overrun");
    register_isr(10,do_invalid_TSS,NULL,"Invalid TSS");
    register_isr(11,do_segment_not_present,NULL,"Segment Not Present");
    register_isr(12,do_stack_segment_fault,NULL,"Stack Segment Fault");
    register_isr(13,do_general_protection,NULL,"General Protection");
    register_isr(14,do_page_fault,NULL,"Page Fault");
    //15保留
    register_isr(16,do_x87_FPU_error,NULL,"x87 FPU Floating-Point Error");
    register_isr(17,do_alignment_check,NULL,"Alignment Check");
    register_isr(18,do_machine_check,NULL,"Machine Check");
    register_isr(19,do_SIMD_exception,NULL,"SIMD Exception");
    register_isr(20,do_virtualization_exception,NULL,"Virtualization Exception");
    // [21] - [31] 为 Intel 保留异常或较新的硬件异常 (如 21号 #CP 控制保护异常)，默认初始化为 NULL
}

void check_and_schedule();

/**
 * @brief 👑 C 语言大管家 (所有中断与异常的统一分发中心)
 */
void c_interrupt_dispatcher(cpu_registers_t *regs) {
    uint64 int_no = regs->int_no;

    // 1. O(1) 统一查表执行 (0~255)
    irq_desc_t *irq_desc = &irq_table[int_no];
    if (irq_desc->handler != NULL) {
        irq_desc->handler(regs, irq_desc->dev_id);
    } else {
        color_printk(YELLOW, BLACK, "Unhandled / Spurious Vector: %ld\n", int_no);
    }

    // ==========================================
    // 2. 外部硬件中断专属收尾：发送 EOI
    // ==========================================
    // 🌟 核心分水岭：仅限外设硬件中断 (>= 32)，且排除 APIC 伪中断 (255)
    if (int_no >= 32 && int_no != 255) {
        apic_send_eoi();
    }

    // ==========================================
    // 3. 调度抢占门禁
    // ==========================================
    check_and_schedule();
}



/**
 * @brief 在已分配的向量上挂载具体的处理例程
 * @param vector   之前 alloc_irq_vector 借到的号码
 * @param handler  中断服务例程
 * @param dev_id   设备上下文
 * @param name     驱动标识名
 */
int32 register_isr(int32 vector, irq_handler_f handler, void *dev_id, const char *name) {
    if (vector >= 256) return -EINVAL;

    if (vector >= 32) {
        // ★ 防火墙 1：不准在没买过的地皮上盖房！
        if (irq_table[vector].state == IRQ_STATE_FREE) {
            PR_ERROR( "IRQ %d is NOT allocated!\n", vector);
            return -EPERM;
        }

        // ★ 防火墙 2：不准在已经盖好房子的地皮上重复盖房！
        if (irq_table[vector].state == IRQ_STATE_REGISTERED) {
            PR_ERROR( "IRQ %d is already registered to %s!\n", vector, irq_table[vector].name);
            return -EBUSY;
        }
    }

    // 状态推进：正式服役
    irq_table[vector].state = IRQ_STATE_REGISTERED;
    irq_table[vector].handler = handler;
    irq_table[vector].dev_id = dev_id;
    irq_table[vector].name = name;

    return 0;
}

/**
 * @brief 注销中断服务例程 (拆除业务，但保留向量分配状态)
 * @param vector 要注销的中断向量号
 * @return 0 成功，负数表示失败
 */
int32 unregister_isr(int32 vector) {
    // 1. 基础物理边界防御
    if (vector < 32 || vector >= 256) return -EINVAL;

    // ★ 防火墙 1：防误杀！如果是一块连地皮都没买过的空地，直接报错
    if (irq_table[vector].state == IRQ_STATE_FREE) {
        PR_ERROR( "IRQ %d is completely free, nothing to unregister!\n", vector);
        return -EPERM;
    }

    // ★ 防火墙 2：防空拆！如果这块地只是买下来了，还没盖房子，也不需要拆
    if (irq_table[vector].state == IRQ_STATE_ALLOCATED) {
        color_printk(YELLOW, BLACK, "IRQ %d is allocated but no ISR registered.\n", vector);
        return 0; // 某种意义上不算致命错误，可以直接返回成功或特定警告码
    }

    // 2. 状态倒退：正式退役 (REGISTERED -> ALLOCATED)
    // 核心架构思想：仅仅是拆掉业务逻辑，坑位（Vector）依然归当前设备所有！
    irq_table[vector].state = IRQ_STATE_ALLOCATED;

    // 3. 清理残留指针，防止大管家 (Dispatcher) 误调引发野指针蓝屏
    irq_table[vector].handler = NULL;
    irq_table[vector].dev_id  = NULL;
    irq_table[vector].name    = NULL; // 配合你的指针设计，直接置空

    return 0;
}

/**
 * @brief 分配连续且对齐的中断向量 (专供纯 MSI 多消息使用)
 * @param count            申请的数量 (必须是 2 的幂：1, 2, 4, 8, 16, 32)
 * @param out_base_vector  [输出] 成功时写入基准向量号
 * @return 0 成功，负数失败
 */
int32 alloc_contiguous_irq(uint8 count) {
    if (count == 0 || count > 32) return -EINVAL;
    
    // 确保 count 是 2 的整数次幂 (位运算魔法)
    if ((count & (count - 1)) != 0) return -EINVAL;

    // 从 32 开始找，直到 255 - count
    for (int32 i = 32; i <= 256 - count; i++) {
        
        // ★ 核心物理铁律：基准向量必须向 count 对齐！
        // 如果 count = 4，基准向量必须是 40, 44, 48...
        if ((i % count) != 0) {
            continue; 
        }

        // 检查这连续的 count 个位置是否全都是 FREE
        boolean all_free = TRUE;
        for (uint8 j = 0; j < count; j++) {
            if (irq_table[i + j].state != IRQ_STATE_FREE) {
                all_free = FALSE;
                break;
            }
        }

        // 如果找到了这块完美的“风水宝地”
        if (all_free) {
            // 批量占坑
            for (uint8 j = 0; j < count; j++) {
                irq_table[i + j].state = IRQ_STATE_ALLOCATED;
            }
            return i; // 成功
        }
    }

    return -ENOSPC; // 碎片化太严重，找不到连续对齐的空间了
}

/**
 * @brief 释放连续的中断向量 (归还给中断池)
 * @param base_vector  之前分配得到的基准向量号 (起始号)
 * @param count        当初申请的数量 (2, 4, 8, 16, 32)
 */
void free_contiguous_irq(uint8 base_vector, uint8 count) {
    // 1. 基础物理边界检查
    if (base_vector < 32 || (uint16)base_vector + count > 256) {
        PR_ERROR( "IRQ: Attempt to free invalid vector range [%d, %d)\n",
                     base_vector, base_vector + count);
        return;
    }

    // 2. 逐个抹除状态，回归池中
    for (uint8 i = 0; i < count; i++) {
        uint8 v = base_vector + i;

        // 如果这里已经是 FREE 状态，说明驱动程序逻辑有误（重复释放）
        if (irq_table[v].state == IRQ_STATE_FREE) {
            color_printk(YELLOW, BLACK, "Warning: IRQ %d is already FREE!\n", v);
            continue;
        }

        // ★ 核心：执行彻底的状态回退
        irq_table[v].state   = IRQ_STATE_FREE;
        irq_table[v].handler = NULL;
        irq_table[v].dev_id  = NULL;
        irq_table[v].name = NULL; // 清空深拷贝的名称
    }

    // 此时这块连续的 Vector 空间已经可以被下一个 MSI 设备重新申请了
}

/**
 * @brief 分配一个空闲的中断向量号 (不挂载业务)
 * @return 成功返回 Vector 号 (32~255)，失败返回 -EAGAIN
 */
int32 alloc_irq(void) {
    // 32开始找
    for (uint8 i = 32; i < 256; i++) {
        if (irq_table[i].state == IRQ_STATE_FREE) {
            irq_table[i].state = IRQ_STATE_ALLOCATED; // 👑 先占坑，宣示主权
            return i;
        }
    }
    return -EAGAIN; // 向量资源池枯竭
}

/**
 * @brief 释放中断向量
 */
void free_irq(int32 vector) {
    if (vector >= 32) {
        irq_table[vector].state = IRQ_STATE_FREE;
        irq_table[vector].handler = NULL;
        irq_table[vector].dev_id = NULL;
        irq_table[vector].name = NULL;
    }
}






