#include "hpet.h"
#include "printk.h"
#include "../x64/cpu.h"

// HPET设备
hpet_device_t hpet_dev;

//hpet时钟
clocksource_t hpet_cs;

//hpet定时器，hpet有多个定时器暂时先注册一个
clockevent_t hpet_ce0;


// HPET 定时器配置位域宏
#define HPET_TN_INT_ENB_CNF   (1ULL << 2) // Bit 2: 允许该通道触发中断
#define HPET_TN_TYPE_CNF      (1ULL << 3) // Bit 3: 0 = One-Shot单次模式, 1 = 周期模式

// =========================================================================
// 1. 初始化通道硬件，并将当前 CPU 加入广播叫早名单
// =========================================================================
void hpet_ce_init_hw(clockevent_t *ce) {
    // 从 clockevent 抽象接口中取出物理设备实体
    hpet_device_t *dev = (hpet_device_t *)ce->priv;

    // 拿到第 0 号通道的逻辑上下文 (其中包含了我们要的锁和 mask)
    hpet_timer_t *timer = &dev->hpet_timers[0];
    uint32 cpu_id = THIS_CPU->logical_id;

    // 🌟 细粒度加锁：只锁住通道 0，绝不影响其它声卡/外设去抢通道 1 或通道 2
    spin_lock(&timer->lock);

    // 1. 软件状态：在花名册上打勾，告诉 0 号核发生中断时记得用 IPI 叫醒我
    timer->bc_cpu_mask |= (1ULL << cpu_id);

    // 2. 硬件状态：配置物理通道寄存器
    uint64 cfg = dev->hw_regs->timers[0].config_cap;
    cfg &= ~HPET_TN_TYPE_CNF;      // 清零 Bit 3，强制设为 One-Shot (单次绝对值模式)
    cfg |= HPET_TN_INT_ENB_CNF;    // 置位 Bit 2，允许硬件通道放行物理中断脉冲
    dev->hw_regs->timers[0].config_cap = cfg;

    spin_unlock(&timer->lock);
}

// =========================================================================
// 2. 停止当前 CPU 的广播服务；若无人使用则物理停表节电
// =========================================================================
void hpet_ce_stop_hw(clockevent_t *ce) {
    hpet_device_t *dev = (hpet_device_t *)ce->priv;
    hpet_timer_t *timer = &dev->hpet_timers[0];
    uint32 cpu_id = THIS_CPU->logical_id;

    spin_lock(&timer->lock);

    // 1. 软件状态：在花名册上划掉名字，我切走(或关机)了，别再发 IPI 烦我
    timer->bc_cpu_mask &= ~(1ULL << cpu_id);

    // 2. 硬件状态：如果全系统 64 个核都没有人挂在 HPET 闹钟上了
    //    彻底关闭物理通道 0 的中断开关，防止产生幽灵中断，并节省总线带宽
    if (timer->bc_cpu_mask == 0) {
        dev->hw_regs->timers[0].config_cap &= ~HPET_TN_INT_ENB_CNF;
        // 重置影子缓存，标记为“空仓”状态
        timer->programmed_ns = 0xFFFFFFFFFFFFFFFFULL;
    }

    spin_unlock(&timer->lock);
}


// =========================================================================
// HPET 定时器编程引擎：融合无锁快筛、绝对时间数学推导与防死锁回读
// =========================================================================
void hpet_ce_set_next_event(clockevent_t *ce, uint64 target_ns, uint64 now_ns) {
    hpet_device_t *dev = (hpet_device_t *)ce->priv;
    hpet_timer_t *timer = &dev->hpet_timers[0];

    // =====================================================================
    // 🌟 第一道防线：无锁极速过滤 (Lockless Fast-Path)
    // 保护南桥慢速总线：如果我设定的目标时间比全局缓存的最早时间还要晚，
    // 说明别人已经把闹钟设得比我还早，硬件一定会在我需要之前醒来。
    // 直接返回，坚决不碰锁，不写 MMIO！
    // =====================================================================
    if (target_ns >= *(volatile uint64 *)&timer->programmed_ns) {
        return;
    }

    // 只有我抢到了“全系统第一名”，才加锁操作硬件
    spin_lock(&timer->lock);

    // Double-Check，防止抢锁期间被其它 CPU 捷足先登
    if (target_ns < timer->programmed_ns) {
        // 1. 宣示主权，更新影子缓存
        timer->programmed_ns = target_ns;

        uint64 target_cycles;
        // 设定最小防漏步进 (约 5 微秒对应的硬件 tick 数)
        // 因为南桥 MMIO 读写极慢，从算完到写进比较器可能耗费几微秒
        uint64 min_ticks = ce->freq_hz / 200000ULL;

        // =====================================================================
        // 🌟 第二道防线：架构级同构/异构分流 (Architecture Fallback)
        // =====================================================================
        if (time_core_try_abs_ns_to_cycles(target_ns, CLOCKSOURCE_ID_HPET, &target_cycles)) {
            // [A. 同构路线]
            // 系统当前正好用 HPET 做时钟源！
            // 完美！核心层通过纯数学魔法直接算出了 HPET 的绝对目标比较刻度。
            // 零耗时，且完全不需要去读沉重的 MMIO 主计数器。
        } else {
            // [B. 异构降级路线]
            // 系统当前在用 TSC 等其它时钟源。数学推导失效。
            // 只能用上层传进来的 now_ns 快照，降维成相对时间（差值）。
            uint64 delay_ns = (target_ns > now_ns) ? (target_ns - now_ns) : 1000ULL;

            // 相对差值 (ns) 转 相对差值 (ticks)
            uint64 delay_cycles = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                                            + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);

            // 被迫承受一次读取 MMIO 的巨大延迟
            volatile uint64 *main_cnt = &dev->hw_regs->main_counter;
            target_cycles = *main_cnt + delay_cycles;
        }

        // 2. 轰入比较器寄存器
        volatile uint64 *comp_reg = &dev->hw_regs->timers[0].comparator;
        *comp_reg = target_cycles;

        // =====================================================================
        // 🌟 第三道防线：防死锁与漏触发回读保护 (Deadlock Watchdog)
        // 这是对付 HPET 的终极杀招！因为 HPET 硬件是严格的 '==' 触发，如果刚刚在
        // 抢锁、算数学公式或者写 MMIO 时，恰好被一波 NMI/SMI 中断偷袭，导致我们
        // 刚把 target_cycles 写进比较器，主计数器就已经越过了这个值...
        // 那么闹钟必须等 4 万年溢出才会响，系统彻底死锁！
        // =====================================================================
        volatile uint64 *main_cnt = &dev->hw_regs->main_counter;
        uint64 now_cnt = *main_cnt; // 刚写完立刻回读主计数器！

        // 如果 (目标 - 现实) 小于最小安全步进 (负数代表已经错过，正数代表离得太近)
        if ((int64)(target_cycles - now_cnt) < (int64)min_ticks) {
            // 哎呀，由于被打断或者目标太近，已经来不及了！
            // 强制将比较器设在当前时间点再往后推 5 微秒，让硬件立马“补”一枪中断！
            *comp_reg = now_cnt + min_ticks;
        }
    }

    spin_unlock(&timer->lock);
}


uint64 hpet_cs_read(clocksource_t *cs) {
    hpet_device_t *dev = cs->priv;
    return dev->hw_regs->main_counter;
}
