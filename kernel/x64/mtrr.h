#include "../include/moslib.h"

#pragma once

/* =====================================================================
 * 1. MTRR 缓存属性宏 (Memory Types)
 * 写入 MTRR 寄存器或从其中读出的属性对应值
 * ===================================================================== */
#define MTRR_TYPE_UC        0x00  // Uncacheable (强不可缓存)
#define MTRR_TYPE_WC        0x01  // Write-Combining (写合并)
#define MTRR_TYPE_WT        0x04  // Write-Through (写透)
#define MTRR_TYPE_WP        0x05  // Write-Protected (写保护)
#define MTRR_TYPE_WB        0x06  // Write-Back (完全缓存)

/* =====================================================================
 * 5. 寄存器位域掩码 (Bitmasks for parsing)
 * ===================================================================== */
#define MTRR_DEF_TYPE_ENABLE    (1ULL << 11)  // 全局使能位
#define MTRR_DEF_TYPE_FIX_EN    (1ULL << 10)  // 固定范围使能位
#define MTRR_PHYSMASK_VALID     (1ULL << 11)  // Variable Mask 的 Valid 位


/* * 绝大多数现代 CPU (Intel/AMD) 的可变 MTRR 数量为 8 或 10 条。
 * 虽然理论上限是 255，但在物理硅片上从未出现过大于 10 的情况。
 * 为保证内核结构的精简与未来的极度安全边界，我们定义 32 为上限。
 */
#define NOVA_MAX_VAR_MTRRS 32

// 定义一对可变范围 MTRR (Base + Mask)
typedef struct {
 uint64 base; // 对应 MTRRphysBase(n)
 uint64 mask; // 对应 MTRRphysMask(n)
} mtrr_var_pair_t;

// MTRR 完整状态快照结构体
// 用于 BSP 在唤醒 AP 之前保存完整的 MTRR 状态，供 AP 在实模式/保护模式早期克隆
typedef struct {
 // 1. 全局与默认控制
 uint64 def_type;          // MTRRdefType (0x2FF) 寄存器的值

 // 2. 硬件能力计数
 uint32 vcnt;              // 实际可用的可变寄存器对数 (从 MTRRcap 的 [7:0] 读出)

 // 3. 可变范围 MTRR (Variable-Range)
 // 数组大小固定为 32，但实际上只有前 vcnt 个元素包含有效数据
 mtrr_var_pair_t var[NOVA_MAX_VAR_MTRRS];

 // 4. 固定范围 MTRR (Fixed-Range)
 // 共 11 个 64 位寄存器。
 // 映射关系：
 // fixed[0]    -> 0x250
 // fixed[1..2] -> 0x258, 0x259
 // fixed[3..10]-> 0x268 ~ 0x26F
 uint64 fixed[11];

} mtrr_state_t;

void bsp_backup_mtrr_state(void);