/* ==================================================================== */
/* 全局内核虚拟地址空间常量定义 (与链接脚本及 C 语言宏保持高度一致)           */
/* ==================================================================== */
.equ DIRECT_MAP_VA_START, 0xFFFF800000000000    // 物理内存直接映射区起始虚拟地址
.equ KERNEL_VA_START,     0xFFFFFFFF80000000    // 高半核内核代码/数据区起始虚拟地址


/*************************** 内核初始化代码入口 *******************************/
.code64
.section    .init_text, "ax"
.globl _start
_start:
    cli     // 彻底屏蔽外部中断，直到我们完全准备好 IDT

    /* ==================================================================== */
    /* 阶段 0：接管宇宙 - 立即加载专属 GDT 并清洗所有段寄存器                    */
    /* 这是系统极其关键的“净身”操作，彻底抹除 UEFI/Bootloader 留下的隐患。        */
    /* ==================================================================== */

    // 1. 动态修补 GDT 描述符基址并加载
    leaq     tmp_gdt(%rip), %rax
    leaq     tmp_gdt_ptr(%rip), %rcx
    movq     %rax, 2(%rcx)              // 消除 PIE(位置无关代码)带来的偏移，填入真实物理地址
    lgdt     (%rcx)                     // 第一时间接管全局描述符表！

    // 2. 清洗数据段寄存器 (使用 0x18 即我们的 64位读写数据段)
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss
    xorw     %ax, %ax                   // FS 和 GS 通常清零，留给后期 TLS/Per-CPU 机制
    movw     %ax, %fs
    movw     %ax, %gs

    // 3. 洗礼代码段：远跳转强制刷新 CS 寄存器为我们的 64 位代码段 (0x08)
    // 64位长模式下无法直接使用 jmp 刷新 CS，必须用 push + lretq 技巧
    leaq     .reload_cs(%rip), %rax
    pushq    $0x08                      // 压入专属 64-bit Code Segment
    pushq    %rax                       // 压入目标地址 (.reload_cs)
    lretq                               // 华丽空翻，刷新 CS！

.reload_cs:
    // 【至此，整个系统 100% 纯净地运行在我们自己的 GDT 体系下！】

    // 保存 loader(Bootloader/UEFI) 通过 rdi 传递过来的 tmp_boot_info 指针
    movq     %rdi, tmp_boot_info(%rip)


    /* ==================================================================== */
    /* 阶段 1：构建基础的 4 级临时页表 (PML4)                                 */
    /* 无论后续硬件是否支持或切换 5 级分页，这个 4 级页表都是一切映射的基石。       */
    /* ==================================================================== */

    // 1. 初始化 PML4[0] (平坦恒等映射 0x0 - 0x8000000000, 覆盖前 512GB 物理内存)
    leaq     tmp_pml4t(%rip), %rdi
    leaq     tmp_pdpt0(%rip), %rax
    orq      $0x3, %rax                 // 0x3 = PAGE_PRESENT | PAGE_RW (存在且可读写)
    movq     %rax, (%rdi)               // 填入 PML4 第 0 项

    // 2. 初始化 PML4[256] (物理内存直接映射区 DIRECT_MAP_VA_START)
    movq     $DIRECT_MAP_VA_START, %rsi
    shrq     $39, %rsi                  // 提取 PML4 索引偏移 (第 39~47 位)
    andq     $0x1ff, %rsi
    shlq     $3, %rsi                   // 乘以 8 字节得到数组偏移量
    movq     %rax, (%rdi, %rsi)         // 同样指向 tmp_pdpt0

    // 3. 初始化 PML4[511] (内核代码执行区 KERNEL_VA_START, 即 0xFFFFFFFF80...)
    leaq     tmp_pdpt1(%rip), %rax
    orq      $0x3, %rax
    movq     $KERNEL_VA_START, %rsi
    shrq     $39, %rsi
    andq     $0x1ff, %rsi
    shlq     $3, %rsi
    movq     %rax, (%rdi, %rsi)         // 指向专用的 tmp_pdpt1

    // 4. 填充 PDPT0 (映射 512 个 1GB 大页，覆盖 0~512GB 物理内存)
    leaq     tmp_pdpt0(%rip), %rdi
    movq     $0x183, %rax               // 0x183 = PRESENT | RW | HUGE_PAGE (开启大页)
    movq     $512, %rcx                 // 循环 512 次
.init_tmp_pdptt0:
    stosq                               // 将 RAX 填入 RDI 指向的内存，并 RDI+=8
    addq     $0x40000000, %rax          // RAX 累加 1GB 的物理步长
    loopq    .init_tmp_pdptt0

    // 5. 填充 PDPT1 (专为 KERNEL 区域映射最后 2GB 的虚拟空间)
    leaq     tmp_pdpt1(%rip), %rdi
    movq     $KERNEL_VA_START, %rsi
    shrq     $30, %rsi                  // 提取 PDPT 索引 (第 30~38 位)
    andq     $0x1ff, %rsi
    shlq     $3, %rsi

    // 提取当前内核代码实际加载的物理基址，并按照 1GB 对齐挂载
    leaq     _start(%rip), %rax
    movq     $0xFFFFFFFFC0000000, %rbx
    andq     %rbx, %rax                 // 向下对齐到 1GB 物理边界
    orq      $0x183, %rax               // 挂载 1GB 大页标志
    movq     %rax, (%rdi, %rsi)         // 填入 PDPT1[510]
    addq     $0x40000000, %rax          // 增加 1GB 物理地址
    movq     %rax, 8(%rdi, %rsi)        // 填入 PDPT1[511]


    /* ==================================================================== */
    /* 阶段 2：LA57 (5级分页) 智能探测与路由                                  */
    /* ==================================================================== */

    // 【探测 1】：检查当前 Bootloader (UEFI) 是否已经帮我们开启了 5 级分页？
    movq     %cr4, %rax
    btq      $12, %rax                  // 测试 CR4.LA57 (Bit 12)
    jc       .already_in_la57           // 如果已开启，直接去挂载 PML5，跳过 32 位蹦床

    // 【探测 2】：UEFI 没开，那物理 CPU 是否支持 5 级分页机制？
    movl     $7, %eax
    xorl     %ecx, %ecx
    cpuid
    btl      $16, %ecx                  // 测试 CPUID (EAX=7, ECX=0) 的 ECX 第 16 位
    jc       .need_trampoline_la57      // 硬件支持！走蹦床代码强行开启 5 级分页


    /* ==================================================================== */
    /* 阶段 3：分页机制分支处理逻辑                                          */
    /* ==================================================================== */

    // 【后备分支】：硬件太老，不支持 LA57，老老实实用 4 级分页
    movb     $4, tmp_paging_level(%rip) // 记录内核状态为 4 级
    leaq     tmp_pml4t(%rip), %rax
    movq     %rax, %cr3                 // 真正启用刚才写好的 4 级页表
    jmp      .prepare_c_env             // 去准备 C 环境

.already_in_la57:
    movb     $5, tmp_paging_level(%rip) // 记录内核状态为 5 级
    call     .build_pml5_bridge         // 构造神级 PML5 桥接页表
    leaq     tmp_pml5t(%rip), %rax
    movq     %rax, %cr3                 // 直接刷新 CR3 即可，无需降级 32 位！
    jmp      .prepare_c_env             // 去准备 C 环境

.need_trampoline_la57:
    movb     $5, tmp_paging_level(%rip)
    call     .build_pml5_bridge         // 构造神级 PML5 桥接页表

    // 准备跳转蹦床所需的平坦物理地址 (因为开启分页瞬间 RIP 必须合法)
    leaq     tmp_pml5t(%rip), %rdi       // RDI = 新的 PML5 物理基址 (传给 tramp32 用)
    leaq     trampoline_64(%rip), %rsi   // RSI = 蹦床完跳回 64 位的物理地址
    leaq     trampoline_32(%rip), %rax   // RAX = 32 位兼容模式蹦床入口

    // 惊险跳跃：远跳转进入 32 位兼容模式
    // 感谢阶段 0，这里的 0x10 指向的绝对是我们自己的 tmp_gdt 里的 32 位段！
    pushq    $0x10                       // 压入 32 位 Code Segment 选择子
    pushq    %rax                        // 压入 32 位入口 EIP
    lretq


    /* ==================================================================== */
    /* 辅助子例程：构造神级折叠 PML5 桥接页表                                */
    /* 此机制能让 5 级目录完美兼容并重用所有的 4 级目录结构，极大地节省了代码。  */
    /* ==================================================================== */
.build_pml5_bridge:
    leaq     tmp_pml5t(%rip), %rdi
    leaq     tmp_pml4t(%rip), %rax
    orq      $0x3, %rax                 // PML5 指向 PML4 (PRESENT | RW)

    // 1. PML5[0] 指向 PML4 (维持平坦映射)
    movq     %rax, (%rdi)

    // 2. PML5[256] 指向 PML4 (适配 LA57 的 DIRECT_MAP_VA_START 映射区)
    movq     %rax, 2048(%rdi)

    // 3. PML5[511] 指向 PML4 (适配 KERNEL_VA_START 静态高半核映射区)
    movq     %rax, 4088(%rdi)
    ret


/* ==================================================================== */
/* 阶段 4：极其惊险的 32 位空中换擎蹦床区 (Trampoline)                       */
/* ==================================================================== */
.code32
trampoline_32:
    // 1. 强制加载 32 位兼容模式数据段 (0x18 兼容 32 位/64 位数据)
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss

    // 2. 拔掉网线：关闭分页 (CR0.PG = 0) - 进入纯物理寻址！
    movl     %cr0, %eax
    btrl     $31, %eax
    movl     %eax, %cr0

    // 3. 更换核芯：拨动 5 级分页硬件开关 (CR4.LA57 = 1)
    movl     %cr4, %eax
    btsl     $12, %eax
    movl     %eax, %cr4

    // 4. 将刚才构造的 PML5 物理基址强行注入 CR3 (地址由 %edi 传入)
    movl     %edi, %cr3

    // 5. 插上网线：重新开启分页 (CR0.PG = 1) - 此时宇宙已扩张到 64 PB！
    movl     %cr0, %eax
    btsl     $31, %eax
    movl     %eax, %cr0

    // 6. 浴火重生：远跳转，回归全新的 64 位宽广模式 (CS 恢复为 0x08)
    pushl    $0x08
    pushl    %esi                       // 之前存放在 RSI 里的 64 位返回地址
    lret

.code64
trampoline_64:
    // 恢复 64 位的数据段，准备迎接最后的跨越
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss


/* ==================================================================== */
/* 阶段 5：环境就绪，打补丁并飞跃至高半核 C 语言内核主入口                       */
/* ==================================================================== */
.prepare_c_env:

    // 1. 更新 GDT 描述符基址并加载
    leaq     tmp_gdt(%rip), %rax
    movabs   $KERNEL_VA_START, %rbx
    orq      %rbx, %rax
    leaq     tmp_gdt_ptr(%rip), %rcx
    movq     %rax, 2(%rcx)              // 消除 PIE(位置无关代码)带来的偏移，填入gdt虚拟地址
    lgdt     (%rcx)                     // 第一时间接管全局描述符表！

    /*
     * 动态修补临时 64 位 TSS 描述符并加载。
     * 此时 GDTR 稳稳指向 tmp_gdt，TSS 描述符位于偏移 0x20 处 (第 5 个槽位)
     */
     
    // 1. 获取 tmp_tss 最终的高半核虚拟首地址到 %rax
    leaq     tmp_tss(%rip), %rax
    movabs   $KERNEL_VA_START, %rbx
    orq      %rbx, %rax

    // 2. 获取 tmp_gdt 的当前内存地址到 %rdi
    leaq     tmp_gdt(%rip), %rdi

    // 3. 开始切片填坑 (由于链接器不支持复杂运算，纯手工在汇编里塞入)
    movw     %ax, 0x22(%rdi)            // Base [15:0] 填入

    movq     %rax, %rcx
    shrq     $16, %rcx
    movb     %cl, 0x24(%rdi)            // Base [23:16] 填入

    movq     %rax, %rcx
    shrq     $24, %rcx
    movb     %cl, 0x27(%rdi)            // Base [31:24] 填入

    movq     %rax, %rcx
    shrq     $32, %rcx
    movl     %ecx, 0x28(%rdi)           // Base [63:32] 填入

    // 4. 加载 TSS，一旦 C 代码里发生异常，CPU 将顺着这个 TSS 找到安全栈！
    movw     $0x20, %cx
    ltr      %cx

    /* ======================= 终极跃迁准备 ============================== */

    // 计算并设置当前 BSP 核心的高半核虚拟栈顶指针
    // 这里完美利用了汇编符号链接时的绝对地址特性
    leaq     tmp_bsp_stack_top(%rip), %rsp
    movabs   $KERNEL_VA_START, %rax
    orq      %rax, %rsp

    // 计算内核 C 语言入口 (kernel_init) 的高半核虚拟地址
    leaq     kernel_init(%rip), %rax
    movabs   $KERNEL_VA_START, %rbx
    orq      %rbx, %rax

    // 发动绝对空间跃迁：脱离低内存泥潭，永远跳入 0xFFFFFFFF80000000 空间！
    jmp      *%rax


/********************** 内核初始化数据静态分配区 **************************/
.section    .init_data, "wa"

    // 页表必须严格 4KB 对齐
    .balign 4096

    // 为 BSP 核心预留的 16KB 临时安全堆栈
    tmp_bsp_stack:  .fill   0x4000, 1, 0
    tmp_bsp_stack_top:

    .globl  tmp_pml5t
    tmp_pml5t:  .fill   0x1000, 1, 0    // 预留 PML5 物理页

    .globl  tmp_pml4t
    tmp_pml4t:  .fill   0x1000, 1, 0    // 预留 PML4 物理页

    tmp_pdpt0:  .fill   0x1000, 1, 0    // 用于平坦映射和直接映射区
    tmp_pdpt1:  .fill   0x1000, 1, 0    // 专用于 KERNEL 代码映射

    /*
     * 临时 TSS (Task State Segment) 内存区 (104 字节)
     */
    .balign 64                          // Cache Line 对齐，防止 SMP 伪共享
tmp_tss:
    .long 0                             // 0x00: reserved0
    /*
     * 链接器在链接阶段会自动赋予 tmp_bsp_stack_top 其处于
     * 0xFFFFFFFF80000000 空间的虚拟地址，直接作为内核 Ring 0 的救命堆栈！
     */
    .quad tmp_bsp_stack_top             // 0x04: rsp0 (Ring 0 内核栈)
    .quad 0                             // 0x0C: rsp1 (废弃)
    .quad 0                             // 0x14: rsp2 (废弃)
    .quad 0                             // 0x1C: reserved1
    .quad tmp_bsp_stack_top, 0, 0, 0, 0, 0, 0           // 0x24: ist1 ~ ist7 (早期无需初始化)
    .quad 0                             // 0x5C: reserved2
    .word 0                             // 0x64: reserved3
    .word 0x0068                        // 0x66: iomap_base (104, 彻底拦截 Ring3 的 I/O 操作)


    // 蹦床跳跃专用的临时全局描述符表 (GDT)
    .balign 16
tmp_gdt:
    .quad 0x0000000000000000            // 0x00: Null / Reserved
    .quad 0x00AF9A000000FFFF            // 0x08: 64-bit Kernel Code (Ring 0)
    .quad 0x00CF9A000000FFFF            // 0x10: 32-bit Compat Code (Ring 0, 蹦床专用)
    .quad 0x00CF92000000FFFF            // 0x18: 32/64-bit Data (Ring 0, 兼容所有)

    /*
     * 0x20: 临时 64 位 TSS 描述符 (占 16 字节 = 2 个槽位)
     * 静态声明时 Base 地址全填 0，由顶部的 .prepare_c_env 运行时动态精雕细琢！
     */
    .word 0x0067                        // Limit 15:0 (104 - 1 = 0x67)
    .word 0x0000                        // Base  15:0  [运行时补丁区]
    .byte 0x00                          // Base  23:16 [运行时补丁区]
    .byte 0x89                          // Type = 0x9 (Available 64-bit TSS), DPL=0, P=1
    .byte 0x00                          // Limit 19:16 = 0, Flags 全部为 0
    .byte 0x00                          // Base  31:24 [运行时补丁区]
    .long 0x00000000                    // Base  63:32 [运行时补丁区]
    .long 0x00000000                    // Reserved (TSS 高8字节的后32位强制保留为0)

tmp_gdt_ptr:
    .word tmp_gdt_ptr - tmp_gdt - 1     // 汇编器自动计算出包含 TSS 后的真实 GDT Size！
    .quad tmp_gdt                       // 运行时会被动态修补为真实物理基址


    .globl tmp_boot_info
    tmp_boot_info:          .quad   0   // 存放解析好的 Bootloader 信息的指针

    // 全局系统状态标记
    .globl tmp_paging_level
    tmp_paging_level: .byte   0         // 向 C 语言汇报系统是否成功启用了 5 级分页 (LA57)


/************************* 正式内核代码区 (后期扩展用) ************************/
.section    .text, "ax"

    // 长模式下极速系统调用入口 (SYSCALL / SYSRET)
.globl  long_mode_syscall_entry
long_mode_syscall_entry:
    // TODO: 硬件已自动将用户态 RIP 存入 RCX，RFLAGS 存入 R11
    // TODO: 手动切换用户态 RSP 到内核态 RSP
    // TODO: 保存所有通用寄存器到现场结构体 (pt_regs)
    // TODO: 调用 C 语言 syscall_handler
    // TODO: 恢复寄存器
    sysretq                             // 神奇的 sysret，根据 STAR 寄存器自动恢复状态并返回用户态