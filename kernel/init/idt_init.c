#include "idt_init.h"
#include "printk.h"

// 门类型宏定义
#define IDT_INTERRUPT_DESC 0x8E // P=1, DPL=0, Type=E (硬件中断，自动关 IF)
#define IDT_TRAP_DESC      0x8F // P=1, DPL=0, Type=F (异常陷阱，不关 IF)
#define IDT_USER_DESC      0xEE // P=1, DPL=3, Type=E (系统调用等，允许 Ring3 触发)


// IDTR 寄存器结构
typedef struct idt_ptr_t{
    uint16 limit;
    idt_t  *base;
}__attribute__((packed))idt_ptr_t;

extern idt_t idt;

// 引入汇编里暴露的地址表 (极其优雅，免去了写 256 行 extern)
extern uint64 isr_stub_table[];

/**
 * @brief 内部函数：组装 16 字节的 IDT 描述符
 */

static inline void idt_set_descriptor(uint8 vector , uint8 attributes, uint8 ist) {
    uint64 isr_addr = isr_stub_table[vector];
    idt_desc_t *desc = &idt.desc[vector];

    desc->offset_low       = isr_addr & 0xFFFF;
    desc->segment_selector = 0x08; // 你的内核代码段选择子 (根据你的 GDT 调整)
    desc->ist              = ist & 0x07;
    desc->attributes       = attributes;
    desc->offset_mid       = (isr_addr >> 16) & 0xFFFF;
    desc->offset_high      = (isr_addr >> 32) & 0xFFFFFFFF;
    desc->reserved         = 0; // 必须为 0
}



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


/**
 * @brief 初始化 IDT (在内核早期初始化时调用)
 */
void tmp_idt_init(void) {
    asm_mem_set(&idt, 0, sizeof(idt));

    // 循环挂载 256 个中断门
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_descriptor(i, IDT_INTERRUPT_DESC,0);
    }

    // 特例：如果是供用户态引发的异常/系统调用，可以改属性
    idt_set_descriptor(3, IDT_USER_DESC,0);
    idt_set_descriptor(4, IDT_USER_DESC,0);

    // =========================================================
    // 👑 架构师核心修改：为致命异常颁发 IST 免死金牌！
    // =========================================================
    idt_set_descriptor(8, IDT_INTERRUPT_DESC,1);// 8: Double Fault (#DF) 双重故障
    idt_set_descriptor(2, IDT_INTERRUPT_DESC,2);// 2: NMI 不可屏蔽中断
    idt_set_descriptor(18, IDT_INTERRUPT_DESC,3);// 18: Machine Check (#MC) 机器检查
    idt_set_descriptor(1, IDT_INTERRUPT_DESC,4);// 1: Debug (#DB) 调试异常

    // 装载 IDTR
    idt_ptr_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = &idt;
    asm_lidt(&idtr);

    // 打开 CPU 全局中断标志
    asm_sti();

    PR_OK("IDT Initialized with 256 vectors.\n");
}
