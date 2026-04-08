#include "interrupt.h"
#include "printk.h"
#include "moslib.h"
#include "apic.h"
#include "errno.h"

/*********************************************************** 系统异常处理函数 *****************************************************************************/

void do_divide_error(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_divide_error(0),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_debug(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_debug(1),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_nmi(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_nmi(2),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_int3(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_int3(3),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_overflow(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_overflow(4),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_bounds(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_bounds(5),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_undefined_opcode(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_undefined_opcode(6),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_dev_not_available(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_dev_not_available(7),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_double_fault(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_double_fault(8),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_coprocessor_segment_overrun(cpu_registers_t *regs) {
    color_printk(RED, BLACK,
                 "do_coprocessor_segment_overrun(9),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_invalid_TSS(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_invalid_TSS(10),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

    while (1);
}


void do_segment_not_present(cpu_registers_t *regs) {
    color_printk(RED, BLACK,
                 "do_segment_not_present(11),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

    while (1);
}


void do_stack_segment_fault(cpu_registers_t *regs) {
    color_printk(RED, BLACK,
                 "do_stack_segment_fault(12),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

    while (1);
}


void do_general_protection(cpu_registers_t *regs) {
    color_printk(RED, BLACK,
                 "do_general_protection(13),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (regs->err_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (regs->err_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((regs->err_code & 0x02) == 0)
        if (regs->err_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", regs->err_code & 0xfff8);

    while (1);
}


void do_page_fault(cpu_registers_t *regs) {
    uint64 *p = NULL;
    uint64 cr2 = 0;

    __asm__ __volatile__("movq	%%cr2,	%0":"=r"(cr2)::"memory");

    color_printk(RED, BLACK, "do_page_fault(14),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);

    if (!(regs->err_code & 0x01))
        color_printk(RED, BLACK, "Page Not-Present,\t");

    if (regs->err_code & 0x02)
        color_printk(RED, BLACK, "Write Cause Fault,\t");
    else
        color_printk(RED, BLACK, "Read Cause Fault,\t");

    if (regs->err_code & 0x04)
        color_printk(RED, BLACK, "Fault in user(3)\t");
    else
        color_printk(RED, BLACK, "Fault in supervisor(0,1,2)\t");

    if (regs->err_code & 0x08)
        color_printk(RED, BLACK, ",Reserved Bit Cause Fault\t");

    if (regs->err_code & 0x10)
        color_printk(RED, BLACK, ",Instruction fetch Cause Fault");

    color_printk(RED, BLACK, "\n");

    color_printk(RED, BLACK, "CR2:%#018lx\n", cr2);

    while (1);
}


void do_x87_FPU_error(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_x87_FPU_error(16),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_alignment_check(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_alignment_check(17),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_machine_check(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_machine_check(18),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_SIMD_exception(cpu_registers_t *regs) {
    color_printk(RED, BLACK, "do_SIMD_exception(19),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}


void do_virtualization_exception(cpu_registers_t *regs) {
    color_printk(RED, BLACK,
                 "do_virtualization_exception(20),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 regs->err_code, regs->rsp,regs->rip);
    while (1);
}

/*******************************************************************************************************************************/

//定义 IDT
static idt_gate_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

// 异常不会动态卸载，直接静态写死极其安全
static exception_handler_t exception_table[32] = {
    [0]  = do_divide_error,                 // #DE: 除零错误 (Divide Error)
    [1]  = do_debug,                        // #DB: 调试异常 (Debug Exception)
    [2]  = do_nmi,                          // NMI: 不可屏蔽中断 (Non-Maskable Interrupt)
    [3]  = do_int3,                         // #BP: 断点异常 (Breakpoint/INT3)
    [4]  = do_overflow,                     // #OF: 溢出异常 (Overflow/INTO)
    [5]  = do_bounds,                       // #BR: 越界异常 (BOUND Range Exceeded)
    [6]  = do_undefined_opcode,             // #UD: 非法指令/未定义操作码 (Invalid Opcode)
    [7]  = do_dev_not_available,            // #NM: 设备不可用/FPU缺失 (Device Not Available)
    [8]  = do_double_fault,                 // #DF: 双重故障 (Double Fault)
    [9]  = do_coprocessor_segment_overrun,  // 协处理器段超限 (Coprocessor Segment Overrun，现代 x86 已废弃)
    [10] = do_invalid_TSS,                  // #TS: 无效的 TSS (Invalid TSS)
    [11] = do_segment_not_present,          // #NP: 段不存在 (Segment Not Present)
    [12] = do_stack_segment_fault,          // #SS: 栈段错误 (Stack-Segment Fault)
    [13] = do_general_protection,           // #GP: 常规保护错误 (General Protection Fault)
    [14] = do_page_fault,                   // #PF: 页错误/缺页异常 (Page Fault)
    // [15] 为 Intel 保留 (Intel Reserved)
    [16] = do_x87_FPU_error,                // #MF: x87 浮点运算错误 (x87 FPU Floating-Point Error)
    [17] = do_alignment_check,              // #AC: 对齐检查 (Alignment Check)
    [18] = do_machine_check,                // #MC: 机器检查 (Machine Check)
    [19] = do_SIMD_exception,               // #XM: SIMD 浮点异常 (SIMD Floating-Point Exception)
    [20] = do_virtualization_exception,     // #VE: 虚拟化异常 (Virtualization Exception)
    // [21] - [31] 为 Intel 保留异常或较新的硬件异常 (如 21号 #CP 控制保护异常)，默认初始化为 NULL
};


//MSI/MSI-X 专属 O(1) 独占路由表 (静态分配，快如闪电)
static irq_desc_t irq_table[IDT_ENTRIES] = {0};

// 引入汇编里暴露的地址表 (极其优雅，免去了写 256 行 extern)
extern uint64 isr_stub_table[];


/**
 * @brief 👑 C 语言大管家 (所有中断的终极交汇点)
 * 汇编层已完全保护现场，并穿透参数，核心只需做 O(1) 分发。
 */
void c_interrupt_dispatcher(cpu_registers_t *regs) {
    uint64 int_no = regs->int_no;

    // ==========================================
    // 1. CPU 内部异常处理 (0~31)
    // ==========================================
    if (int_no < 32) {
        exception_handler_t exception = exception_table[int_no];
        if (exception != NULL) {
            // 调用注册好的处理函数
            exception(regs);
        } else {
            // 对于保留的异常，或者没有注册的异常，直接 panic
            color_printk(RED, BLACK, "Unhandled Exception: %d, ERROR_CODE: %#018lx\n", int_no, regs->err_code);
            while(1);
        }

        // 🚨 极其重要：异常处理完毕必须直接返回！
        // 异常是 CPU 内部产生的，绝对不能向外设 APIC 发送 EOI！
        return;
    }

    // 2. 硬件中断 O(1) 极速分发 (32~255)
    irq_desc_t *irq_desc = &irq_table[int_no];
    if (irq_desc->handler != NULL) {
        // 精确制导，将上下文和设备私有指针送达驱动核心
        irq_desc->handler(regs, irq_desc->dev_id);
    } else {
        color_printk(YELLOW, BLACK, "Spurious Interrupt Vector: %d\n", int_no);
    }

    // ==========================================
    // 3. 发送 EOI (End of Interrupt)
    // ==========================================
    // 假设你将 255 (0xFF) 设为了 APIC Spurious Interrupt Vector
    // Intel 手册规定：发生伪中断时，不需要也不应该发送 EOI
    if (int_no != 255) {
        // 通知主板：“本官已审理完毕，允许发送下一批案卷”
        apic_send_eoi();
    }
}



/**
 * @brief 在已分配的向量上挂载具体的处理例程
 * @param vector   之前 alloc_irq_vector 借到的号码
 * @param handler  中断服务例程
 * @param dev_id   设备上下文
 * @param name     驱动标识名
 */
int32 register_isr(int32 vector, irq_handler_t handler, void *dev_id, const char *name) {
    if (vector < 32 || vector >= IDT_ENTRIES) return -EINVAL;

    // ★ 防火墙 1：不准在没买过的地皮上盖房！
    if (irq_table[vector].state == IRQ_STATE_FREE) {
        color_printk(RED, BLACK, "IRQ %d is NOT allocated!\n", vector);
        return -EPERM;
    }

    // ★ 防火墙 2：不准在已经盖好房子的地皮上重复盖房！
    if (irq_table[vector].state == IRQ_STATE_REGISTERED) {
        color_printk(RED, BLACK, "IRQ %d is already registered to %s!\n", vector, irq_table[vector].name);
        return -EBUSY;
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
    if (vector < 32 || vector >= IDT_ENTRIES) return -EINVAL;

    // ★ 防火墙 1：防误杀！如果是一块连地皮都没买过的空地，直接报错
    if (irq_table[vector].state == IRQ_STATE_FREE) {
        color_printk(RED, BLACK, "IRQ %d is completely free, nothing to unregister!\n", vector);
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
    for (int32 i = 32; i <= IDT_ENTRIES - count; i++) {
        
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
    if (base_vector < 32 || (uint16)base_vector + count > IDT_ENTRIES) {
        color_printk(RED, BLACK, "IRQ: Attempt to free invalid vector range [%d, %d)\n",
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
    for (uint8 i = 32; i < IDT_ENTRIES; i++) {
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


/**
 * @brief 内部函数：组装 16 字节的 IDT 描述符
 */
static void idt_set_descriptor(uint8 vector, uint64 isr_addr, uint8 attributes, uint8 ist) {
    idt_gate_t *desc = &idt[vector];

    desc->offset_low       = isr_addr & 0xFFFF;
    desc->segment_selector = 0x08; // 你的内核代码段选择子 (根据你的 GDT 调整)
    desc->ist              = ist & 0x07;
    desc->attributes       = attributes;
    desc->offset_mid       = (isr_addr >> 16) & 0xFFFF;
    desc->offset_high      = (isr_addr >> 32) & 0xFFFFFFFF;
    desc->reserved         = 0; // 必须为 0
}


/**
 * @brief 初始化 IDT (在内核早期初始化时调用)
 */
void idt_init(void) {
    asm_mem_set(idt, 0, sizeof(idt));

    // 循环挂载 256 个中断门
    for (int i = 0; i < IDT_ENTRIES; i++) {
        // 默认全部设为硬件中断门 (关中断执行)
        uint8 attr = IDT_GATE_INTERRUPT;

        // 特例：如果是供用户态引发的异常/系统调用，可以改属性
        if (i == 3 || i == 4) {
            attr = IDT_GATE_USER;
        }

        // IST 默认不使用(0)。后续你可以为 #DF(8) 专门指派独立的 IST
        idt_set_descriptor(i, isr_stub_table[i], attr, 1);
    }

    // 装载 IDTR
    idtr_t idtr = {
        .limit = sizeof(idt) - 1,
        .base = idt,
    };
    asm_lidt(&idtr);

    // 打开 CPU 全局中断标志
    asm_sti();

    color_printk(GREEN, BLACK, "IDT Initialized with 256 vectors.\n");
}
