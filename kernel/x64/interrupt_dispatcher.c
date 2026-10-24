#include "interrupt_dispatcher.h"
#include "cpu.h"
#include "printk.h"
#include "moslib.h"
#include "../init/apic_init.h"
#include "errno.h"
#include "../sched/sched.h"

/*********************************************************** 系统异常处理函数 *****************************************************************************/

int32 do_divide_error(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_divide_error(0),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_debug(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_int3(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_int3(3),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_overflow(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_overflow(4),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_bounds(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_bounds(5),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_undefined_opcode(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_undefined_opcode(6),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_dev_not_available(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_dev_not_available(7),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_double_fault(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    asm_cli();
    PR_ERROR("do_double_fault(8),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    while (1) {
        asm_cli();
        asm_hlt();
    };
}


int32 do_coprocessor_segment_overrun(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR("do_coprocessor_segment_overrun(9),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_invalid_tss(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_segment_not_present(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_stack_segment_fault(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_general_protection(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_page_fault(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
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


int32 do_x87_FPU_error(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_x87_FPU_error(16),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_alignment_check(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_alignment_check(17),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_machine_check(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_machine_check(18),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_SIMD_exception(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR( "do_SIMD_exception(19),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}


int32 do_virtualization_exception(cpu_registers_t *regs, void *dev_id) {
    (void)dev_id;
    PR_ERROR("do_virtualization_exception(20),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
        while (1) {
        asm_hlt();
    }
}

/*******************************************************************************************************************************/

//全局中断描述符表
RODATA __attribute__((aligned(4096))) const idt_t idt = {0};

static exception_handler_f exception_table[32];

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

//异常分发
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

//中断分发
void c_irq_dispatcher(cpu_registers_t *regs) {
    // 1. 过滤虚假中断
    if (regs->no == APIC_SPURIOUS_VECTOR) return;

    // 2. 映射为 0~223 的索引
    uint64 irq_no = regs->no - 32;

    // 3. 🌟 O(1) 极速本地查表 (全程在本地 L1 Cache 内完成)
    irq_desc_t *irq_desc = &this_cpu()->irq_table[irq_no];

    if (irq_desc->handler != NULL) {
        // 传递 regs 和 驱动注册时的私有 dev_id (如网卡对象指针)
        irq_desc->handler(regs, irq_desc->dev_id);
    } else {
        color_printk(YELLOW, BLACK, "CPU %d Unhandled Vector: %ld\n",
                     THIS_CPU->logical_id, irq_no);
    }

    // 4. 发送 EOI，解除 APIC 锁定
    apic_send_eoi();
}


/**
 * @brief 在指定 CPU 的向量上挂载具体的处理例程 (硬件中断专属)
 * @param target_cpu_id 目标 CPU 的逻辑 ID
 * @param vector        之前 alloc_irq_vector 借到的号码 (32~255)
 * @param handler       中断服务例程
 * @param dev_id        设备上下文
 * @param name          驱动标识名
 */
int32 register_irq(uint32 target_cpu_id, int32 vector, irq_handler_f handler, void *dev_id, const char *name) {
    // 1. 参数合法性校验
    if (vector < 32 || vector >= 256 || target_cpu_id >= active_cpu_count) return -EINVAL;

    // 2. 获取目标 CPU 的控制块指针 (核心：跨 CPU 寻址！)
    cpu_core_t *target_cpu = cpu_lid_to_core(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    // 3. 计算在 target_cpu->irq_table 中的实际索引 (0 ~ 223)
    uint32 irq_index = vector - 32;

    // 4. 🌟 跨核修改防护：获取目标 CPU 中断表的自旋锁 (保存中断状态)
    uint64 flags;
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    irq_desc_t *desc = &target_cpu->irq_table[irq_index];

    // ★ 防火墙 1：不准在没买过的地皮上盖房！
    if (desc->state == IRQ_STATE_FREE) {
        spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
        PR_ERROR("CPU %u IRQ %d is NOT allocated!\n", target_cpu_id, vector);
        return -EPERM;
    }

    // ★ 防火墙 2：不准在已经盖好房子的地皮上重复盖房！
    if (desc->state == IRQ_STATE_REGISTERED) {
        spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
        PR_ERROR("CPU %u IRQ %d is already registered to %s!\n", target_cpu_id, vector, desc->name);
        return -EBUSY;
    }

    // 5. 状态推进：正式服役
    desc->handler = handler;
    desc->dev_id  = dev_id;
    desc->name    = name;

    desc->state = IRQ_STATE_REGISTERED;

    // 6. 释放锁
    spin_unlock_irqrestore(&target_cpu->irq_lock, flags);

    return 0;
}


/**
 * @brief 卸载特定 CPU 上的硬件中断处理例程
 * @param target_cpu_id 目标 CPU 的逻辑 ID
 * @param vector        要卸载的硬件中断号 (32 ~ 255)
 * @param dev_id        设备上下文指针 (作为卸载的“私钥”安全凭证)
 */
int32 unregister_irq(uint32 target_cpu_id, int32 vector, void *dev_id) {
    // 1. 参数合法性校验
    if (vector < 32 || vector >= 256 || target_cpu_id >= active_cpu_count) return -EINVAL;

    // 2. 获取目标 CPU 的控制块指针 (核心：跨 CPU 寻址！)
    cpu_core_t *target_cpu = cpu_lid_to_core(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    uint32 irq_index = vector - 32;
    uint64 flags;

    // 3. 🌟 跨核修改防护：获取目标 CPU 中断表的自旋锁，并关中断
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    irq_desc_t *desc = &target_cpu->irq_table[irq_index];

    // ★ 校验 1：确保这块地皮确实盖了房子
    if (desc->state != IRQ_STATE_REGISTERED) {
        spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
        PR_ERROR("CPU %u IRQ %d is not registered!\n", target_cpu_id, vector);
        return -EINVAL;
    }

    // ★ 校验 2：安全凭证（dev_id）必须匹配！
    // 杜绝恶意驱动 A 去强行卸载驱动 B 注册的中断
    if (desc->dev_id != dev_id) {
        spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
        PR_ERROR("Dev ID mismatch! You don't own CPU %u IRQ %d!\n", target_cpu_id, vector);
        return -EPERM;
    }

    // 4. 🌟 极其讲究的卸载顺序 (配合无锁的 dispatcher)

    // 步骤 A: 先修改状态为已分配未注册 (退回 Alloc 状态，地皮仍归该设备，但房子拆了)
    desc->state = IRQ_STATE_ALLOCATED;

    // 步骤 C: 清空关键的函数指针和设备上下文
    // 由于 dispatcher 是先检查 handler != NULL 才调用，
    // 哪怕此时刚好进来一个中断，也会走到 "Unhandled Vector" 分支，而不会调用空指针。
    desc->handler = NULL;
    desc->dev_id  = NULL;
    desc->name    = NULL;

    // 5. 释放锁
    spin_unlock_irqrestore(&target_cpu->irq_lock, flags);

    return 0;
}


/**
 * @brief 在目标 CPU 上动态分配连续的中断向量 (支持 MSI 硬件对齐)
 * @param target_cpu_id 目标 CPU 逻辑 ID
 * @param count         需要的连续向量个数 (MSI 通常为 1, 2, 4, 8, 16, 32)
 * @param align         首向量对齐要求 (通常与 count 相等，填 1 则不对齐)
 * @param strategy      分配策略 (高速设备优先占用高编号)
 * @return 成功返回分配到的首个向量号 (Base Vector)；失败返回负数错误码
 */
int32 alloc_irq_vector(uint32 target_cpu_id, uint32 count, uint32 align, irq_alloc_strategy_t strategy) {
    if (count == 0 || align == 0 || target_cpu_id >= active_cpu_count) return -EINVAL;

    cpu_core_t *target_cpu = cpu_lid_to_core(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    int32 allocated_base = -1;
    uint64 flags;

    // 🌟 跨核分配防护：必须获取目标 CPU 的 IRQ 表锁
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    if (strategy == IRQ_ALLOC_HIGH_SPEED) {
        // --- 策略 1：高速设备 (Top-Down 向下查找) ---
        // 比如从 0xDF 开始往下找，优先赋予高硬件优先级
        for (int32 v = IRQ_DYNAMIC_FAST_LIMIT - count + 1; v >= IRQ_DYNAMIC_LOW_BASE; v--) {
            if (v % align != 0) continue; // 满足 MSI 的 2^N 对齐要求

            // 检查这连续的 count 个位置是否全为 FREE
            boolean available = TRUE;
            for (uint32 i = 0; i < count; i++) {
                if (target_cpu->irq_table[(v + i) - 32].state != IRQ_STATE_FREE) {
                    available = FALSE;
                    break;
                }
            }

            if (available) {
                allocated_base = v;
                break;
            }
        }
    } else {
        // --- 策略 2：低速设备 (Bottom-Up 向上查找) ---
        for (int32 v = IRQ_DYNAMIC_LOW_BASE; v <= IRQ_DYNAMIC_FAST_LIMIT - count + 1; v++) {
            if (v % align != 0) continue;

            boolean available = TRUE;
            for (uint32 i = 0; i < count; i++) {
                if (target_cpu->irq_table[(v + i) - 32].state != IRQ_STATE_FREE) {
                    available = FALSE;
                    break;
                }
            }

            if (available) {
                allocated_base = v;
                break;
            }
        }
    }

    // 如果找到了合适的连续区间，推进状态
    if (allocated_base != -1) {
        for (uint32 i = 0; i < count; i++) {
            // 状态变为 ALLOCATED（已被地主买下，但还没调用 register_irq 盖房子）
            target_cpu->irq_table[(allocated_base + i) - 32].state = IRQ_STATE_ALLOCATED;
        }
    }

    spin_unlock_irqrestore(&target_cpu->irq_lock, flags);

    return (allocated_base != -1) ? allocated_base : -ENOSPC; // 空间不足
}

/**
 * @brief 释放目标 CPU 上的中断向量 (归还到资源池)
 * @param target_cpu_id 目标 CPU 逻辑 ID
 * @param base_vector   要释放的首向量号
 * @param count         连续释放的个数
 */
int32 free_irq_vector(uint32 target_cpu_id, int32 base_vector, uint32 count) {
    if (count == 0 || base_vector < IRQ_DYNAMIC_LOW_BASE ||
        (base_vector + count - 1 > IRQ_DYNAMIC_FAST_LIMIT) ||
        target_cpu_id >= active_cpu_count) {
        return -EINVAL;
        }

    cpu_core_t *target_cpu = cpu_lid_to_core(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    uint64 flags;
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    // 严格安全校验：确保要释放的向量当前处于 ALLOCATED 状态
    // 如果处于 REGISTERED 状态，说明驱动还没调用 unregister_irq 卸载函数！
    for (uint32 i = 0; i < count; i++) {
        irq_desc_t *desc = &target_cpu->irq_table[(base_vector + i) - 32];
        if (desc->state == IRQ_STATE_REGISTERED) {
            spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
            PR_ERROR("CPU %u Vector %d is still REGISTERED! Call unregister_irq first.\n",
                     target_cpu_id, base_vector + i);
            return -EBUSY;
        }
    }

    // 安全通过，彻底清空并归还状态
    for (uint32 i = 0; i < count; i++) {
        irq_desc_t *desc = &target_cpu->irq_table[(base_vector + i) - 32];
        desc->handler = NULL;
        desc->dev_id  = NULL;
        desc->name    = NULL;
        desc->state = IRQ_STATE_FREE;
    }

    spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
    return 0;
}




