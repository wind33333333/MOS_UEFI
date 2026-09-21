#pragma once
#include "moslib.h"

#define IA32_PAT_MSR        0x277        //设置页属性PAT类型
#define IA32_EFER_MSR       0xC0000080   // 扩展功能寄存器（Extended Feature Enable Register）
#define IA32_STAR_MSR       0xC0000081   // 系统调用目标寄存器（Segment Target Address Register）
#define IA32_LSTAR_MSR      0xC0000082   // 64位系统调用入口寄存器（Long Mode System Call Target Address Register）
#define IA32_CSTAR_MSR      0xC0000083   // 兼容模式系统调用入口寄存器（Compatibility Mode System Call Target Address Register）
#define IA32_FMASK_MSR      0xC0000084   // 系统调用掩码寄存器（System Call Flag Mask Register）
#define IA32_APIC_BASE_MSR                0x1B   // 本地APIC基地址寄存器（Local APIC Base Address Register）
#define IA32_TSC_DEADLINE                 0x6E0  //TSC-Deadline时间戳计数器截止寄存器

#define APIC_ID_MSR                       0x802  // 本地APIC ID寄存器
#define APIC_VERSION_MSR                  0x803  // 本地APIC版本寄存器
#define APIC_TASK_PRIORITY_MSR            0x808  // 任务优先级寄存器
#define APIC_PROCESSOR_PRIORITY_MSR       0x80A  // 处理器优先级寄存器
#define APIC_EOI_MSR                      0x80B  // 中断结束（EOI）寄存器
#define APIC_SPURIOUS_VECTOR_MSR          0x80F  // 虚假中断向量寄存器

// 中断服务寄存器（ISR）
#define APIC_ISR_31_0_MSR                 0x810  // ISR位31:0
#define APIC_ISR_63_32_MSR                0x811  // ISR位63:32
#define APIC_ISR_95_64_MSR                0x812  // ISR位95:64
#define APIC_ISR_127_96_MSR               0x813  // ISR位127:96
#define APIC_ISR_159_128_MSR              0x814  // ISR位159:128
#define APIC_ISR_191_160_MSR              0x815  // ISR位191:160
#define APIC_ISR_223_192_MSR              0x816  // ISR位223:192
#define APIC_ISR_255_224_MSR              0x817  // ISR位255:224

// 中断请求寄存器（IRR）
#define APIC_IRR_31_0_MSR                 0x820  // IRR位31:0
#define APIC_IRR_63_32_MSR                0x821  // IRR位63:32
#define APIC_IRR_95_64_MSR                0x822  // IRR位95:64
#define APIC_IRR_127_96_MSR               0x823  // IRR位127:96
#define APIC_IRR_159_128_MSR              0x824  // IRR位159:128
#define APIC_IRR_191_160_MSR              0x825  // IRR位191:160
#define APIC_IRR_223_192_MSR              0x826  // IRR位223:192
#define APIC_IRR_255_224_MSR              0x827  // IRR位255:224

// 中断屏蔽寄存器（TMR）
#define APIC_TMR_31_0_MSR                 0x818  // TMR位31:0
#define APIC_TMR_63_32_MSR                0x819  // TMR位63:32
#define APIC_TMR_95_64_MSR                0x81A  // TMR位95:64
#define APIC_TMR_127_96_MSR               0x81B  // TMR位127:96
#define APIC_TMR_159_128_MSR              0x81C  // TMR位159:128
#define APIC_TMR_191_160_MSR              0x81D  // TMR位191:160
#define APIC_TMR_223_192_MSR              0x81E  // TMR位223:192
#define APIC_TMR_255_224_MSR              0x81F  // TMR位255:224

#define APIC_ERROR_STATUS_MSR             0x828  // 错误状态寄存器
#define APIC_LVT_CMCI_MSR                 0x82F  // LVT校验错误寄存器

#define APIC_INTERRUPT_COMMAND_MSR        0x830  // 中断命令寄存器（ICR）

// 本地向量表（LVT）寄存器
#define APIC_LVT_TIMER_MSR                0x832  // 定时器LVT寄存器
#define APIC_LVT_THERMAL_SENSOR_MSR       0x833  // 热传感器LVT寄存器
#define APIC_LVT_PERF_COUNTER_MSR         0x834  // 性能计数器LVT寄存器
#define APIC_LVT_LINT0_MSR                0x835  // 本地中断LINT0寄存器
#define APIC_LVT_LINT1_MSR                0x836  // 本地中断LINT1寄存器
#define APIC_LVT_ERROR_MSR                0x837  // 错误LVT寄存器

#define APIC_INITIAL_COUNT_MSR            0x838  // 初始计数寄存器
#define APIC_CURRENT_COUNT_MSR            0x839  // 当前计数寄存器
#define APIC_DIVIDE_CONFIG_MSR            0x83E  // 分频配置寄存器
#define APIC_SELF_IPI_MSR                 0x83F  // 自发送IPI寄存器

#define APIC_ONESHOT 0              //一次性定时模式
#define APIC_PERIODIC  0x20000      //周期性定时模式
#define APIC_TSC_DEADLINE 0x40000   //TSC截止期限模式


//中断结束发送EOI
static inline void apic_send_eoi(void) {
    __asm__ __volatile__(
        "xorl %%edx, %%edx \n\t"
        "xorl %%eax, %%eax \n\t"
        "movl $0x80B, %%ecx \n\t"
        "wrmsr             \n\t"
        : /* 无输出 */
        : /* 无输入 */
        : "%rax", "%rcx", "%rdx", "memory" // 明确告知编译器这三个寄存器被破坏了
    );
}


#define DISABLE_APIC_TIME() \
        do {  \
          __asm__ __volatile__( \
         "xorl   %%edx,%%edx           \n\t"          \
         "movl   $0x10000,%%eax        \n\t"         /*bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline*/ \
         "movl   $0x832,%%ecx          \n\t"         /*定时器模式配置寄存器*/ \
         "wrmsr                        \n\t"          \
          :::"%rdx","%rax");    \
          } while(0)

#define APIC_SET_TSCDEADLINE(TIME) \
        do {                       \
        __asm__ __volatile__( \
        "rdtscp                           \n\t" \
        "shll       $32,%%rdx             \n\t" \
        "orq        %%rdx,%%rax           \n\t" \
        "addq       %0,%%rax              \n\t" \
        "movq       %%rax,%%rdx           \n\t" \
        "movq       $0xFFFFFFFF,%%rcx     \n\t" \
        "andq       %%rcx,%%rax           \n\t" \
        "shrq       $32,%%rdx             \n\t" \
        "movq       $0x6E0,%%ecx          \n\t" /*IA32_TSC_DEADLINE寄存器 TSC-Deadline定时模式 */ \
        "wrmsr                            \n\t" \
        ::"m"(TIME):"%rax","%rcx","%rdx"); \
        } while(0)


// 前置声明任务/线程控制块 (你的调度器定义)
struct thread_t;
// 前置声明 x86_64 的任务状态段 (用于存放 rsp0 和 IST)
struct tss64_t;

// 定义 CPU 的生命周期状态
typedef enum {
    CPU_STATE_OFFLINE = 0, // 尚未唤醒或已下线
    CPU_STATE_BOOTING,     // 正在执行启动代码 (AP 唤醒中)
    CPU_STATE_IDLE,        // 闲置中 (执行 hlt 指令)
    CPU_STATE_RUNNING,     // 正在执行用户态或内核态任务
    CPU_STATE_PANIC        // 该核心已崩溃停止
} cpu_state_t;

// =================================================================
// 👑 核心数据结构：每 CPU 控制块 (Per-CPU Control Block)
// =================================================================
typedef struct cpu_core {
    // -------------------------------------------------------------
    // [1] 身份与拓扑区 (Hardware Identity & Topology)
    // -------------------------------------------------------------
    uint32  logical_id;       // 内核分配的逻辑编号 (0, 1, 2... 用于数组索引)
    uint32  apic_id;          // 真实的物理 APIC ID (用于发送 IPI 中断唤醒/TLB 刷新)
    uint32  acpi_proc_id;     // ACPI 逻辑 ID (从 MADT 读出)
    uint32  numa_node;        // 所属 NUMA 物理节点 (由 SRAT 表解析得出)
    boolean is_bsp;           // 是否为主核心 (Bootstrap Processor)
    uint8   lint_nmi;

    // -------------------------------------------------------------
    // [2] 运行时状态区 (Runtime Status)
    // -------------------------------------------------------------
    cpu_state_t state;        // 当前核心的运行状态

    // -------------------------------------------------------------
    // [3] 调度器上下文 (Scheduler Context)
    // -------------------------------------------------------------
    struct thread_t *current_thread; // 当前正在该 CPU 上运行的线程/进程
    struct thread_t *idle_thread;    // 该 CPU 专属的空闲线程 (没有任务时执行 hlt)
    // void *run_queue;              // 进阶：该 CPU 专属的就绪任务队列 (无锁调度核心)

    // -------------------------------------------------------------
    // [4] x86_64 底层硬件区 (Architecture Specific)
    // -------------------------------------------------------------
    // 💡 呼应我们之前的讨论：栈和 TSS！
    struct tss64_t *tss;      // 该 CPU 专属的 TSS (里面存放着这个核心的 rsp0, IST 1~4)
    uint64  gdt_base;         // 该 CPU 专属的 GDT 基地址 (多核系统每个核要有独立的 GDT)

    // -------------------------------------------------------------
    // [5] 本地内存池 (SLUB 关联)
    // -------------------------------------------------------------
    void    *kmem_cache_cpu;  // SLUB 分配器为该 CPU 准备的【无锁内存池】指针

    // -------------------------------------------------------------
    // [6] 统计与性能监控 (Stats & Profiling)
    // -------------------------------------------------------------
    uint64  interrupt_count;  // 该 CPU 处理的总中断次数
    uint64  context_switches; // 该 CPU 发生的上下文切换次数
    uint64  timer_ticks;      // 属于该 CPU 的 Local APIC Timer 滴答数

} __attribute__((aligned(64))) cpu_core_t;
// 🌟 架构师细节：__attribute__((aligned(64))) 极其重要！
// 保证这个结构体按 CPU Cache Line (64字节) 对齐，防止极其致命的“伪共享 (False Sharing)”性能暴跌！


typedef struct {
    char8 manufacturer_name[13];
    char8 model_name[49];
    uint32 logical_processors_number;
    uint32 fundamental_hz;
    uint32 maximum_hz;
    uint32 bus_hz;
    uint32 tsc_hz;
}cpu_info_t;

extern cpu_info_t cpu_info;
extern uint32 *apic_id_table;

uint32 apicid_to_cpuid(uint32 apic_id);
uint32 cpuid_to_apicid(uint32 cpu_id);

