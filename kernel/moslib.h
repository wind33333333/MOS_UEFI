#ifndef __MOSLIB_H__
#define __MOSLIB_H__

typedef unsigned char UINT8;
typedef UINT8 BOOLEAN;
typedef UINT8 CHAR8;
typedef unsigned short UINT16;
typedef UINT16 CHAR16;
typedef unsigned int UINT32;
typedef unsigned long long  UINT64;
typedef char INT8;
typedef short INT16;
typedef int INT32;
typedef long long INT64;

#define BOCHS_DG()    __asm__ __volatile__ ("xchg %%bx,%%bx \n\t":: :);

#define NULL 0

#define STI()       __asm__ __volatile__ ("sti	\n\t":::"memory")
#define CLI()       __asm__ __volatile__ ("cli	\n\t":::"memory")
#define STAC()      __asm__ __volatile__ ("stac	\n\t":::"memory")
#define CLAC()      __asm__ __volatile__ ("clac	\n\t":::"memory")
#define PAUSE()     __asm__ __volatile__ ("pause	\n\t":::"memory")
#define MFENCE()    __asm__ __volatile__ ("mfence	\n\t":::"memory")
#define SFENCE()    __asm__ __volatile__ ("sfence	\n\t":::"memory")
#define LFENCE()    __asm__ __volatile__ ("lfence	\n\t":::"memory")

// 自旋锁的实现
static inline void spin_lock(volatile UINT8 *lock_var) {
    __asm__ __volatile__ (
            "mov        $1,%%bl         \n\t"  // 将值1加载到BL寄存器中
            "1:                         \n\t"
            "xor    %%al,%%al           \n\t"  // 清空AL寄存器（设置为0）
            "lock                       \n\t"  // 确保后续的操作是原子的
            "cmpxchg    %%bl,%0         \n\t"  // 比较 lock_var 和 AL，若相等，则将 BL 写入 lock_var
            "jnz        1b              \n\t"  // 如果未能成功锁定，则跳转到标签1重试
            "pause                      \n\t"  // 优化的CPU等待，减少功耗和资源占用
            :
            :"m"(*lock_var)
            :"%rax","%rbx","memory"
            );
    return;
}

static inline void rdtscp(UINT32 *apic_id,UINT64 *timestamp) {
    __asm__ __volatile__(
            "rdtscp                 \n\t"  // 执行 rdtscp 指令
            "shlq    $32, %%rdx     \n\t"  // 将高 32 位左移 32 位
            "orq     %%rdx, %%rax   \n\t"  // 合并高低位到 RAX
            : "=a" (*timestamp),"=c" (*apic_id)
            :
            : "rdx","memory"
            );
    return;
}

static inline UINT64 get_cr0(void) {
    UINT64 cr0;
    __asm__ __volatile__(
            "movq   %%cr0,%%rax \n\t"
            :"=a"(cr0)
            :
            : "memory"
            );
    return  cr0;
}

static inline void set_cr0(UINT64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr0 \n\t"
            :
            :"r"(value)
            : "memory"
            );
    return;
}

static inline UINT64 get_cr3(void) {
    UINT64 cr3;
    __asm__ __volatile__(
            "movq   %%cr3,%%rax \n\t"
            : "=a"(cr3)         // 输出：将 CR3 的值存入 value
            :                   // 无输入
            : "memory"          // 通知编译器：此操作可能影响内存
            );
    return cr3;
}

static inline void set_cr3(UINT64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr3 \n\t"
            :
            :"r"(value)
            : "memory"
            );
    return;
}

static inline UINT64 get_cr4(void) {
    UINT64 cr4;
    __asm__ __volatile__(
            "movq   %%cr4,%%rax \n\t"
            : "=a"(cr4)         // 输出：将 CR3 的值存入 value
            :                   // 无输入
            : "memory"          // 通知编译器：此操作可能影响内存
            );
    return cr4;
}

static inline void set_cr4(UINT64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr4 \n\t"
            :
            :"r"(value)
            : "memory"
            );
    return;
}

static inline UINT64 xgetbv(UINT32 register_number) {
    UINT64 xcr;
    __asm__ __volatile__(
            "xgetbv             \n\t"  // 执行 xgetbv 指令
            "shlq   $32,%%rdx   \n\t"
            "orq    %%rdx,%%rax \n\t"
            : "=a"(xcr)
            : "c"(register_number)                  // 输入 XCR 编号到 ECX
            : "memory"
            );
    return xcr;
}

static inline void xsetbv(UINT32 register_number,UINT64 value) {
    __asm__ __volatile__(
            "xsetbv \n\t"
            :
            : "a"((UINT32)(value & 0xFFFFFFFFUL)),"d"((UINT32)(value >> 32)),"c"(register_number)
            : "memory"
            );
    return;
}

static inline UINT64 rdmsr(UINT32 register_number) {
    UINT64 msr;
    __asm__ __volatile__(
            "rdmsr              \n\t"
            "shlq   $32,%%rdx   \n\t"
            "orq    %%rdx,%%rax \n\t"
            : "=a"(msr)
            : "c"(register_number)
            : "memory"
            );
    return msr;
}

static inline void wrmsr(UINT32 register_number,UINT64 value) {
    __asm__ __volatile__(
            "wrmsr \n\t"
            :
            : "a"((UINT32)(value & 0xFFFFFFFFUL)),"d"((UINT32)(value >> 32)),"c"(register_number)
            : "memory"
            );
    return;
}

static inline void cpuid(UINT32 in_eax, UINT32 in_ecx,UINT32 *out_eax, UINT32 *out_ebx,UINT32 *out_ecx, UINT32 *out_edx) {
    __asm__ __volatile__(
            "cpuid \n\t"
            : "=a"(*out_eax),"=b"(*out_ebx),"=c"(*out_ecx),"=d"(*out_edx)
            : "a"(in_eax),"c"(in_ecx)
            : "memory"
            );
    return;
}

static inline void *memcpy(void *From, void *To, long Num) {
    int d0, d1, d2;
    __asm__ __volatile__    (    "cld	\n\t"
                                 "rep	\n\t"
                                 "movsq	\n\t"
                                 "testb	$4,%b4	\n\t"
                                 "je	1f	\n\t"
                                 "movsl	\n\t"
                                 "1:\ttestb	$2,%b4	\n\t"
                                 "je	2f	\n\t"
                                 "movsw	\n\t"
                                 "2:\ttestb	$1,%b4	\n\t"
                                 "je	3f	\n\t"
                                 "movsb	\n\t"
                                 "3:	\n\t"
            :"=&c"(d0), "=&D"(d1), "=&S"(d2)
            :"0"(Num / 8), "q"(Num), "1"(To), "2"(From)
            :"memory"
            );
    return To;
}

static inline int memcmp(void *FirstPart, void *SecondPart, long Count) {
    register int __res;

    __asm__ __volatile__    (    "cld	\n\t"        //clean direct
                                 "repe	\n\t"        //repeat if equal
                                 "cmpsb	\n\t"
                                 "je	1f	\n\t"
                                 "movl	$1,	%%eax	\n\t"
                                 "jl	1f	\n\t"
                                 "negl	%%eax	\n\t"
                                 "1:	\n\t"
            :"=a"(__res)
            :"0"(0), "D"(FirstPart), "S"(SecondPart), "c"(Count)
            :
            );
    return __res;
}

static inline void *mem_set(void *Address, UINT8 C, long Count) {
    int d0, d1;
    UINT64 tmp = C * 0x0101010101010101UL;
    __asm__ __volatile__    (    "cld	\n\t"
                                 "rep	\n\t"
                                 "stosq	\n\t"
                                 "testb	$4, %b3	\n\t"
                                 "je	1f	\n\t"
                                 "stosl	\n\t"
                                 "1:\ttestb	$2, %b3	\n\t"
                                 "je	2f\n\t"
                                 "stosw	\n\t"
                                 "2:\ttestb	$1, %b3	\n\t"
                                 "je	3f	\n\t"
                                 "stosb	\n\t"
                                 "3:	\n\t"
            :"=&c"(d0), "=&D"(d1)
            :"a"(tmp), "q"(Count), "0"(Count / 8), "1"(Address)
            :"memory"
            );
    return Address;
}

static inline char *strcpy(char *Dest, char *Src) {
    __asm__ __volatile__    (    "cld	\n\t"
                                 "1:	\n\t"
                                 "lodsb	\n\t"
                                 "stosb	\n\t"
                                 "testb	%%al,	%%al	\n\t"
                                 "jne	1b	\n\t"
            :
            :"S"(Src), "D"(Dest)
            :

            );
    return Dest;
}

static inline char *strncpy(char *Dest, char *Src, long Count) {
    __asm__ __volatile__    (    "cld	\n\t"
                                 "1:	\n\t"
                                 "decq	%2	\n\t"
                                 "js	2f	\n\t"
                                 "lodsb	\n\t"
                                 "stosb	\n\t"
                                 "testb	%%al,	%%al	\n\t"
                                 "jne	1b	\n\t"
                                 "rep	\n\t"
                                 "stosb	\n\t"
                                 "2:	\n\t"
            :
            :"S"(Src), "D"(Dest), "c"(Count)
            :
            );
    return Dest;
}

static inline char *strcat(char *Dest, char *Src) {
    __asm__ __volatile__    (    "cld	\n\t"
                                 "repne	\n\t"
                                 "scasb	\n\t"
                                 "decq	%1	\n\t"
                                 "1:	\n\t"
                                 "lodsb	\n\t"
                                 "stosb	\n\r"
                                 "testb	%%al,	%%al	\n\t"
                                 "jne	1b	\n\t"
            :
            :"S"(Src), "D"(Dest), "a"(0), "c"(0xffffffff)
            :
            );
    return Dest;
}

static inline int strcmp(char *FirstPart, char *SecondPart) {
    register int __res;
    __asm__ __volatile__    (    "cld	\n\t"
                                 "1:	\n\t"
                                 "lodsb	\n\t"
                                 "scasb	\n\t"
                                 "jne	2f	\n\t"
                                 "testb	%%al,	%%al	\n\t"
                                 "jne	1b	\n\t"
                                 "xorl	%%eax,	%%eax	\n\t"
                                 "jmp	3f	\n\t"
                                 "2:	\n\t"
                                 "movl	$1,	%%eax	\n\t"
                                 "jl	3f	\n\t"
                                 "negl	%%eax	\n\t"
                                 "3:	\n\t"
            :"=a"(__res)
            :"D"(FirstPart), "S"(SecondPart)
            :
            );
    return __res;
}

static inline int strncmp(char *FirstPart, char *SecondPart, long Count) {
    register int __res;
    __asm__ __volatile__    (    "cld	\n\t"
                                 "1:	\n\t"
                                 "decq	%3	\n\t"
                                 "js	2f	\n\t"
                                 "lodsb	\n\t"
                                 "scasb	\n\t"
                                 "jne	3f	\n\t"
                                 "testb	%%al,	%%al	\n\t"
                                 "jne	1b	\n\t"
                                 "2:	\n\t"
                                 "xorl	%%eax,	%%eax	\n\t"
                                 "jmp	4f	\n\t"
                                 "3:	\n\t"
                                 "movl	$1,	%%eax	\n\t"
                                 "jl	4f	\n\t"
                                 "negl	%%eax	\n\t"
                                 "4:	\n\t"
            :"=a"(__res)
            :"D"(FirstPart), "S"(SecondPart), "c"(Count)
            :
            );
    return __res;
}

static inline int strlen(char *String) {
    register int __res;
    __asm__ __volatile__    (    "cld	\n\t"
                                 "repne	\n\t"
                                 "scasb	\n\t"
                                 "notl	%0	\n\t"
                                 "decl	%0	\n\t"
            :"=c"(__res)
            :"D"(String), "a"(0), "0"(0xffffffff)
            :
            );
    return __res;
}

static inline void io_in8(UINT16 port, UINT8 *value) {
    __asm__ __volatile__("inb %%dx,%%al \n\t"
            :"=a"(*value)
            :"d"(port)
            :"memory");
}

static inline void io_out8(UINT16 port, UINT8 value) {
    __asm__ __volatile__("outb %%al,%%dx \n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

static inline void io_in32(UINT16 port, UINT32 *value) {
    __asm__ __volatile__("inl %%dx,%%eax \n\t"
            :"=a"(*value)
            :"d"(port)
            :"memory");
}

static inline void io_out32(UINT16 port, UINT32 value) {
    __asm__ __volatile__("outl %%eax,%%dx \n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

#endif
