#include "gdt_tss.h"

#include "cpu.h"
#include "slub.h"
#include "vmalloc.h"

typedef struct {
    uint64 null_desc;          // 0x00: Reserve / Null Descriptor
    uint64 kernel_code64_desc; // 0x08: Ring 0 代码段 (64-bit)
    uint64 kernel_data_desc;   // 0x10: Ring 0 数据段 (通用)
    uint64 user_code32_desc;   // 0x18: Ring 3 代码段 (32-bit 兼容模式)
    uint64 user_data_desc;     // 0x20: Ring 3 数据段 (通用)
    uint64 user_code64_desc;   // 0x28: Ring 3 代码段 (64-bit原生)

    // x86_64 下 TSS 描述符强制占用 16 字节 (两个 8 字节槽位)
    uint64 tss_desc[2];        // 0x30 ~ 0x3F: TSS Descriptor
} __attribute__((packed)) gdt_t;

typedef struct{
    uint16 limit;
    gdt_t *base;
} __attribute__((packed)) gdt_ptr_t;


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


static inline void asm_lgdt(const gdt_t *gdt_ptr, uint16 code64_sel, uint16 data64_sel) {
    __asm__ __volatile__(
            "lgdtq       %0                  \n\t"  // 加载 GDT 描述符地址
            "pushq       %q1                 \n\t"  // 压入代码段选择器
            "leaq        1f(%%rip), %%rax    \n\t"  // 获取返回地址
            "pushq       %%rax               \n\t"  // 压入返回地址
            "lretq                           \n\t"  // 执行长返回，切换到新代码段选择子
            "1:                              \n\t"  // 跳转目标标记
            "movw        %2, %%ss            \n\t"  // 设置堆栈段选择器
            "movw        %2, %%ds            \n\t"  // 设置数据段选择器
            "movw        %2, %%es            \n\t"  // 设置额外段选择器
            :
            : "m"(*gdt_ptr), "r"(code64_sel), "r"(data64_sel)
            : "memory", "%rax"
            );
}



static inline void asm_ltr(uint16 tss_sel) {
    __asm__ __volatile__(
            "ltr    %w0 \n\t"
            :
            : "r"(tss_sel)
            :
            );
}

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

    asm_lgdt(gdt,8,16);
    asm_ltr(48);
}