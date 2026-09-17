#pragma once
#include "moslib.h"

// --------------------------------------------------------------------------
// 1. CPU 特性位图掩码 (根据你的需求扩展，比如 CPUID 检测结果)
// --------------------------------------------------------------------------
#define X86_FEATURE_INVPCID  (1ULL << 0)
#define X86_FEATURE_FSGSBASE (1ULL << 1)

// --------------------------------------------------------------------------
// 2. 动态修补档案结构体 (与汇编严格对应，总长 12 字节)
// --------------------------------------------------------------------------
struct alt_instr {
    int32  instr_offset;    // 老机器码相对于本结构体的地址偏移
    int32  repl_offset;     // 新机器码相对于本结构体的地址偏移
    uint16 cpuid_feature;   // 触发修补的 CPU 特性掩码
    uint8  instrlen;        // 老指令(含防踩踏垫片)的长度
    uint8  replacementlen;  // 新指令长度
} __attribute__((packed));

// --------------------------------------------------------------------------
// 3. 👑 终极通用修补宏 (纯文本拼接，极度优雅)
// --------------------------------------------------------------------------
/**
 * @param oldinstr 默认的老指令 (字符串)
 * @param newinstr 如果硬件支持，则替换成的新指令 (字符串)
 * @param feature  触发替换的硬件特性 ID
 *
 * @note  巧妙使用 661f / 661b 这种 GNU 局部标签，
 *        保证这个宏在同一个 C 文件里被调用 1000 次也不会发生标签名冲突！
 */
// 🌟 1. 添加这两个神奇的字符串化宏 (直接抄自 Linux 源码)
#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define ALT_INSTR(oldinstr, newinstr, feature)                  \
"661:\n\t"                                                  \
oldinstr "\n\t"                                             \
"662:\n\t"                                                  \
".skip -(((664f - 663f) - (662b - 661b)) > 0) * "           \
"((664f - 663f) - (662b - 661b)), 0x90\n\t"          \
"6621:\n\t"                                                 \
".pushsection .altinstr_replacement, \"ax\"\n\t"            \
"663:\n\t"                                                  \
newinstr "\n\t"                                             \
"664:\n\t"                                                  \
".popsection\n\t"                                           \
".pushsection .altinstructions, \"a\"\n\t"                  \
".long 661b - .\n\t"                                        \
".long 663b - .\n\t"                                        \
/* 👇 核心修复：用 __stringify 替代 #feature */                 \
".word " __stringify(feature) "\n\t"                        \
".byte 6621b - 661b\n\t"                                    \
".byte 664b - 663b\n\t"                                     \
".popsection\n\t"

void apply_alternatives(uint64 cpu_features_mask);