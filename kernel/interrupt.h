#pragma once

#include "linkage.h"
#include "moslib.h"

// 定义中断处理函数
void ignore(void);
void keyboard(void);
void apic_timer(void);
void hpet(void);


// 定义0-20异常处理函数
void divide_error();
void debug();
void nmi();
void int3();
void overflow();
void bounds();
void undefined_opcode();
void dev_not_available();
void double_fault();
void coprocessor_segment_overrun();
void invalid_TSS();
void segment_not_present();
void stack_segment_fault();
void general_protection();
void page_fault();
void x87_FPU_error();
void alignment_check();
void machine_check();
void SIMD_exception();
void virtualization_exception();

//=============================================================================================================

#pragma pack(push,1)

#define IDT_ENTRIES 256

// 门类型宏定义
#define IDT_GATE_INTERRUPT 0x8E // P=1, DPL=0, Type=E (硬件中断，自动关 IF)
#define IDT_GATE_TRAP      0x8F // P=1, DPL=0, Type=F (异常陷阱，不关 IF)
#define IDT_GATE_USER      0xEE // P=1, DPL=3, Type=E (系统调用等，允许 Ring3 触发)

// 1. x64 中断门描述符 (16 字节，严格对齐)
typedef struct {
    uint16 offset_low;
    uint16 segment_selector;
    uint8  ist;
    uint8  attributes;
    uint16 offset_mid;
    uint32 offset_high;
    uint32 reserved;
} idt_gate_t;

// IDTR 寄存器结构
typedef struct idtr_t{
    uint16 limit;
    void   *base;
}idtr_t;

// 2. 汇编层压入的 CPU 现场上下文 (严格匹配 push 顺序)
typedef struct {
    // 我们的 common_stub 手动 push 的通用寄存器
    uint64 r15, r14, r13, r12, r11, r10, r9, r8;
    uint64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    // 我们的 ISR 桩硬编码 push 的参数
    uint64 int_no;   // 中断号
    uint64 err_code; // 错误码 (无错误码的异常填充 0)
    
    // CPU 发生中断时自动压入的关键现场
    uint64 rip;
    uint64 cs;
    uint64 rflags;
    uint64 rsp;
    uint64 ss;
}cpu_registers_t;

#pragma pack(pop)

//CPU异常函数签名
typedef void (*exception_handler_t)(cpu_registers_t *regs);

// 3. 独占式 (MSI/MSI-X) 中断路由表数据结构
typedef enum:int8 {
    IRQ_NONE        = 0, // 硬件没数据，虚假唤醒
    IRQ_HANDLED     = 1, // 已成功处理
    IRQ_WAKE_THREAD = 2  // 留给未来的底半部唤醒标志
} irqreturn_e;

//cpu中断函数签名
typedef irqreturn_e (*irq_handler_f)(cpu_registers_t *regs, void *dev_id);

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


static inline void asm_lidt(idtr_t *idt_ptr) {
    __asm__ __volatile__(
            "lidt %0 \n\t"  // 加载 IDT 描述符地址
            :
            : "m"(*idt_ptr)    // 输入：IDT 描述符的地址
            : "memory"        // 防止编译器重排序内存操作
            );
}

// 对外暴露的 API
void idt_init(void);
int32 alloc_contiguous_irq(uint8 count);
void free_contiguous_irq(uint8 base_vector, uint8 count);
int32 alloc_irq(void);
void free_irq(int32 vector);
int32 register_isr(int32 vector, irq_handler_f handler, void *dev_id, const char *name);
int32 unregister_isr(int32 vector);

