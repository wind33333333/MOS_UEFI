#pragma once
#include "../x64/cpu_registers.h"
#include "../include/moslib.h"

// 门类型宏定义
#define IDT_INTERRUPT_DESC 0x8E // P=1, DPL=0, Type=E (硬件中断，自动关 IF)
#define IDT_TRAP_DESC      0x8F // P=1, DPL=0, Type=F (异常陷阱，不关 IF)
#define IDT_USER_DESC      0xEE // P=1, DPL=3, Type=E (系统调用等，允许 Ring3 触发)

// 1. x64 中断门描述符 (16 字节，严格对齐)
typedef struct {
    uint16 offset_low;
    uint16 segment_selector;
    uint8  ist;
    uint8  attributes;
    uint16 offset_mid;
    uint32 offset_high;
    uint32 reserved;
}__attribute__((packed)) idt_desc_t;

#define IDT_ENTRIES 256
typedef struct {
    idt_desc_t desc[IDT_ENTRIES];
}__attribute__((packed))idt_t;

// IDTR 寄存器结构
typedef struct idt_ptr_t{
    uint16 limit;
    idt_t  *base;
}__attribute__((packed))idt_ptr_t;


// 假设你之前定义的 IDTR 结构体名叫 idtr_t
static inline void asm_lidt(const idt_ptr_t *idt_ptr) {
    __asm__ __volatile__(
        "lidt %0 \n\t"
        :
        /*
         * 【架构师修复】：必须加 * 解引用！
         * 这样 GCC 就会生成正确的内存寻址，例如: lidt (%rdi)
         * 而不是把你指针变量在栈上的地址喂给 CPU。
         */
        : "m"(*idt_ptr)
        : "memory"      // 内存屏障，非常正确！
    );
}

extern idt_t idt;

// 定义0-20异常处理函数
int32 do_divide_error(cpu_registers_t *regs, void *dev_id);
int32 do_debug(cpu_registers_t *regs, void *dev_id);
void do_nmi(cpu_registers_t *regs);
int32 do_int3(cpu_registers_t *regs, void *dev_id);
int32 do_overflow(cpu_registers_t *regs, void *dev_id) ;
int32 do_bounds(cpu_registers_t *regs, void *dev_id);
int32 do_undefined_opcode(cpu_registers_t *regs, void *dev_id);
int32 do_dev_not_available(cpu_registers_t *regs, void *dev_id);
int32 do_double_fault(cpu_registers_t *regs, void *dev_id);
int32 do_coprocessor_segment_overrun(cpu_registers_t *regs, void *dev_id);
int32 do_invalid_tss(cpu_registers_t *regs, void *dev_id);
int32 do_segment_not_present(cpu_registers_t *regs, void *dev_id);
int32 do_stack_segment_fault(cpu_registers_t *regs, void *dev_id);
int32 do_general_protection(cpu_registers_t *regs, void *dev_id);
int32 do_page_fault(cpu_registers_t *regs, void *dev_id);
int32 do_x87_FPU_error(cpu_registers_t *regs, void *dev_id) ;
int32 do_alignment_check(cpu_registers_t *regs, void *dev_id) ;
int32 do_machine_check(cpu_registers_t *regs, void *dev_id);
int32 do_SIMD_exception(cpu_registers_t *regs, void *dev_id);
int32 do_virtualization_exception(cpu_registers_t *regs, void *dev_id) ;

//=============================================================================================================

//CPU异常函数签名
typedef void (*exception_handler_t)(cpu_registers_t *regs);

// 3. 独占式 (MSI/MSI-X) 中断路由表数据结构
#define IRQ_NONE        0 // 硬件没数据，虚假唤醒
#define IRQ_HANDLED     1 // 已成功处理
#define IRQ_WAKE_THREAD 2  // 留给未来的底半部唤醒标志
//cpu中断函数签名
typedef int32 (*irq_handler_f)(cpu_registers_t *regs, void *dev_id);

// 定义中断向量的 3 种核心生命周期状态
typedef enum:int8 {
    IRQ_STATE_FREE       = 0, // [空闲]：池中可用，无人问津
    IRQ_STATE_ALLOCATED  = 1, // [占坑]：已分配给某个驱动，但业务函数还未就绪
    IRQ_STATE_REGISTERED = 2  // [服役]：业务函数已挂载，随时准备处理硬件中断
} irq_state_e;

typedef struct {
    irq_state_e   state;     // 状态
    irq_handler_f handler;   // 独占处理函数
    void *dev_id;            // 设备实例上下文 (如 xhci_t*)
    const char *name;        // 驱动名称标识
} irq_desc_t;


// 对外暴露的 API
void idt_set_descriptor(uint8 vector , uint8 attributes, uint8 ist);
int32 alloc_contiguous_irq(uint8 count);
void free_contiguous_irq(uint8 base_vector, uint8 count);
int32 alloc_irq(void);
void free_irq(int32 vector);
int32 register_isr(int32 vector, irq_handler_f handler, void *dev_id, const char *name);
int32 unregister_isr(int32 vector);
void tmp_interrupt_init(void);
