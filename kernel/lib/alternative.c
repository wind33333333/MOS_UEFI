#include "alternative.h"

// 链接器导出的符号
extern struct alt_instr __alt_instructions[];
extern struct alt_instr __alt_instructions_end[];

/**
 * @brief  系统级动态二进制修补执行函数
 * @param  cpu_features_mask 当前 CPU 探测到的所有特性位图
 */
void apply_alternatives(uint64 cpu_features_mask) {
    // 1. 关闭内存写保护 (WP位)
    uint64 cr0 = asm_get_cr0();
    asm_set_cr0(cr0 & ~(1ULL << 16));

    // 2. 遍历所有档案
    for (struct alt_instr *alt = __alt_instructions; alt < __alt_instructions_end; alt++) {

        // 验证：CPU 是否具有触发此修补的特性？
        if ((cpu_features_mask & alt->cpuid_feature) == 0) {
            continue; // 保持默认老指令，跳过
        }

        // 定位真实物理地址
        uint8 *old_ptr = (uint8 *)alt + alt->instr_offset;
        uint8 *new_ptr = (uint8 *)alt + alt->repl_offset;

        // 覆盖手术
        for (int i = 0; i < alt->replacementlen; i++) {
            old_ptr[i] = new_ptr[i];
        }

        // 尾部清理 (动态 NOP)
        // 如果新指令比老指令短，后面多出来的老废料必须填成 0x90
        for (int i = alt->replacementlen; i < alt->instrlen; i++) {
            old_ptr[i] = 0x90;
        }
    }

    // 3. 恢复写保护并冲刷 I-Cache
    asm_set_cr0(cr0);
    __asm__ __volatile__("cpuid" ::: "memory", "eax", "ebx", "ecx", "edx");
}
