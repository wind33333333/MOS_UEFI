/* ==================================================================== */
/* 极简全域复用架构：x86-64 早期启动汇编 (Bootstrapping)                  */
/* 特性：运行时动态计算所有页表索引，为未来 Virtual KASLR 铺平道路          */
/* ==================================================================== */
.equ KERNEL_VA_START,        0xFFFFFFFF80000000    // 高半核内核代码/数据区起始虚拟地址
.equ DIRECT_MAP_VA_START_4L, 0xFFFF800000000000    // 4 级分页直接映射区起始地址
.equ DIRECT_MAP_VA_START_5L, 0xFF00000000000000    // 5 级分页直接映射区起始地址
.equ PARITY_MAP_VA_START,    0x0                   // 对等映射起始地址

/********************** 内核初始化数据静态分配区 **************************/
.section    .init_data, "wa"

    // 页表必须严格 4KB 物理对齐
    .balign 4096

    // 中断专属安全栈 (Interrupt Stack Table)
    .fill 0x1000, 1, 0
    tmp_df_stack_top:       // 给 #DF (双重异常) 用 (IST1)

    .fill 0x1000, 1, 0
    tmp_nmi_stack_top:      // 给 #NMI (不可屏蔽中断) 用 (IST2)

    .fill 0x1000, 1, 0
    tmp_mc_stack_top:       // 给 #MC (机器检查异常) 用 (IST3)

    .fill 0x1000, 1, 0
    tmp_db_stack_top:       // 给 #DB (调试异常) 用 (IST4)

    .fill 0x1000, 1, 0
    tmp_rsp0_stack_top:     // TSS Ring 0 默认栈

    // BSP 引导处理器的临时安全栈 (16KB)
    .fill 0x4000, 1, 0
    tmp_bsp_stack_top:

    /* ================== 🌟 极简页表体系 🌟 ================== */
    /* 砍掉了一半的临时页表，通过精妙的指针复用覆盖所有地址空间 */
    .globl  tmp_pml5t
    tmp_pml5t:       .fill 0x1000, 1, 0    // 顶级 PML5 (LA57 专用)

    .globl  tmp_pml4t
    tmp_pml4t:       .fill 0x1000, 1, 0    // 顶级 PML4 (4 级主根)

    // 核心复用：同时肩负“1:1恒等映射”与“上帝视角直接映射”
    tmp_pdpt_direct: .fill 0x1000, 1, 0    // 1GB 大页表 (精准覆盖 0~512GB 物理内存)

    tmp_pdpt_kernel: .fill 0x1000, 1, 0    // 高半区 KERNEL 专属 PDPT
    tmp_pd_kernel:   .fill 0x1000, 1, 0    // 高半区 KERNEL 专属 PD (2MB 页)
    /* ======================================================== */

    // 临时 TSS (Task State Segment)
    .balign 64                          // Cache Line 对齐，防止 SMP 伪共享
tmp_tss:
    .long 0
    .quad tmp_rsp0_stack_top
    .quad 0
    .quad 0
    .quad 0
    .quad tmp_df_stack_top
    .quad tmp_nmi_stack_top
    .quad tmp_mc_stack_top
    .quad tmp_db_stack_top
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .word 0
    .word 0x0068                        // iomap_base 拦截 IO

    // 早期全局描述符表 (GDT)
    .balign 16
tmp_gdt:
    .quad 0x0000000000000000            // 0x00: Null
    .quad 0x00AF9A000000FFFF            // 0x08: 64-bit Kernel Code (Ring 0)
    .quad 0x00CF9A000000FFFF            // 0x10: 32-bit Compat Code (蹦床跳跃专用)
    .quad 0x00CF92000000FFFF            // 0x18: 32/64-bit Data (Ring 0)

    // 0x20: 临时 64 位 TSS 描述符 (占 16 字节 = 2 个槽位)
    .word 0x0067, 0x0000
    .byte 0x00, 0x89, 0x00, 0x00
    .long 0x00000000, 0x00000000

    .globl tmp_boot_info
    tmp_boot_info:    .quad 0           // 存放 Bootloader 信息物理指针

    .globl tmp_paging_level
    tmp_paging_level: .byte 0           // 记录当前处于 4 级还是 5 级分页 (供 C 语言读取)


/*************************** 内核初始化代码入口 *******************************/
.code64
.section    .init_text, "ax"
.globl _start
_start:
    jmp .
    cli

    // 切换到内核堆栈
    leaq   tmp_bsp_stack_top(%rip),%rsp

    movq     %rdi, %r15                 // 保护 BootInfo 物理指针到 %r15，防止被破坏

    /* ==================================================================== */
    /* 阶段 0：接管宇宙 - 加载物理 GDT 并刷新环境                             */
    /* ==================================================================== */
    leaq     tmp_gdt(%rip), %rax        // 获取 GDT 物理基址
    pushq    %rax                       // 压入 8 字节的 Base 地址到栈上
    pushw    $0x2F                      // 压入 2 字节的 Limit 到栈上 GDT Size = 48 字节, Limit = 47 (0x2F)
    lgdt     (%rsp)                     // 安全加载 GDT精准指向栈顶的 10 字节 GDTR 结构！
    addq     $10, %rsp                  // 恢复栈顶指针，清理我们刚才借用的 10 字节

    // 清洗数据段寄存器
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss
    xorw     %ax, %ax
    movw     %ax, %fs
    movw     %ax, %gs

    // 空中翻滚刷新 CS 寄存器
    leaq     .reload_cs(%rip), %rax
    pushq    $0x08
    pushq    %rax
    lretq

.reload_cs:
    // 将 BootInfo 物理指针存入全局变量
    leaq     tmp_boot_info(%rip), %rax
    movq     %r15, (%rax)

    // 启用 x2apic (如果支持)
    movl     $0x1B, %ecx
    rdmsr
    orl      $0xC00, %eax
    wrmsr

    /* ==================================================================== */
    /* 阶段 1：构建极简复用页表 (运行时动态计算偏移槽位)                       */
    /* ==================================================================== */

    // 提取当前内核加载的物理基址到 R12 (2MB 向下对齐)
    leaq     _start(%rip), %r12
    movq     $0xFFFFFFFFFFE00000, %rbx
    andq     %rbx, %r12

    // --------------------------------------------------------------------
    // 定律 2 + 1 融合：构建全域巨无霸 PDPT (覆盖 0~512GB)
    // --------------------------------------------------------------------
    leaq     tmp_pdpt_direct(%rip), %rdi
    movq     $0x183, %rax               // 物理 0 开始，1GB大页 (PRESENT | RW | HUGE)
    movl     $512, %ecx                 // 512 个 1GB 页 = 512GB
.fill_direct_pdpt:
    stosq
    addq     $0x40000000, %rax          // 物理地址步长 1GB
    loop     .fill_direct_pdpt

    // 🌟 复用魔法开始：双挂载 (动态索引计算) 🌟
    leaq     tmp_pml4t(%rip), %rdi
    leaq     tmp_pdpt_direct(%rip), %rbx
    orq      $0x3, %rbx

    // 挂载 A：恒等映射 (动态计算 PARITY_MAP_VA_START 在 PML4 的索引)
    movabs   $PARITY_MAP_VA_START, %rax
    shrq     $39, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 0

    // 挂载 B：4 级分页直接映射区 (动态计算 DIRECT_MAP_VA_START_4L 在 PML4 的索引)
    movabs   $DIRECT_MAP_VA_START_4L, %rax
    shrq     $39, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 256

    // --------------------------------------------------------------------
    // 定律 3：构建 KERNEL 高半区专属映射 (动态锚定 KERNEL_VA_START)
    // --------------------------------------------------------------------
    leaq     tmp_pml4t(%rip), %rdi
    leaq     tmp_pdpt_kernel(%rip), %rbx
    orq      $0x3, %rbx

    // 动态计算 KERNEL_VA_START 在 PML4 的索引
    movabs   $KERNEL_VA_START, %rax
    shrq     $39, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 511

    leaq     tmp_pdpt_kernel(%rip), %rdi
    leaq     tmp_pd_kernel(%rip), %rbx
    orq      $0x3, %rbx

    // 动态计算 KERNEL_VA_START 在 PDPT 的索引
    movabs   $KERNEL_VA_START, %rax
    shrq     $30, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 510

    // 精确填充内核专属 PD (2MB 大页)，起点强制从 R12(内核物理基址) 开始！
    leaq     tmp_pd_kernel(%rip), %rdi
    movq     %r12, %rax                 // 将 KASLR 随机基址融入虚拟映射
    orq      $0x83, %rax                // PRESENT | RW | HUGE
    movl     $512, %ecx                 // 映射 1GB 虚拟空间
.fill_kernel_pd:
    stosq
    addq     $0x200000, %rax            // 物理地址步长 2MB
    loop     .fill_kernel_pd


    /* ==================================================================== */
    /* 阶段 2：LA57 (5级分页) 智能探测与无缝路由                              */
    /* ==================================================================== */
    movq     %cr4, %rax
    btq      $12, %rax                  // 探测 CR4.LA57 是否被 UEFI 开启
    jc       .already_in_la57

    movl     $7, %eax
    xorl     %ecx, %ecx
    cpuid
    btl      $16, %ecx                  // 探测 CPU 是否支持 LA57 指令集
    jc       .need_trampoline_la57

    // 【降级分支】仅支持 4 级分页
    leaq     tmp_paging_level(%rip), %rax
    movb     $4, (%rax)
    leaq     tmp_pml4t(%rip), %rax
    movq     %rax, %cr3                 // 加载 4 级根页表
    jmp      .ready_to_jump

.already_in_la57:
    leaq     tmp_paging_level(%rip), %rax
    movb     $5, (%rax)
    call     .build_pml5_bridge
    leaq     tmp_pml5t(%rip), %rax
    movq     %rax, %cr3                 // 刷新为 5 级桥接页表
    jmp      .ready_to_jump

.need_trampoline_la57:
    leaq     tmp_paging_level(%rip), %rax
    movb     $5, (%rax)
    call     .build_pml5_bridge

    // 准备 32 位降级跳板 (因为切换 CR4.LA57 必须暂时关闭分页)
    leaq     tmp_pml5t(%rip), %rdi
    leaq     trampoline_64(%rip), %rsi
    leaq     trampoline_32(%rip), %rax
    pushq    $0x10
    pushq    %rax
    lretq


/* ==================================================================== */
/* 子例程：构造 LA57 路由桥接，完美重用 4 级页表树 (纯动态索引计算)         */
/* ==================================================================== */
.build_pml5_bridge:
    leaq     tmp_pml5t(%rip), %rdi
    leaq     tmp_pml4t(%rip), %rbx
    orq      $0x3, %rbx

    // 路由 1：1:1 恒等映射区 (动态计算 PARITY_MAP_VA_START 在 PML5 的索引)
    movabs   $PARITY_MAP_VA_START, %rax
    shrq     $48, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 0

    // 路由 2：5级直接映射区 (动态计算 DIRECT_MAP_VA_START_5L 在 PML5 的索引)
    // 寻址链：PML5[动态槽位] -> PML4 -> PML4[0] -> 巨无霸 PDPT -> 完美映射物理内存！
    movabs   $DIRECT_MAP_VA_START_5L, %rax
    shrq     $48, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 256

    // 路由 3：内核高半区 (动态计算 KERNEL_VA_START 在 PML5 的索引)
    movabs   $KERNEL_VA_START, %rax
    shrq     $48, %rax
    andq     $0x1FF, %rax
    movq     %rbx, (%rdi, %rax, 8)      // 动态槽位：通常是 511

    ret


/* ==================================================================== */
/* 阶段 3：32 位蹦床 (开启 5 级硬件开关)                                  */
/* ==================================================================== */
.code32
trampoline_32:
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss

    movl     %cr0, %eax
    btrl     $31, %eax                  // 拔掉网线：关闭分页
    movl     %eax, %cr0

    movl     %cr4, %eax
    btsl     $12, %eax                  // 拨动开关：开启 CR4.LA57
    movl     %eax, %cr4

    movl     %edi, %cr3                 // 加载 tmp_pml5t 物理基址

    movl     %cr0, %eax
    btsl     $31, %eax                  // 插上网线：重新开启分页
    movl     %eax, %cr0

    pushl    $0x08
    pushl    %esi
    lret

.code64
trampoline_64:
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss


/* ==================================================================== */
/* 阶段 4：极速跃迁！计算绝对高半区目标地址并跳转 (动态锚定)                  */
/* ==================================================================== */
.ready_to_jump:
    // 计算 .higher_half 在高半区虚拟空间的绝对地址：
    // VTarget = (.higher_half物理 - 内核物理基址) + 动态目标虚拟基址
    leaq     .higher_half(%rip), %rax   // 当前物理指令地址
    subq     %r12, %rax                 // 减去内核 2MB 对齐物理基址 (算出偏移量)
    movabs   $KERNEL_VA_START, %rbx
    addq     %rbx, %rax                 // 加上极高位虚拟基址

    // 发动空间跃迁！
    jmp      *%rax


/* ==================================================================== */
/* 阶段 5：过河拆桥！重建高级 C 语言环境                                  */
/* ==================================================================== */
.higher_half:
    // ！！！成功存活：现在的 %rip 已经是 0xFFFFFFFF80xxxxxx ！！！

    // --------------------------------------------------------------------
    // 定律 1 执行：拆除 1:1 恒等映射，防御空指针 (动态计算目标索引并拆除)
    // --------------------------------------------------------------------
    cmpb     $5, tmp_paging_level(%rip)
    je       .clear_pml5

    // 4 级分页：斩断 tmp_pml4t 上的 PARITY_MAP_VA_START 映射
    movabs   $PARITY_MAP_VA_START, %rax
    shrq     $39, %rax
    andq     $0x1FF, %rax
    leaq     tmp_pml4t(%rip), %rdi      // %rip 已是虚拟地址，取出的指针合法！
    movq     $0, (%rdi, %rax, 8)
    jmp      .flush_tlb

.clear_pml5:
    // 5 级分页：斩断 tmp_pml5t 上的 PARITY_MAP_VA_START 映射
    movabs   $PARITY_MAP_VA_START, %rax
    shrq     $48, %rax
    andq     $0x1FF, %rax
    leaq     tmp_pml5t(%rip), %rdi
    movq     $0, (%rdi, %rax, 8)

.flush_tlb:
    // 强刷 TLB，旧世界的 0 地址桥梁彻底粉碎
    movq     %cr3, %rax
    movq     %rax, %cr3


    // --------------------------------------------------------------------
    // 动态重修 GDT 与 TSS (此时全是在高半核虚拟地址空间操作)
    // --------------------------------------------------------------------
    leaq     tmp_gdt(%rip), %rax        // 获取 GDT 物理基址
    pushq    %rax                       // 压入 8 字节的 Base 地址到栈上
    pushw    $0x2F                      // 压入 2 字节的 Limit 到栈上 GDT Size = 48 字节, Limit = 47 (0x2F)
    lgdt     (%rsp)                     // 安全加载 GDT精准指向栈顶的 10 字节 GDTR 结构！
    addq     $10, %rsp                  // 恢复栈顶指针，清理我们刚才借用的 10 字节

    leaq     tmp_tss(%rip), %rax
    leaq     tmp_gdt(%rip), %rdi
    movw     %ax, 0x22(%rdi)
    movq     %rax, %rcx
    shrq     $16, %rcx
    movb     %cl, 0x24(%rdi)
    movq     %rax, %rcx
    shrq     $24, %rcx
    movb     %cl, 0x27(%rdi)
    movq     %rax, %rcx
    shrq     $32, %rcx
    movl     %ecx, 0x28(%rdi)

    movw     $0x20, %cx
    ltr      %cx


    // --------------------------------------------------------------------
    // 零化 BSS 数据段 (安全规范)
    // --------------------------------------------------------------------
    movabs   $_start_bss, %rdi
    movabs   $_end_bss, %rcx
    subq     %rdi, %rcx
    shrq     $3, %rcx                   // 按 8 字节(qword)清零
    xorl     %eax, %eax
    rep stosq


    // --------------------------------------------------------------------
    // 切换至绝对安全的高半核栈，并挂载参数进入 C 语言
    // --------------------------------------------------------------------
    leaq     tmp_bsp_stack_top(%rip), %rsp

    // R15 一直保存着 BootInfo 的物理基址，现在传给 RDI 作为 kernel_init 第一个参数
    movq     %r15, %rdi

    // 极速呼叫 C 语言大门
    leaq     kernel_init(%rip), %rax
    call     *%rax

    cli
.hlt_loop:
    hlt
    jmp .hlt_loop