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
 * 2. 能力与全局控制寄存器 (Capability & Def Type)
 * ===================================================================== */
// MTRR 能力寄存器 (只读)
// [7:0] VCNT: 支持多少对可变范围寄存器 (通常是 8 或 10)
// [8] FIX: 是否支持固定范围 MTRR
// [10] WC: 是否支持 WC 属性
#define MSR_MTRRcap             0x000000FE

// MTRR 默认类型与全局使能寄存器
// [7:0] 默认属性 (如果地址没命中任何MTRR规则，就用这个，通常是 UC)
// [10] FE: 开启固定范围 MTRR
// [11] E: 开启全局 MTRR
#define MSR_MTRRdefType         0x000002FF

/* =====================================================================
 * 3. 可变范围 MTRR 寄存器 (Variable-Range MTRRs)
 * 这是系统内存布局的核心，通常有 8 对 (Base + Mask)
 * ===================================================================== */
// 基址寄存器起点
#define MSR_MTRRphysBase_BASE   0x00000200
// 掩码寄存器起点
#define MSR_MTRRphysMask_BASE   0x00000201

// 辅助宏：获取第 n 对 MTRR 的 MSR 地址 (n = 0 ~ 7)
// Base 寄存器存：物理起始地址 + 属性
#define MSR_MTRRphysBase(n)     (MSR_MTRRphysBase_BASE + 2 * (n))
// Mask 寄存器存：物理地址掩码 + Valid(有效位)
#define MSR_MTRRphysMask(n)     (MSR_MTRRphysMask_BASE + 2 * (n))

/* =====================================================================
 * 4. 固定范围 MTRR 寄存器 (Fixed-Range MTRRs)
 * 专门用于精细控制最初的 1MB 物理内存 (0x00000 ~ 0xFFFFF)
 * 主要为了兼容古老的 VGA 显存段和 BIOS ROM 段
 * 现代 OS 中通常只需读一遍审计，绝不修改。
 * ===================================================================== */
// 掌管 0x00000 ~ 0x7FFFF (共 512KB)，分为 8 个 64KB 的块
#define MSR_MTRRfix64K_00000    0x00000250

// 掌管 0x80000 ~ 0xBFFFF (共 256KB)，分为 2 个寄存器，每个掌管 8 个 16KB 的块
#define MSR_MTRRfix16K_80000    0x00000258
#define MSR_MTRRfix16K_A0000    0x00000259 // A0000 是经典 VGA 显存段起始

// 掌管 0xC0000 ~ 0xFFFFF (共 256KB)，分为 8 个寄存器，每个掌管 8 个 4KB 的块
#define MSR_MTRRfix4K_C0000     0x00000268
#define MSR_MTRRfix4K_C8000     0x00000269
#define MSR_MTRRfix4K_D0000     0x0000026A
#define MSR_MTRRfix4K_D8000     0x0000026B
#define MSR_MTRRfix4K_E0000     0x0000026C
#define MSR_MTRRfix4K_E8000     0x0000026D
#define MSR_MTRRfix4K_F0000     0x0000026E // BIOS ROM 段
#define MSR_MTRRfix4K_F8000     0x0000026F // BIOS ROM 段

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
 uint64 base; // 对应 MSR_MTRRphysBase(n)
 uint64 mask; // 对应 MSR_MTRRphysMask(n)
} mtrr_var_pair_t;

// MTRR 完整状态快照结构体
// 用于 BSP 在唤醒 AP 之前保存完整的 MTRR 状态，供 AP 在实模式/保护模式早期克隆
typedef struct {
 // 1. 全局与默认控制
 uint64 def_type;          // MSR_MTRRdefType (0x2FF) 寄存器的值

 // 2. 硬件能力计数
 uint32 vcnt;              // 实际可用的可变寄存器对数 (从 MSR_MTRRcap 的 [7:0] 读出)

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