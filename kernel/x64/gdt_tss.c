#include "gdt_tss.h"
#include "cpu.h"
#include "slub.h"
#include "vmalloc.h"




// 1. 基础段类型 (统一，没必要区分32/64)
#define TYPE_CODE           (0xAUL << 40)  // 可执行、可读代码段
#define TYPE_DATA           (0x2UL << 40)  // 可读、可写数据段

// 2. 基础属性
#define S                   (1UL << 44)    // 1=非系统段 (普通代码/数据段)
#define DPL_0               (0UL << 45)    // 特权级 Ring 0
#define DPL_3               (3UL << 45)    // 特权级 Ring 3
#define P                   (1UL << 47)    // Present (存在位)
#define L                   (1UL << 53)    // 64位代码段标志 (Long Mode)
#define DB                  (1UL << 54)    // 32位操作数/堆栈标志 (Default Size)
#define G                   (1UL << 55)    // 4KB 粒度 (Granularity)
#define LIMIT_4G            (0xFUL << 48 | 0xFFFF) // 配合G位表示 4GB 限制

// 3. 终极组合 (5 个核心段)
// =====================================================================
// 内核态 (Ring 0)
#define KERNEL_CODE64_DESC  (TYPE_CODE | DPL_0 | S | P | L)
#define KERNEL_CODE32_DESC  (TYPE_CODE | DPL_0 | S | P | LIMIT_4G | DB | G)
#define KERNEL_DATA_DESC    (TYPE_DATA | DPL_0 | S | P | LIMIT_4G | DB | G)

// 用户态 (Ring 3) - 用户态需要兼容 32位 和 64位
#define USER_CODE64_DESC    (TYPE_CODE | DPL_3 | S | P | L)
#define USER_CODE32_DESC    (TYPE_CODE | DPL_3 | S | P | LIMIT_4G | DB | G)
#define USER_DATA_DESC      (TYPE_DATA | DPL_3 | S | P | LIMIT_4G | DB | G) // 32位和64位通用！



/**
 * @brief 将 TSS 挂载到 GDT 中 (极限化简版)
 * @param tss_desc_ptr 指向 GDT 中 tss_desc[2] 数组的首地址
 * @param base         TSS 结构体的 64 位虚拟首地址
 *
 * @note 64位 TSS 固定大小 104 字节，Limit 强制硬编码为 0x67
 *       Type 强制硬编码为 0x89 (Available 64-bit TSS)
 */
static inline void set_tss_desc(uint64 *tss_desc_ptr, tss_t *tss) {
    /*
     * 低 8 字节 (Low 64-bit) 极限位运算组合：
     * [0:15]   = 0x0067 (Limit 15:0)
     * [16:39]  = Base 23:0 (巧妙地用 base & 0xFFFFFF 一次性填入)
     * [40:47]  = 0x89 (Type & Attr)
     * [48:55]  = 0x00 (Limit 19:16 + Flags 全为 0)
     * [56:63]  = Base 31:24
     */
    tss_desc_ptr[0] = (sizeof(tss_t)-1)
                    | ((((uint64)tss) & 0xFFFFFFULL) << 16)
                    | (0x89ULL << 40)
                    | ((((uint64)tss >> 24) & 0xFFULL) << 56);

    /* 高 8 字节 (High 64-bit)：直接就是 Base 的高 32 位 */
    tss_desc_ptr[1] = (uint64)tss >> 32;
}


void gdt_tss_init(uint32 logical_id) {
    gdt_t *gdt = kzalloc(sizeof(gdt_t));
    gdt->kernel_code64_desc = KERNEL_CODE64_DESC;
    gdt->kernel_data_desc = KERNEL_DATA_DESC;
    gdt->user_code32_desc = USER_CODE32_DESC;
    gdt->user_data_desc = USER_DATA_DESC;
    gdt->user_code64_desc = USER_CODE64_DESC;

    tss_t *tss = kzalloc(sizeof(tss_t));
    cpu_cores[logical_id].tss = tss;
    set_tss_desc(gdt->tss_desc,tss);
    tss->rsp0 = (uint64)vmalloc(4*4096) + 4*4096;       // 内核栈 16K
    tss->ist1 = (uint64)vmalloc(2*4096) + 2*4096;       // 8: Double Fault (#DF) 双重故障 8K
    tss->ist2 = (uint64)vmalloc(2*4096) + 2*4096;       // 2: NMI 不可屏蔽中断 8K
    tss->ist3 = (uint64)vmalloc(2*4096) + 2*4096;       // 18: Machine Check (#MC) 机器检查 8K
    tss->ist4 = (uint64)vmalloc(2*4096) + 2*4096;       // 1: Debug (#DB) 调试异常 8K

}