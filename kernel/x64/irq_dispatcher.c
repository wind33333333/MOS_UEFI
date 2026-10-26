#include "irq_dispatcher.h"
#include "cpu.h"
#include "printk.h"
#include "moslib.h"
#include "errno.h"

irq_override_info_t irq_override_info = {0};

//中断分发 汇编irq_cmn_stub -> c_irq_dispatcher
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
    if (vector < 32 || vector >= 256 || target_cpu_id >= cpu_info.total_cpu_count) return -EINVAL;

    // 2. 获取目标 CPU 的控制块指针 (核心：跨 CPU 寻址！)
    per_cpu_t *target_cpu = get_lid_by_per_cpu(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    // 3. 计算在 target_cpu->irq_table 中的实际索引 (0 ~ 223)
    uint32 irq_index = vector - 32;

    // 4. 🌟 跨核修改防护：获取目标 CPU 中断表的自旋锁 (保存中断状态)
    uint64 flags;
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    irq_desc_t *desc = &target_cpu->irq_table[irq_index];

    // ★ 防火墙 1：不准在没买过的地皮上盖房！
    if (desc->state == IRQ_STATE_FREE && vector < 0xE0) {
        spin_unlock_irqrestore(&target_cpu->irq_lock, flags);
        PR_ERROR("CPU %u IRQ %d is NOT allocated!\n", target_cpu_id, vector);
        return -EPERM;
    }

    // ★ 防火墙 2：不准在已经盖好房子的地皮上重复盖房！
    if (desc->state == IRQ_STATE_REGISTERED && vector < 0xE0) {
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
    if (vector < 32 || vector >= 256 || target_cpu_id >= cpu_info.run_cpu_count) return -EINVAL;

    // 2. 获取目标 CPU 的控制块指针 (核心：跨 CPU 寻址！)
    per_cpu_t *target_cpu = get_lid_by_per_cpu(target_cpu_id);
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
    if (count == 0 || align == 0 || target_cpu_id >= cpu_info.run_cpu_count) return -EINVAL;

    per_cpu_t *target_cpu = get_lid_by_per_cpu(target_cpu_id);
    if (!target_cpu) return -ENODEV;

    int32 allocated_base = -1;
    uint64 flags;

    // 🌟 跨核分配防护：必须获取目标 CPU 的 IRQ 表锁
    spin_lock_irqsave(&target_cpu->irq_lock, &flags);

    // 提前计算好搜索的理论上下界
    int32 upper_bound = IRQ_DYNAMIC_FAST_LIMIT - count + 1;
    int32 lower_bound = IRQ_DYNAMIC_LOW_BASE;

    int32 start, limit, step;

    if (strategy == IRQ_ALLOC_HIGH_SPEED) {
        // --- 策略 1：高速设备 (Top-Down 向下查找) ---
        // 数学魔法：向下取整到 align 的倍数
        start = (upper_bound / align) * align;
        limit = lower_bound;
        step  = - (int32)align;
    } else {
        // --- 策略 2：低速设备 (Bottom-Up 向上查找) ---
        // 数学魔法：向上取整到 align 的倍数
        start = ((lower_bound + align - 1) / align) * align;
        limit = upper_bound;
        step  = (int32)align;
    }

    // =========================================================
    // 核心遍历逻辑 (完全合并，且消灭了 v % align != 0 的判断！)
    // 这里的循环条件利用了三元运算符，优雅兼容向上和向下双向遍历
    // =========================================================
    for (int32 v = start; (step > 0) ? (v <= limit) : (v >= limit); v += step) {

        boolean available = TRUE;

        // 检查这连续的 count 个位置是否全为 FREE
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
        target_cpu_id >= cpu_info.run_cpu_count) {
        return -EINVAL;
        }

    per_cpu_t *target_cpu = get_lid_by_per_cpu(target_cpu_id);
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




