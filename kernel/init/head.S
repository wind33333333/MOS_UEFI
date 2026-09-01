.equ DIRECT_MAP_VA_START, 0xFFFF800000000000
.equ KERNEL_VA_START, 0xFFFFFFFF80000000


/*************************** 内核初始化代码入口 *******************************/
.code64
.section    .init_text, "ax"
.globl _start
_start:
    cli
    // 保存 loader 通过 rdi 传递过来的 boot_info 指针到物理平坦地址的内存中
    movq     %rdi, boot_info(%rip)

    /* ==================================================================== */
    /* 阶段 1：构建基础的 4 级临时页表 (PML4)                                 */
    /* 无论后续是否切换 5 级，这个 4 级页表都是必须的基石。                       */
    /* ==================================================================== */

    // 1. 初始化 PML4[0] (平坦映射 0x0 - 0x8000000000, 512GB)
    leaq     tmp_pml4t(%rip), %rdi
    leaq     tmp_pdpt0(%rip), %rax
    orq      $0x3, %rax                 // 0x3 = PRESENT | RW
    movq     %rax, (%rdi)

    // 2. 初始化 PML4[256] (直接映射区 DIRECT_MAP_VA_START, 比如 0xFFFF80...)
    movq     $DIRECT_MAP_VA_START, %rsi
    shrq     $39, %rsi
    andq     $0x1ff, %rsi
    shlq     $3, %rsi
    movq     %rax, (%rdi, %rsi)

    // 3. 初始化 PML4[511] (内核代码区 KERNEL_VA_START, 0xFFFFFFFF80...)
    leaq     tmp_pdpt1(%rip), %rax
    orq      $0x3, %rax
    movq     $KERNEL_VA_START, %rsi
    shrq     $39, %rsi
    andq     $0x1ff, %rsi
    shlq     $3, %rsi
    movq     %rax, (%rdi, %rsi)

    // 4. 填充 PDPT0 (512 个 1GB 大页，覆盖 0~512GB 物理内存)
    leaq     tmp_pdpt0(%rip), %rdi
    movq     $0x183, %rax               // 0x183 = PRESENT | RW | HUGE_PAGE (PS)
    movq     $512, %rcx
.init_tmp_pdptt0:
    stosq
    addq     $0x40000000, %rax          // 每次递增 1GB
    loopq    .init_tmp_pdptt0

    // 5. 填充 PDPT1 (专为内核映射 2GB 空间)
    leaq     tmp_pdpt1(%rip), %rdi
    movq     $KERNEL_VA_START, %rsi
    shrq     $30, %rsi
    andq     $0x1ff, %rsi
    shlq     $3, %rsi

    leaq     _start(%rip), %rax         // 提取内核实际加载的物理地址
    movq     $0xFFFFFFFFC0000000, %rbx
    andq     %rbx, %rax                 // 向下对齐到 1GB 边界
    orq      $0x183, %rax               // 挂载 1GB 大页标志
    movq     %rax, (%rdi, %rsi)         // PDPT1[510]
    addq     $0x40000000, %rax
    movq     %rax, 8(%rdi, %rsi)        // PDPT1[511]


    /* ==================================================================== */
    /* 阶段 2：LA57 (5级分页) 智能探测与路由                                  */
    /* ==================================================================== */

    // 【探测 1】：当前 UEFI 是否已经帮我们开启了 5 级分页？
    movq     %cr4, %rax
    btq      $12, %rax                  // 测试 CR4.LA57 (Bit 12)
    jc       .already_in_la57           // 如果已开启，直接去挂载 PML5，跳过蹦床

    // 【探测 2】：UEFI 没开，那物理 CPU 是否支持 5 级分页？
    movl     $7, %eax
    xorl     %ecx, %ecx
    cpuid
    btl      $16, %ecx                  // 测试 ECX 第 16 位
    jc       .need_trampoline_la57      // 支持！走蹦床强行开启

    // 【后备】：硬件不支持 LA57，老老实实用 4 级分页
    jmp      .la48_only


    /* ==================================================================== */
    /* 阶段 3：分支处理逻辑                                                  */
    /* ==================================================================== */

.already_in_la57:
    movb     $1, la57_enabled(%rip)
    call     .build_pml5_bridge         // 构造折叠桥接的 PML5
    leaq     tmp_pml5t(%rip), %rax
    movq     %rax, %cr3                 // 直接刷新 CR3，无需切换 32 位！
    jmp      .prepare_c_env

.la48_only:
    movb     $0, la57_enabled(%rip)
    leaq     tmp_pml4t(%rip), %rax
    movq     %rax, %cr3                 // 启用 4 级临时页表
    jmp      .prepare_c_env

.need_trampoline_la57:
    movb     $1, la57_enabled(%rip)
    call     .build_pml5_bridge         // 构造折叠桥接的 PML5

    // 动态修复 GDT 描述符的基址 (解决由于 PIE/PIC 带来的相对寻址偏移)
    leaq     tmp_gdt(%rip), %rax
    leaq     tmp_gdt_desc(%rip), %rcx
    movq     %rax, 2(%rcx)
    lgdt     (%rcx)

    // 准备跳转蹦床所需的平坦物理地址 (此时 RIP = 物理地址)
    leaq     tmp_pml5t(%rip), %rdi       // RDI = 新的 PML5 物理基址
    leaq     trampoline_64(%rip), %rsi   // RSI = 64 位跳回地址
    leaq     trampoline_32(%rip), %rax   // RAX = 32 位蹦床入口

    // 远跳转进入 32 位兼容模式 (利用 push + lretq 模拟长跳转)
    pushq    $0x10                       // 压入 32 位 Code Segment (0x10)
    pushq    %rax                        // 压入 EIP
    lretq


    /* ==================================================================== */
    /* 辅助子例程：构造神级折叠 PML5 桥接页表                                */
    /* ==================================================================== */
.build_pml5_bridge:
    leaq     tmp_pml5t(%rip), %rdi
    leaq     tmp_pml4t(%rip), %rax
    orq      $0x3, %rax                 // 0x3 = PRESENT | RW

    // 1. PML5[0] 指向 PML4
    // 维持 0x0 开头的平坦映射存活 (蹦床执行必需)
    movq     %rax, (%rdi)

    // 2. 【新增的补丁】：PML5[256] 指向 PML4
    // 适配 LA57 动态直接映射区 (0xFF00...)
    // CPU 寻址路径: PML5[256] -> PML4[0] -> tmp_pdpt0 -> 物理内存 0~512G
    movq     %rax, 2048(%rdi)

    // 3. PML5[511] 指向 PML4
    // 适配 LA48 静态高半核映射 (0xFFFF...)
    // 保证 KERNEL_VA_START 和 4级的 DIRECT_MAP_VA_START 继续生效
    movq     %rax, 4088(%rdi)
    ret


/* ==================================================================== */
/* 阶段 4：极其惊险的 32 位空中换擎蹦床区 (Trampoline)                       */
/* ==================================================================== */
.code32
trampoline_32:
    // 1. 加载 32 位兼容模式数据段
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss

    // 2. 关闭分页 (CR0.PG = 0) - 进入纯物理寻址！
    movl     %cr0, %eax
    btrl     $31, %eax
    movl     %eax, %cr0

    // 3. 拨动 5 级分页硬件开关 (CR4.LA57 = 1)
    movl     %cr4, %eax
    btsl     $12, %eax
    movl     %eax, %cr4

    // 4. 将 PML5 物理基址注入 CR3 (地址由刚才的 %edi 传入)
    movl     %edi, %cr3

    // 5. 重新开启分页 (CR0.PG = 1) - 此时宇宙已经扩张到 64 PB！
    movl     %cr0, %eax
    btsl     $31, %eax
    movl     %eax, %cr0

    // 6. 远跳转，浴火重生回归 64 位模式
    pushl    $0x08                      // 64 位 Code Segment (0x08)
    pushl    %esi                       // 存放在 RSI 里的 64 位返回地址
    lret

.code64
trampoline_64:
    // 恢复 64 位数据段
    movw     $0x18, %ax
    movw     %ax, %ds
    movw     %ax, %es
    movw     %ax, %ss


/* ==================================================================== */
/* 阶段 5：环境就绪，飞跃至高半核 C 语言内核主入口                           */
/* ==================================================================== */
.prepare_c_env:
    // 计算并设置当前核心的高半核虚拟栈顶指针
    leaq     _end_stack(%rip), %rsp
    movabs   $KERNEL_VA_START, %rax
    orq      %rax, %rsp

    // 计算内核 C 语言入口的高半核虚拟地址，并实施最终跃迁
    leaq     init_kernel(%rip), %rax
    movabs   $KERNEL_VA_START, %rbx
    orq      %rbx, %rax
    jmp      *%rax                      // 永远跳入 0xFFFFFFFF80000000 空间！


/********************** 内核初始化数据分配区 **************************/
.section    .init_data, "wa"

    // 页表必须严格 4KB 对齐
    .balign 4096

    .globl  tmp_pml5t
    tmp_pml5t:  .fill   0x1000, 1, 0    // 预留 PML5

    .globl  tmp_pml4t
    tmp_pml4t:  .fill   0x1000, 1, 0    // 预留 PML4

    tmp_pdpt0:  .fill   0x1000, 1, 0    // 用于平坦映射和直接映射区
    tmp_pdpt1:  .fill   0x1000, 1, 0    // 专用于 KERNEL 代码映射

    // 全局系统状态标记
    .globl la57_enabled
    la57_enabled: .byte   0             // 记录系统是否成功启用了 5 级分页

    // 蹦床专用的临时全局描述符表 (GDT)
    .balign 16
tmp_gdt:
    .quad 0x0000000000000000            // 0x00: Null
    .quad 0x00AF9A000000FFFF            // 0x08: 64-bit Code (Ring 0)
    .quad 0x00CF9A000000FFFF            // 0x10: 32-bit Code (Compat, Ring 0)
    .quad 0x00CF92000000FFFF            // 0x18: 32/64-bit Data (Ring 0)
tmp_gdt_desc:
    .word tmp_gdt_desc - tmp_gdt - 1
    .quad tmp_gdt                       // 运行时会被动态修补为物理地址

    .globl kernel_stack_top
    kernel_stack_top:   .quad   0

    .globl boot_info
    boot_info:          .quad   0


/************************* 正式内核代码区 ****************************/
.section    .text, "ax"

    // 长模式系统调用入口 (SYSCALL)
.globl  long_mode_syscall_entry
long_mode_syscall_entry:
    // TODO: 手动保存用户态 rip(rcx), rsp, rflags(r11), 及其他通用寄存器。
    // TODO: 执行系统调用逻辑
    // TODO: 恢复寄存器
    sysretq