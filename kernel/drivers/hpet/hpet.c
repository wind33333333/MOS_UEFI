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
// 3. 设置下一次唤醒时间 (含无锁快筛与硬件防死锁回读)
// =========================================================================
void hpet_ce_set_next_delay_ns(clockevent_t *ce, uint64 delay_ns) {
    hpet_device_t *dev = (hpet_device_t *)ce->priv;
    hpet_timer_t *timer = &dev->hpet_timers[0];

    // 获取本 CPU 内核调度器决定的绝对唤醒时间
    uint64 my_deadline_ns = THIS_CPU->next_deadline_ns;

    // =====================================================================
    // 🌟 第一道防线：无锁极速过滤 (Lockless Fast-Path)
    // 直接偷看通道内的影子缓存 (programmed_ns)。如果我设的闹钟比当前底层正在
    // 等待的全局最早闹钟还要【晚】，我根本不需要去抢锁，更不用写慢速南桥 MMIO！
    // =====================================================================
    if (my_deadline_ns >= *(volatile uint64 *)&timer->programmed_ns) {
        return;
    }

    // 只有我抢到了“全系统第一名 (最早到期)”，才需要加锁改写真实硬件
    spin_lock(&timer->lock);

    // 加锁后 Double-Check，防止抢锁期间被其它 CPU 捷足先登
    if (my_deadline_ns < timer->programmed_ns) {
        // 1. 更新影子缓存，宣示主权
        timer->programmed_ns = my_deadline_ns;

        // 2. 将通用的纳秒 (ns) 换算成 HPET 硬件芯片自己的原始 Tick 周期数
        uint64 ticks = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                                 + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);

        // 设定最小防漏步进（保守值：例如至少往后推 5 微秒对应的 tick 数）
        // 因为 MMIO 跨南桥写入可能耗费 1~2 微秒，写太短会导致漏触发
        uint64 min_ticks = ce->freq_hz / 200000ULL;
        if (ticks < min_ticks) {
            ticks = min_ticks;
        }

        // 准备操作硬件寄存器
        volatile uint64 *main_cnt = &dev->hw_regs->main_counter;
        volatile uint64 *comp_reg = &dev->hw_regs->timers[0].comparator;

        // 3. 算出绝对目标触发值：当前正在不断飞奔的主计数器读数 + 差值 ticks
        uint64 now_cnt = *main_cnt;
        uint64 target_cnt = now_cnt + ticks;

        // 发送给物理比较器
        *comp_reg = target_cnt;

        // =====================================================================
        // 🌟 第二道防线：防死锁回读保护 (Deadlock Watchdog)
        // HPET 比较器是“绝对相等(==)”触发。如果刚刚那一通操作耗时太久，
        // 导致主计数器已经超越了 target_cnt，那么相等时刻已经错过，
        // 硬件要等 64 位跑满溢出一圈 (约 4 万年) 才会再响！系统当场死锁！
        // 办法：赶紧回读一眼主计数器。如果发现错过了，往前推一个安全身位强制补救！
        // =====================================================================
        now_cnt = *main_cnt;
        if ((int64)(target_cnt - now_cnt) <= 0) {
            // 哎呀迟到了！强制把它设在当前时间再往后几微秒，让硬件立马“补”一枪中断
            *comp_reg = now_cnt + min_ticks;
        }
    }

    spin_unlock(&timer->lock);
}


uint64 hpet_cs_read(clocksource_t *cs) {
    hpet_device_t *dev = cs->priv;
    return dev->hw_regs->main_counter;
}
