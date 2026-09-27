#include "../include/moslib.h"
#include "memblock_init.h"
#include "vmm_init.h"
#include "pmm_init.h"
#include "slub_init.h"
#include "cpu_init.h"
#include "kpt_init.h"
#include "vmalloc_init.h"
#include "uefi_init.h"
#include "alternative_init.h"
#include "video_init.h"
#include "apic_init.h"
#include "printk.h"
#include "../x64/mtrr.h"
#include "../x64/cpu.h"
#include "../include/bus.h"
#include "../x64/interrupt.h"
#include "../include/ioapic.h"
#include "../drivers/hpet/hpet.h"


// =========================================================================
// 2. 通用定点数参数计算器 (仅在开机时调用，自动寻找精度最高的 mult 和 shift)
// =========================================================================
static inline void calc_mult_shift(
    uint64  from_hz,
    uint64  to_hz,
    uint64 *out_mult,
    uint32 *out_shift,
    boolean round_up)
{
    uint32 shift = 62;
    uint64 mult  = 0;

    // 从高到低搜索最大的 shift，使 mult 恰好落入 32 位上限 (<= 0xFFFFFFFF)
    while (shift > 0) {
        // 防溢出检查：确保 (to_hz << shift) 的高 64 位严格小于除数 from_hz
        if ((to_hz >> (64 - shift)) < from_hz) {
            if (round_up) {
                mult = asm_mul_div64_ceil(to_hz, 1ULL << shift, from_hz);
            } else {
                mult = asm_mul_div64(to_hz, 1ULL << shift, from_hz);
            }

            if (mult <= 0xFFFFFFFFULL && mult > 0) {
                break;
            }
        }
        shift--;
    }

    *out_mult  = mult;
    *out_shift = shift;
}

// =========================================================================
// 3. 全局只读定点数常数 (由 0号核 BSP 在开机时一次性算出，全核共享只读)
// =========================================================================
static boolean g_has_tsc_deadline  = FALSE;
static uint64  g_apic_hz           = 0;
static uint8   g_apic_div_cfg      = 0x03;

// [转换组 A] TSC -> 纳秒 (用于 get_uptime_ns)
static uint64  g_tsc_to_ns_mult    = 0;
static uint32  g_tsc_to_ns_shift   = 0;

// [转换组 B] 纳秒 -> TSC (用于 sleep_ns / sleep_us 计算目标唤醒 TSC)
static uint64  g_ns_to_tsc_mult    = 0;
static uint32  g_ns_to_tsc_shift   = 0;
static uint64  g_ns_to_tsc_mask    = 0;

// [转换组 C] TSC -> APIC Ticks (用于 AMD / 虚拟机 One-Shot 闹钟装填)
static uint64  g_tsc_to_apic_mult  = 0;
static uint32  g_tsc_to_apic_shift = 0;
static uint64  g_tsc_to_apic_mask  = 0;

// =========================================================================
// 4. 运行期极速热路径 API (100% 纯乘法 + 右移，零除法指令！)
// =========================================================================

// [热路径 1] 获取系统开机以来的纳秒时间 (耗时 ~1 纳秒，永不溢出)
static inline uint64 get_uptime_ns(void) {
    uint64 tsc = asm_rdtscp();
    return (uint64)(((__uint128_t)tsc * g_tsc_to_ns_mult) >> g_tsc_to_ns_shift);
}

// [热路径 2] 将纳秒延时换算为 TSC 增量 (向上取整，保证睡眠绝不早退)
static inline uint64 ns_to_tsc_delta(uint64 delay_ns) {
    return (uint64)(((__uint128_t)delay_ns * g_ns_to_tsc_mult + g_ns_to_tsc_mask) >> g_ns_to_tsc_shift);
}

// [热路径 3] 设定下一次中断的绝对 TSC 时间点 (零除法双擎版)
static inline void lapic_timer_set_deadline_tsc(uint64 target_tsc) {
    // 通道 A：Intel 真机 / KVM 直接写硬件 0x6E0
    if (g_has_tsc_deadline) {
        asm_wrmsr(TSC_DEADLINE_MSR, target_tsc);
        return;
    }

    // 通道 B：AMD 真机 / 默认虚拟机，使用定点数极速换算 32 位 One-Shot 倒数值
    uint64 now_tsc = asm_rdtscp();
    if (target_tsc <= now_tsc) {
        asm_wrmsr(APIC_INITIAL_COUNT_MSR, 1);
        return;
    }

    uint64 delta_tsc = target_tsc - now_tsc;

    // 🌟 核心加速：1 条 mulq 乘法 + 1 条加法 + 1 条 shrdq 右移，取代慢速 128 位除法！
    // 由于加了 mask 向上取整，当 delta_tsc >= 1 时，算出的 apic_ticks 天然 >= 1！
    uint64 apic_ticks = (uint64)(((__uint128_t)delta_tsc * g_tsc_to_apic_mult + g_tsc_to_apic_mask)
                                 >> g_tsc_to_apic_shift);

    // 32 位计数器上限饱和截断
    if (apic_ticks > 0xFFFFFFFFULL) {
        apic_ticks = 0xFFFFFFFFULL;
    }

    asm_wrmsr(APIC_INITIAL_COUNT_MSR, (uint32)apic_ticks);
}

// =========================================================================
// 5. 初始化入口：在 BSP 上一次性预计算所有定点数常数
// =========================================================================
void lapic_timer_init_per_cpu(void) {
    cpu_core_t *core = &cpu_cores[THIS_CPU->logical_id];

    if (core->logical_id == 0) {
        uint64 tsc_hz = core->tsc_hz;

        // 1. 预计算 TSC <-> 纳秒 (10^9 Hz) 的双向定点数参数
        calc_mult_shift(tsc_hz, 1000000000ULL, &g_tsc_to_ns_mult, &g_tsc_to_ns_shift, FALSE);
        calc_mult_shift(1000000000ULL, tsc_hz, &g_ns_to_tsc_mult, &g_ns_to_tsc_shift, TRUE);
        g_ns_to_tsc_mask = (1ULL << g_ns_to_tsc_shift) - 1;

        // 2. 检测 TSC-Deadline；若不支持则校准 APIC 并预计算 TSC -> APIC 定点数参数
        g_has_tsc_deadline = core->has_tsc_deadline;
        if (!g_has_tsc_deadline) {
            uint64 raw_bus_hz = tsc_calibrate_apic_hz(tsc_hz, 5);

            // 自适应分频：将 APIC 计数频率锚定在 <= 10MHz 黄金区间
            static const uint8 k_div_table[8] = {0x0B, 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A};
            uint32 div_shift = 0;
            while (div_shift < 7 && (raw_bus_hz >> div_shift) > 10000000ULL) {
                div_shift++;
            }
            g_apic_hz      = raw_bus_hz >> div_shift;
            g_apic_div_cfg = k_div_table[div_shift];

            // 预计算 TSC -> APIC Ticks 定点数参数 (开启向上取整 round_up = TRUE)
            calc_mult_shift(tsc_hz, g_apic_hz, &g_tsc_to_apic_mult, &g_tsc_to_apic_shift, TRUE);
            g_tsc_to_apic_mask = (1ULL << g_tsc_to_apic_shift) - 1;
        }
    }

    core->tsc_step_per_tick = core->tsc_hz / 1000;

    // 配置当前核心的 APIC 硬件寄存器
    if (g_has_tsc_deadline) {
        asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TSC_DEADLINE  | alloc_irq());
        asm_mfence();
    } else {
        asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, g_apic_div_cfg);
        asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_ONESHOT | alloc_irq());
    }

    // 装填第 1 次闹钟
    core->next_tsc_deadline = asm_rdtscp() + core->tsc_step_per_tick;
    lapic_timer_set_deadline_tsc(core->next_tsc_deadline);
}

void kernel_init(void) {
    asm_mem_set(_start_bss,0x0,_end_bss-_start_bss);    //初始化bss段
    output_init();                                              //初始化输出控制台
    tmp_idt_init();                                             //初始化临时中断描述符表
    cpu_enable_feature();                                       //cpu启用高级特性
    apply_alternatives();                                       //动态修复指令
    vm_layout_init();                                           //虚拟内存空间初始化
    memblock_init();                                            //初始化启动内存分配器
    kpage_table_init();                                         //初始化正式内核页表
    apic_init();                                                //apic初始化
    buddy_system_init();                                        //初始化伙伴系统
    slub_init();                                                //初始化slub内存分配器
    vmalloc_init();                                             //初始化vmalloc
    video_mem_map();                                            //映射显存到虚拟地址空间
    bsp_backup_mtrr_state();                                    //备份mtrr
    cpu_alloc_resources();                                      //给所有cpu分配资源
    cpu_load_resource();                                        //加载cpu资源
    ioapic_init();                                              //初始化ioapic
    hpet_init();                                                //初始化hpet
    detect_tsc_hz() ;                                           //探测tsc频率
    lapic_timer_init_per_cpu();
    efi_runtime_service_init();                                 //映射efi运行时服务到虚拟地址空间
    while(1);

    bus_init();                                                 //总线初始化
    ap_init();                                                  //初始化ap核

    while (1);
}
