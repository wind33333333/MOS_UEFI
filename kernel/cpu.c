#include "cpu.h"
#include "idt.h"
#include "apic.h"
#include "gdt.h"
#include "tss.h"
#include "page.h"
#include "ap.h"
#include "acpi.h"
#include "syscall.h"
#include "printk.h"
#include "memory.h"

cpu_info_t cpu_info;

void init_cpu(void){
    UINT32 apic_id,cpu_id,tmp;
    CPUID(tmp,tmp,tmp,apic_id,0xB,0x1);        //获取apic_ia
    cpu_id = apicid_to_cpuid(apic_id);         //获取cpu_id
    init_cpu_amode();                          //初始化cpu开启高级功能
    get_cpu_info();                            //获取cpu信息
    init_page();                               //初始化内核页表
    init_gdt();                                //初始化GDT
    init_tss();                                //初始化TSS
    init_idt();                                //初始化IDT
    init_apic();                               //初始化apic
    init_syscall();                            //初始化系统调用

    UINT64 *user_progarm_address = alloc_pages(1);
    map_pages((UINT64)user_progarm_address,0x6000,1,PAGE_USER_RWX);
    user_progarm_address = alloc_pages(1);
    map_pages((UINT64)user_progarm_address,0x5000,1,PAGE_USER_RWX);
    memcpy(user_program,(void *)0x5000,4096);
    UINT64 rflags;
    __asm__ __volatile__(
            "movl   $0x10,%%eax    \n\t"
            "movl   %%eax,%%gs     \n\t"
            "movl   %%eax,%%fs     \n\t"
            "pushfq                     \n\t"
            "pop        %%rax           \n\t"
            "rdgsbase   %%rax           \n\t"
            "movq       $0x5000,%%rax   \n\t"
            "wrgsbase   %%rax           \n\t"
            "movq       %%gs:(0),%%rbx    \n\t"
            "rdgsbase   %%rbx           \n\t"
            "swapgs                     \n\t"
            "movq       $0x6000,%%rax   \n\t"
            "wrgsbase   %%rax           \n\t"
            "swapgs                     \n\t"
            "rdfsbase   %%rax           \n\t"
            "movq       $0x5000,%%rax   \n\t"
            "wrfsbase   %%rax           \n\t"
            "movq       %%fs:(0),%%rbx    \n\t"
            :"=a"(rflags)::"memory");

    __asm__ __volatile__(
            "movq   %1,%%r11    \n\t"
            "movq   %2,%%rsp    \n\t"
            "sysretq            \n\t"
            ::"c"(0x5000),"m"(rflags),"i"(0x7000):"memory");

    while(1);
    color_printk(GREEN, BLACK, "CPU Manufacturer: %s  Model: %s\n",cpu_info.manufacturer_name, cpu_info.model_name);
    color_printk(GREEN, BLACK, "CPU Cores: %d  FundamentalFrequency: %ldMhz  MaximumFrequency: %ldMhz  BusFrequency: %ldMhz  TSCFrequency: %ldhz\n",cpu_info.logical_processors_number,cpu_info.fundamental_frequency,cpu_info.maximum_frequency,cpu_info.bus_frequency,cpu_info.tsc_frequency);
    init_ap();                                 //初始化ap核
    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);
    return;
}

void user_program(void){
    __asm__ __volatile__(
            "syscall    \n\t"
            :::);
    while(1);
    return;
}

void print_h(void){
    color_printk(BLUE,BLACK,"hello word system call!");
    return;
}

__attribute__((section(".init_text"))) void get_cpu_info(void) {
    UINT32 eax,ebx,ecx,edx;
    // 获取CPU厂商
    CPUID(*(UINT32*)&cpu_info.manufacturer_name[8],*(UINT32*)&cpu_info.manufacturer_name[0],*(UINT32*)&cpu_info.manufacturer_name[8],*(UINT32*)&cpu_info.manufacturer_name[4],0,0);

    // 获取CPU型号
    CPUID(*(UINT32*)&cpu_info.model_name[0],*(UINT32*)&cpu_info.model_name[4],*(UINT32*)&cpu_info.model_name[8],*(UINT32*)&cpu_info.model_name[12],0x80000002UL,0);
    CPUID(*(UINT32*)&cpu_info.model_name[16],*(UINT32*)&cpu_info.model_name[20],*(UINT32*)&cpu_info.model_name[24],*(UINT32*)&cpu_info.model_name[28],0x80000003UL,0);
    CPUID(*(UINT32*)&cpu_info.model_name[32],*(UINT32*)&cpu_info.model_name[36],*(UINT32*)&cpu_info.model_name[40],*(UINT32*)&cpu_info.model_name[44],0x80000004UL,0);

    // 获取CPU频率
    CPUID(cpu_info.fundamental_frequency,cpu_info.maximum_frequency,cpu_info.bus_frequency,edx,0x16,0);

    // 获取CPU TSC频率
    CPUID(eax,ebx,ecx,edx,0x15,0);
    cpu_info.tsc_frequency = (ecx != 0) ? ebx*ecx/eax : 0;

    return;
}

__attribute__((section(".init_text"))) void init_cpu_amode(void){
    UINT32 eax,ebx,ecx,edx;
    UINT64 tmp,value;

//region IA32_APIC_BASE_MSR (MSR 0x1B)
//X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。X2APIC 是 APIC 的扩展版本，提供了更多的功能，例如更大的中断目标地址空间。
//EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
//BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
//APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
////endregion
    RDMSR(eax,edx,IA32_APIC_BASE_MSR);
    eax |= 0xC00;                        //bit8 1=bsp 0=ap bit10 X2APIC使能   bit11 APIC全局使能
    WRMSR(eax,edx,IA32_APIC_BASE_MSR);

//region CR4 寄存器
//VME（bit 0） 描述：启用虚拟 8086 模式的扩展功能，允许在虚拟 8086 模式中支持虚拟中断。用途：用于实现虚拟机监控或虚拟 8086 环境中的精细中断控制。
//PVI（bit 1） 描述：启用保护模式下的虚拟中断标志，支持在虚拟化环境中进行中断管理。用途：允许虚拟化软件在保护模式下模拟中断处理，用于虚拟机监控程序。
//TSD（bit 2） 描述：控制是否允许非特权代码访问 `RDTSC` 指令。如果设置为 1，只有 CPL = 0 的代码可以使用 RDTSC。用途：限制非特权用户访问时间戳计数器，避免恶意代码分析系统性能。
//DE（bit 3）  描述：启用对调试寄存器 DR4 和 DR5 的特殊使用。如果设置为 1，则使用 DR4 和 DR5 会触发异常。用途：增强调试功能，支持更精确的断点和调试操作。
//PSE（bit 4） 描述：控制 4MB 的页大小（相对于标准的 4KB 页大小）。当该位设置为 1 时，启用 4MB 页大小。用途：大页面有助于减少分页表的管理开销，并提高性能。
//PAE（bit 5） 描述：启用 36 位的物理地址扩展（PAE），支持物理内存超过 4GB 的地址空间。用途：对于支持超过 4GB 内存的 32 位系统，PAE 是必需的。
//MCE（bit 6） 描述：启用机器检查异常。如果硬件检测到机器检查错误，则会触发该异常。用途：用于检测 CPU 或内存的硬件错误。
//PGE（bit 7） 描述：允许全局页功能。如果启用全局页，则不会在上下文切换时刷新这些页的 TLB（转换后备缓冲）。用途：提高性能，特别是对于内核和共享内存。
//PCE（bit 8） 描述：启用在用户模式下访问性能监控计数器的权限。用途：允许用户模式下访问性能计数器，用于性能分析。
//OSFXSR（bit 9）描述：启用操作系统对 FXSAVE 和 FXRSTOR 指令的支持，这些指令用于保存和恢复浮点及 SIMD 状态。用途：用于支持 SSE（Streaming SIMD Extensions）指令集。
//OSXMMEXCPT（bit 10）描述：启用对未屏蔽的 SIMD 浮点异常的操作系统支持。用途：在使用 SSE 指令集时启用更精细的异常处理。
//UMIP（bit 11）描述：防止某些系统指令（如 SGDT 和 SIDT）在用户模式下执行。用途：增强安全性，防止用户模式程序获取系统敏感信息。
//LA57（bit 12）描述：启用 5 级分页（用于支持更大的虚拟地址空间）。用途：扩展虚拟地址空间到 57 位地址范围，特别是在大规模内存服务器上使用。
//VMXE（bit 13）描述：启用虚拟化技术的支持。如果设置，处理器将能够执行虚拟化相关指令（如 VMX 指令集）。用途：支持硬件虚拟化技术。
//SMXE（bit 14）描述：启用安全模式扩展的支持，用于执行安全相关的指令。用途：增强平台安全性，主要用于 Intel 的 TXT 技术。
//FSGSBASE（bit 16）描述：启用对 RDFSBASE、WRFSBASE、RDGSBASE 和 WRGSBASE 指令的支持，允许直接从 FS 和 GS 段寄存器读取和写入基地址。用途：提高用户模式和内核模式之间的切换效率。
//PCIDE（bit 17）描述：启用进程上下文标识符（PCID），允许在不同的进程之间缓存 TLB 条目，从而避免 TLB 的频繁刷新。用途：提高上下文切换时的性能。
//OSXSAVE（bit 18）描述：启用对 XSAVE/XRSTOR 指令的支持，这些指令用于保存和恢复扩展处理器状态（如 AVX 寄存器）。用途：支持 AVX、AVX2 和其他扩展状态的保存与恢复。
//SMEP（bit 20）描述：启用内核模式下禁止执行用户空间代码。如果启用，内核模式下试图执行用户空间代码会触发错误。用途：增强操作系统安全性，防止内核模式的代码误执行用户空间的恶意代码。
//SMAP（bit 21）描述：启用内核模式下禁止访问用户空间内存。内核模式试图访问用户空间内存会触发错误，除非显式禁用访问。用途：进一步增强内核的安全性，防止潜在的漏洞利用。
//PKE（bit 22）描述：启用内存保护密钥功能。该功能允许程序在不修改页表的情况下控制内存的访问权限。用途：提供更灵活的内存保护机制，用于区分不同的内存访问权限。
//endregion
    tmp=0;
    CPUID(eax,ebx,ecx,edx,0x7,0);
    if(ecx & 4)
        tmp |= 0x800;       //bit11 UMIP

    if(ecx & 8)
        tmp |= 0x400000;    //bit11 UMIP

    if(ebx & 0x80)
        tmp |= 0x100000;    //bit20 SMEP

//    if(ebx & 0x100000)
//        tmp |= 0x200000;    //bit21 SMAP

    CPUID(eax,ebx,ecx,edx,0x1,0);
    if(ecx & 0x20)
        tmp |= 0x2000;      //bit13 VMXE

    if(ecx & 0x20000)
        tmp |= 0x20000;     //bit17 PCIDE

    tmp |= 0x507C8;
    GET_CR4(value);
    value |= tmp;
    SET_CR4(value);

//region XCR0 寄存器
//x87（bit 0）：描述：控制 x87 浮点状态的保存与恢复。x87 是早期的浮点运算单元，负责处理浮点数运算。用途：如果该位被设置为 1，XSAVE 和 XRSTOR 指令将保存和恢复 x87 浮点状态。
//SSE（bit 1）：描述：控制 SSE 状态（XMM 寄存器）的保存与恢复。SSE 是现代 SIMD 指令集，用于处理浮点和整数数据。用途：设置该位后，XSAVE 和 XRSTOR 将保存和恢复 SSE 的 XMM 寄存器状态。
//AVX（bit 2）：描述：控制 AVX 状态（YMM 寄存器）的保存与恢复。AVX 是比 SSE 更强大的 SIMD 指令集，能够处理 256 位数据。用途：启用该位后，处理器将保存和恢复 AVX 的 YMM 寄存器状态。
//MPX（bit 3-4）：描述：控制 MPX（内存保护扩展）寄存器的保存与恢复。MPX 用于检测和防止内存边界越界问题。用途：启用该位后，XSAVE 和 XRSTOR 将保存 MPX 寄存器的状态。
//AVX-512（bit 5）：描述：控制 AVX-512 状态（ZMM 寄存器）的保存与恢复。AVX-512 是 AVX 的扩展，使用 512 位寄存器，提供了更大的数据并行性。用途：启用该位后，XSAVE 和 XRSTOR 将保存和恢复 AVX-512 的寄存器状态。
//BNDCSR（bit 6）：描述：控制 MPX 的 BNDCSR 状态的保存与恢复。BNDCSR 用于管理 MPX 的边界检查寄存器。用途：启用该位后，处理器会保存和恢复 MPX 边界检查的控制状态。
//PKRU（bit 8）：描述：控制 PKRU 状态的保存与恢复。PKRU（Protection Keys for Userspace）是内存保护的一种机制。用途：启用该位后，处理器会保存和恢复与 PKRU 相关的状态.
//endregion
    CPUID(eax,ebx,ecx,edx,0x7,0x0);
    tmp=(ebx & 0x10000) ? 0xE7 : 0x7;   //AVX512=0xE7 AVX256=0x7
    XGETBV(eax,edx,0);
    eax |= tmp;
    XSETBV(eax,edx,0);

//region IA32_EFER_MSR 寄存器（MSR 0xC0000080)
//SCE（bit 0） 1:启用 SYSCALL 和 SYSRET 指令。
//LME（bit 8） 1:启用 64 位长模式。当该位被设置为 1 时，处理器允许进入 64 位模式。在启用长模式时，CR0.PG（分页启用位）和 CR4.PAE（物理地址扩展启用位）也必须设置。
//NXE（bit 11）1:sfdsfs启用 NX（No-eXecute） 位功能。NX 位用于控制某些内存页面的执行权限。如果该位被设置为 1，操作系统可以使用分页机制将特定的内存页面标记为不可执行，以防止执行非代码数据，如栈或堆内存，防止某些缓冲区溢出攻击。
//endregion
    RDMSR(eax,edx,IA32_EFER_MSR);
    eax |= 0x801;
    WRMSR(eax,edx,IA32_EFER_MSR);

//region CR0寄存器
//PE（位 0）：1：启用保护模式，使得 CPU 能使用分段和分页机制。0：CPU 处于实模式，仅支持基础的内存访问。
//MP（位 1）：1：协处理器错误将触发设备不可用异常 (#NM)。0：不会触发异常，协处理器错误被忽略。
//EM（位 2）：1：所有浮点指令将导致非法指令异常 (#UD)。0：允许执行浮点指令，硬件协处理器可用。
//TS（位 3）：1：表示任务已切换，需要刷新协处理器状态。0：未进行任务切换，不需要刷新状态。
//ET（位 4）：1：使用 80387 协处理器。0：使用 80287 协处理器。
//NE（位 5）：1：浮点异常会触发处理器异常 (#MF)。0：禁用浮点异常。
//WP（位 16）：1：启用写保护，即使在超级用户模式下也无法修改只读页。0：禁用写保护，允许超级用户模式修改只读页。
//AM（位 18）：1：启用对齐检查，当 CR4.AM 和 EFLAGS.AC 也设置时生效。0：禁用对齐检查。
//NW（位 29）：1：禁用写回缓存策略。0：启用写回缓存策略。
//CD（位 30）：1：禁用 CPU 缓存，所有内存访问直接访问主存。0：启用 CPU 缓存，提高性能。
//PG（位 31）：1：启用分页机制，支持虚拟内存管理。0：禁用分页，CPU 只能使用物理内存地址。
//endregion
    GET_CR0(value);
    value &= 0xFFFFFFFF9FFFFFFFUL;
    value |= 0x10002;
    SET_CR0(value);

    return;
}



