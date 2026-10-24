#pragma once
#include "../x64/cpu_registers.h"
#include "../include/moslib.h"


// ==========================================
// 1. CPU 架构保留区 (0x00 - 0x1F)
// ==========================================
// 0x00 ~ 0x1F 严禁被外设或软件占用，由硬件异常独占。

// ==========================================
// 2. 传统 ISA 与低速设备区 (0x20 - 0x2F) [Priority 2]
// ==========================================
// 即便在 UEFI 和纯 64 位下，IOAPIC 依然可能会把主板上残存的
// 8254 定时器、PS/2 键盘模拟、串口等映射到前 16 个引脚。
#define IRQ_ISA_BASE                0x20 // IOAPIC 默认 ISA 映射起点

// ==========================================
// 3. 现代设备动态分配池 (0x30 - 0xDF) [Priority 3 ~ 13]
// ==========================================
// - 低速设备 (USB 鼠标/键盘, 触摸板, 声卡)：从 0x30 往上分配 (Bottom-Up)
// - 高速设备 (NVMe 固态, 10G/40G 万兆网卡, GPU)：从 0xDF 往下分配 (Top-Down)
#define IRQ_DYNAMIC_LOW_BASE        0x30
#define IRQ_DYNAMIC_FAST_LIMIT      0xDF

// ==========================================
// 4. Local APIC 本地核心控制区 (0xE0 - 0xEF) [Priority 14]
// ==========================================
// 这些中断不是外设发出的，而是 CPU 内部的 Local APIC 硬件自己产生的。
#define APIC_CMCI_VECTOR            0xEB // Corrected Machine Check (内存可纠正错误告警)
#define APIC_PMU_VECTOR             0xEC // 性能监控计数器溢出 (用于内核 Profiling/火焰图)
#define APIC_ERROR_VECTOR           0xED // APIC 自身硬件校验错误
#define APIC_THERMAL_VECTOR         0xEE // CPU 温度过热降频警告
#define APIC_TIMER_VECTOR           0xEF // APIC 本地时钟中断 (驱动当前 CPU 的线程调度器)

// ==========================================
// 5. SMP 核间中断 (IPI) 与最高权限区 (0xF0 - 0xFF) [Priority 15]
// ==========================================
// 整个系统最紧急的事件，一旦发生，必须以最快速度被硬件选中投递。
#define IPI_GENERIC_CALL            0xFA // 泛型核间调用 (让其他核执行特定的 C 函数，用于软中断/RCU)
#define IPI_RESCHEDULE              0xFB // 调度器 IPI：强制其他核心立即触发线程调度
#define IPI_TLB_SHOOTDOWN           0xFC // TLB 击落：命令其他核心使特定的页表缓存失效
#define IPI_CRASH_DUMP              0xFD // 崩溃转储：一个核 Panic 了，命令其他核冻结并保存寄存器
#define IPI_HALT                    0xFE // 停机命令：全系统致命崩溃，强制其他所有 CPU 无条件执行 hlt
#define APIC_SPURIOUS_VECTOR        0xFF // 虚假中断 (Intel 规范建议使用 0xFF)


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

extern const idt_t idt;

// IDTR 寄存器结构
typedef struct idt_ptr_t{
    uint16 limit;
    const idt_t  *base;
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

// 定义0-20异常处理函数
void do_divide_error(cpu_registers_t *regs);
void do_debug(cpu_registers_t *regs);
void do_nmi(cpu_registers_t *regs);
void do_int3(cpu_registers_t *regs);
void do_overflow(cpu_registers_t *regs) ;
void do_bounds(cpu_registers_t *regs);
void do_undefined_opcode(cpu_registers_t *regs);
void do_dev_not_available(cpu_registers_t *regs);
void do_double_fault(cpu_registers_t *regs);
void do_coprocessor_segment_overrun(cpu_registers_t *regs);
void do_invalid_tss(cpu_registers_t *regs);
void do_segment_not_present(cpu_registers_t *regs);
void do_stack_segment_fault(cpu_registers_t *regs);
void do_general_protection(cpu_registers_t *regs);
void do_page_fault(cpu_registers_t *regs);
void do_x87_FPU_error(cpu_registers_t *regs) ;
void do_alignment_check(cpu_registers_t *regs) ;
void do_machine_check(cpu_registers_t *regs);
void do_SIMD_exception(cpu_registers_t *regs);
void do_virtualization_exception(cpu_registers_t *regs) ;

//=============================================================================================================

//CPU异常函数签名
typedef void (*exception_handler_f)(cpu_registers_t *regs);

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


typedef enum {
    IRQ_ALLOC_LOW_SPEED,   // 低速设备：从 0x30 开始 Bottom-Up 查找
    IRQ_ALLOC_HIGH_SPEED   // 高速设备：从 0xDF 开始 Top-Down 查找
} irq_alloc_strategy_t;

int32 alloc_irq_vector(uint32 target_cpu_id, uint32 count, uint32 align, irq_alloc_strategy_t strategy);
int32 free_irq_vector(uint32 target_cpu_id, int32 base_vector, uint32 count) ;

int32 register_irq(uint32 target_cpu_id, int32 vector, irq_handler_f handler, void *dev_id, const char *name);
int32 unregister_irq(uint32 target_cpu_id, int32 vector, void *dev_id);

int32 register_exception(int32 vector, exception_handler_f handler);
int32 unregister_exception(int32 vector);

void idt_init(void);
void tmp_exception_init();
