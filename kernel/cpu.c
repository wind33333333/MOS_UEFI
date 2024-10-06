#include "cpu.h"

void cpuInit(unsigned int *cpuId,unsigned char *bspFlags) {

    __asm__ __volatile__(
            //IA32_APIC_BASE (MSR 0x1B)
            /*
X2APIC（X2APIC 模式启用位，bit 10）：
作用：如果该位被设置为 1，处理器启用 X2APIC 模式。X2APIC 是 APIC 的扩展版本，提供了更多的功能，例如更大的中断目标地址空间。
重要性：X2APIC 模式在大型多处理器系统中尤为重要，因为它允许更高效的中断处理，并支持更大的 CPU 核心数量。
EN（Enable，本地 APIC 启用位，bit 11）：
作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
重要性：如果未启用 APIC，处理器将无法使用高级中断控制机制。
BSP（Bootstrap Processor，系统启动处理器标志位，bit 9）：
作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
重要性：BSP 的作用是在多处理器系统启动时执行初始化任务，确保系统正常运行。
APIC Base Address（APIC 基地址，bit 12-31）：
作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
重要性：APIC 基地址用于访问本地 APIC 的寄存器，通常不需要修改这个值，除非系统有特殊的硬件需求。*/
            "movl       $0x1B,%%ecx  \n\t"
            "rdmsr                   \n\t"
            "or         $0xC00,%%eax \n\t"
            "wrmsr                   \n\t"
            "shr        $8,%%eax     \n\t"
            "and        $1,%%eax     \n\t"
            :"=a"(*bspFlags)::"%rcx","%rdx");

    __asm__ __volatile__(
            //region CR4
            /*    CR4 寄存器的结构（按位）
    CR4.PSE（第 4 位） - Page Size Extension（页大小扩展）
    位位置：bit 4
    描述：控制 4MB 的页大小（相对于标准的 4KB 页大小）。当该位设置为 1 时，启用 4MB 页大小。
    用途：大页面有助于减少分页表的管理开销，并提高性能。
    CR4.PAE（第 5 位） - Physical Address Extension（物理地址扩展）
    位位置：bit 5
    描述：启用 36 位的物理地址扩展（PAE），支持物理内存超过 4GB 的地址空间。
    用途：对于支持超过 4GB 内存的 32 位系统，PAE 是必需的。
    CR4.MCE（第 6 位） - Machine Check Exception（机器检查异常）
    位位置：bit 6
    描述：启用机器检查异常。如果硬件检测到机器检查错误，则会触发该异常。
    用途：用于检测 CPU 或内存的硬件错误。
    CR4.PGE（第 7 位） - Page Global Enable（全局页启用）
    位位置：bit 7
    描述：允许全局页功能。如果启用全局页，则不会在上下文切换时刷新这些页的 TLB（转换后备缓冲）。
    用途：提高性能，特别是对于内核和共享内存。
    CR4.PCE（第 8 位） - Performance-Monitoring Counter Enable（性能监控计数器启用）
    位位置：bit 8
    描述：启用在用户模式下访问性能监控计数器的权限。
    用途：允许用户模式下访问性能计数器，用于性能分析。
    CR4.OSFXSR（第 9 位） - Operating System Support for FXSAVE/FXRSTOR
    位位置：bit 9
    描述：启用操作系统对 FXSAVE 和 FXRSTOR 指令的支持，这些指令用于保存和恢复浮点及 SIMD 状态。
    用途：用于支持 SSE（Streaming SIMD Extensions）指令集。
    CR4.OSXMMEXCPT（第 10 位） - Operating System Support for Unmasked SIMD Floating-Point Exceptions
    位位置：bit 10
    描述：启用对未屏蔽的 SIMD 浮点异常的操作系统支持。
    用途：在使用 SSE 指令集时启用更精细的异常处理。
    CR4.UMIP（第 11 位） - User-Mode Instruction Prevention
    位位置：bit 11
    描述：防止某些系统指令（如 SGDT 和 SIDT）在用户模式下执行。
    用途：增强安全性，防止用户模式程序获取系统敏感信息。
    CR4.LA57（第 12 位） - Level 5 Paging Enable（5 级分页启用）
    位位置：bit 12
    描述：启用 5 级分页（用于支持更大的虚拟地址空间）。
    用途：扩展虚拟地址空间到 57 位地址范围，特别是在大规模内存服务器上使用。
    CR4.VMXE（第 13 位） - Virtual Machine Extensions Enable（虚拟机扩展启用）
    位位置：bit 13
    描述：启用虚拟化技术的支持。如果设置，处理器将能够执行虚拟化相关指令（如 VMX 指令集）。
    用途：支持硬件虚拟化技术。
    CR4.SMXE（第 14 位） - Safer Mode Extensions Enable（安全模式扩展启用）
    位位置：bit 14
    描述：启用安全模式扩展的支持，用于执行安全相关的指令。
    用途：增强平台安全性，主要用于 Intel 的 TXT 技术。
    CR4.FSGSBASE（第 16 位） - Enable RDFSBASE, WRFSBASE, RDGSBASE, WRGSBASE Instructions
    位位置：bit 16
    描述：启用对 RDFSBASE、WRFSBASE、RDGSBASE 和 WRGSBASE 指令的支持，允许直接从 FS 和 GS 段寄存器读取和写入基地址。
    用途：提高用户模式和内核模式之间的切换效率。
    CR4.PCIDE（第 17 位） - Process-Context Identifiers Enable（进程上下文标识符启用）
    位位置：bit 17
    描述：启用进程上下文标识符（PCID），允许在不同的进程之间缓存 TLB 条目，从而避免 TLB 的频繁刷新。
    用途：提高上下文切换时的性能。
    CR4.OSXSAVE（第 18 位） - XSAVE and Processor Extended States Enable
    位位置：bit 18
    描述：启用对 XSAVE/XRSTOR 指令的支持，这些指令用于保存和恢复扩展处理器状态（如 AVX 寄存器）。
    用途：支持 AVX、AVX2 和其他扩展状态的保存与恢复。
    CR4.SMEP（第 20 位） - Supervisor Mode Execution Prevention（监控模式执行预防）
    位位置：bit 20
    描述：启用内核模式下禁止执行用户空间代码。如果启用，内核模式下试图执行用户空间代码会触发错误。
    用途：增强操作系统安全性，防止内核模式的代码误执行用户空间的恶意代码。
    CR4.SMAP（第 21 位） - Supervisor Mode Access Prevention（监控模式访问预防）
    位位置：bit 21
    描述：启用内核模式下禁止访问用户空间内存。内核模式试图访问用户空间内存会触发错误，除非显式禁用访问。
    用途：进一步增强内核的安全性，防止潜在的漏洞利用。
    CR4.PKE（第 22 位） - Protection-Key Enable（保护密钥启用）
    位位置：bit 22
    描述：启用内存保护密钥功能。该功能允许程序在不修改页表的情况下控制内存的访问权限。
    用途：提供更灵活的内存保护机制，用于区分不同的内存访问权限。*/
            "mov        %%cr4,%%rax        \n\t"
            "or         $0x50E80,%%rax     \n\t"
            "mov        %%rax,%%cr4        \n\t"
            :::"%rax");

    __asm__ __volatile__(
            //region XCR0
            /*
    x87 状态（bit 0）：
    描述：控制 x87 浮点状态的保存与恢复。x87 是早期的浮点运算单元，负责处理浮点数运算。
    用途：如果该位被设置为 1，XSAVE 和 XRSTOR 指令将保存和恢复 x87 浮点状态。
    SSE 状态（bit 1）：
    描述：控制 SSE 状态（XMM 寄存器）的保存与恢复。SSE 是现代 SIMD 指令集，用于处理浮点和整数数据。
    用途：设置该位后，XSAVE 和 XRSTOR 将保存和恢复 SSE 的 XMM 寄存器状态。
    AVX 状态（bit 2）：
    描述：控制 AVX 状态（YMM 寄存器）的保存与恢复。AVX 是比 SSE 更强大的 SIMD 指令集，能够处理 256 位数据。
    用途：启用该位后，处理器将保存和恢复 AVX 的 YMM 寄存器状态。
    MPX 状态（bit 3-4）：
    描述：控制 MPX（内存保护扩展）寄存器的保存与恢复。MPX 用于检测和防止内存边界越界问题。
    用途：启用该位后，XSAVE 和 XRSTOR 将保存 MPX 寄存器的状态。
    AVX-512 状态（bit 5）：
    描述：控制 AVX-512 状态（ZMM 寄存器）的保存与恢复。AVX-512 是 AVX 的扩展，使用 512 位寄存器，提供了更大的数据并行性。
    用途：启用该位后，XSAVE 和 XRSTOR 将保存和恢复 AVX-512 的寄存器状态。
    BNDCSR 状态（bit 6）：
    描述：控制 MPX 的 BNDCSR 状态的保存与恢复。BNDCSR 用于管理 MPX 的边界检查寄存器。
    用途：启用该位后，处理器会保存和恢复 MPX 边界检查的控制状态。
    PKRU 状态（bit 8）：
    描述：控制 PKRU 状态的保存与恢复。PKRU（Protection Keys for Userspace）是内存保护的一种机制。
    用途：启用该位后，处理器会保存和恢复与 PKRU 相关的状态。*/
            "mov        $0x7,%%r8d        \n\t"  //avx256
            "mov        $0,%%ecx          \n\t"
            "mov        $0x7,%%eax        \n\t"
            "cpuid                        \n\t"
            "test       $0x10000,%%ebx    \n\t"  //bit16置位表示支持avx512指令集
            "jz        1f                 \n\t"
            "mov        $0x27,%%r8d       \n\t" //avx512
            "1:mov        $0,%%ecx        \n\t"
            "xgetbv                       \n\t"
            "or         %%r8d,%%eax       \n\t"
            "xsetbv                       \n\t"
            :::"%rax","%rbx","%rcx","%rdx","%r8");

    __asm__ __volatile__(
            //region EFER 寄存器（MSR 0xC0000080)
            /*
    SCE（bit 0） - System Call Enable
    描述：启用 SYSCALL 和 SYSRET 指令。在启用后，用户模式可以通过 SYSCALL 指令快速切换到内核模式，而不需要通过中断（如 int 0x80）来进行系统调用。
    用途：SYSCALL/SYSRET 是现代 x86-64 操作系统常用的系统调用机制，能显著减少系统调用的开销，尤其是在 64 位模式下。
    LME（bit 8） - Long Mode Enable
    描述：启用 64 位长模式。当该位被设置为 1 时，处理器允许进入 64 位模式。在启用长模式时，CR0.PG（分页启用位）和 CR4.PAE（物理地址扩展启用位）也必须设置。
    用途：该位允许操作系统进入 64 位模式，支持更大的虚拟地址空间和更复杂的分页机制。
    NXE（bit 11） - No-Execute Enable
    描述：启用 NX（No-eXecute） 位功能。NX 位用于控制某些内存页面的执行权限。如果该位被设置为 1，操作系统可以使用分页机制将特定的内存页面标记为不可执行，以防止执行非代码数据，如栈或堆内存，防止某些缓冲区溢出攻击。
    用途：增强系统安全性，通过将某些内存区域标记为不可执行，防止恶意代码利用缓冲区溢出漏洞执行任意代码。*/
            "mov        $0xC0000080,%%ecx  \n\t"
            "rdmsr                         \n\t"
            "or         $0x801,%%eax       \n\t"
            "wrmsr                         \n\t"
            :::"%rax","%rcx","%rdx");

    __asm__ __volatile__(
            //region CR0
            /*
                PE（Protection Enable，bit 0）
                描述：该位控制处理器是否运行在保护模式下。当设置为 1 时，处理器进入保护模式；当为 0 时，处理器运行在实模式。
                用途：保护模式是现代操作系统运行的基本模式，提供了内存保护、多任务处理和虚拟内存管理的功能。
                MP（Monitor Coprocessor，bit 1）
                描述：该位用于协处理器的管理。如果设置为 1，并且 TS 位也被设置，当执行 WAIT/FWAIT 指令时会触发处理器异常。
                用途：用于处理浮点协处理器的上下文切换。
                EM（Emulation，bit 2）
                描述：如果该位设置为 1，处理器会仿真协处理器操作，而不是实际执行协处理器指令。
                用途：用于没有协处理器的系统，或者禁用协处理器的场景。
                TS（Task Switched，bit 3）
                描述：该位用于指示是否发生了任务切换。它与协处理器指令相关联，用于标记任务切换。
                用途：在任务切换时，用于在浮点协处理器和主处理器之间切换上下文。
                ET（Extension Type，bit 4）
                描述：该位用于区分处理器的扩展类型。在现代处理器中，这个位通常为 1。
                用途：历史遗留位，在现代处理器中固定为 1。
                NE（Numeric Error，bit 5）
                描述：控制浮点异常的处理方式。如果设置为 1，处理器使用现代的标准 x87 浮点错误处理方式；如果为 0，则使用旧式错误处理机制。
                用途：现代系统中通常将该位设置为 1 以启用标准浮点异常处理。
                WP（Write Protect，bit 16）
                描述：该位控制分页系统中的写保护。如果设置为 1，即使是运行在特权级别 0（内核模式）的代码也无法写入只读的分页内存。
                用途：用于保护内核空间中的只读数据，防止特权级代码意外或恶意修改内存。
                AM（Alignment Mask，bit 18）
                描述：该位用于启用对齐检查。如果该位和 EFLAGS 寄存器中的 AC 位都被设置，当内存访问没有按正确的边界对齐时会触发对齐检查异常。
                用途：用于捕获未对齐的内存访问，通常在调试和某些高性能应用中启用。
                NW（Not Write-through，bit 29）
                描述：控制写通缓存策略。如果设置为 1，禁用写通缓存，即写操作不会直接写入主存。
                用途：常用于调试或处理器特性设置。
                CD（Cache Disable，bit 30）
                描述：该位控制缓存是否启用。如果设置为 1，禁用处理器的缓存功能，所有内存访问都直接通过主存完成。
                用途：用于调试缓存问题，或在某些嵌入式系统中禁用缓存以简化内存管理。
                PG（Paging Enable，bit 31）
                描述：该位控制是否启用分页机制。如果设置为 1，启用分页机制，使虚拟地址转换为物理地址；如果为 0，禁用分页。
                用途：分页是现代操作系统实现虚拟内存和内存保护的基础。该位在现代操作系统中必须启用。*/
            "mov        %%cr0,%%rax        \n\t"
            "or         $0x10002,%%rax     \n\t"
            "mov        %%rax,%%cr0        \n\t"
            :::"%rax");

    // 获取当前CPU id号
    __asm__ __volatile__ (
            "movl       $0x802,%%ecx   \n\t"
            "rdmsr                     \n\t"
            :"=a"(*cpuId)::"%rcx", "%rdx");

    if (*bspFlags) {
        // 获取CPU厂商
        __asm__ __volatile__(
                "xor    %%eax, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%ebx, (%%rdi) \n\t"
                "mov    %%edx, 4(%%rdi)\n\t"
                "mov    %%ecx, 8(%%rdi) \n\t"
                "movb   $0, 12(%%rdi) \n\t"
                ::"D"(&cpu_info.manufacturer_name):"%rax", "%rbx", "%rcx", "%rdx");

        // 获取CPU核心数量
        __asm__ __volatile__(
                "mov        $1,%%eax    \n\t"
                "cpuid                  \n\t"
                "shr        $16,%%ebx   \n\t"         //右移16位得到cpu数量
                :"=b"(cpu_info.cores_num)::"%rax", "%rcx", "%rdx");

        // 获取CPU型号
        __asm__ __volatile__(
                "mov    $0x80000002, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, (%%rdi)   \n\t"
                "mov    %%ebx, 4(%%rdi)  \n\t"
                "mov    %%ecx, 8(%%rdi)  \n\t"
                "mov    %%edx, 12(%%rdi) \n\t"

                "mov    $0x80000003, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, 16(%%rdi)   \n\t"
                "mov    %%ebx, 20(%%rdi)  \n\t"
                "mov    %%ecx, 24(%%rdi)  \n\t"
                "mov    %%edx, 28(%%rdi) \n\t"

                "mov    $0x80000004, %%eax \n\t"
                "cpuid         \n\t"
                "mov    %%eax, 32(%%rdi)   \n\t"
                "mov    %%ebx, 36(%%rdi)  \n\t"
                "mov    %%ecx, 40(%%rdi)  \n\t"
                "mov    %%edx, 44(%%rdi) \n\t"

                "mov    $0, 48(%%rdi) \n\t"
                ::"D"(&cpu_info.model_name):"%rax", "%rbx", "%rcx", "%rdx");

        // 获取CPU频率
        __asm__ __volatile__(
                "mov    $0x16, %%eax \n\t"
                "cpuid         \n\t"
                "shl    $32,%%rdx  \n\t"
                "or     %%rdx,%%rax \n\t"
                :"=a"(cpu_info.fundamental_frequency), "=b"(cpu_info.maximum_frequency), "=c"(cpu_info.bus_frequency)::"%rdx");

        // 获取CPU TSC频率
        __asm__ __volatile__(
                "mov    $0x15,%%eax  \n\t"
                "cpuid               \n\t"
                "test   %%ecx,%%ecx  \n\t"
                "jz     .1           \n\t"            //如果ecx等于0则获取到的tsc频率无效
                "xchg   %%rax,%%rbx  \n\t"
                "mul    %%rcx        \n\t"
                "div    %%rbx        \n\t"
                ".1:                 \n\t"
                :"=a"(cpu_info.tsc_frequency)::"%rcx", "%rbx", "%rdx");

    }

    return;
}

