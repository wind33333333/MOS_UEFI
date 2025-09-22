#pragma once

#define INIT_TEXT __attribute__((__section__(".init_text")))
#define INIT_DATA __attribute__((__section__(".init_data")))
#define STACK_SECTION __attribute__((__section__(".stack")))

typedef unsigned char uint8;
typedef uint8 boolean;
typedef uint8 char8;
typedef unsigned short uint16;
typedef uint16 char16;
typedef unsigned int uint32;
typedef unsigned long long  uint64;
typedef char int8;
typedef short int16;
typedef int int32;
typedef long long int64;

extern char _start[];
extern char _start_init_text[];
extern char _end_init_text[];
extern char _start_init_data[];
extern char _end_init_data[];
extern char _start_text[];
extern char _end_text[];
extern char _start_data[];
extern char _end_data[];
extern char _start_bss[];
extern char _end_bss[];
extern char _start_stack[];
extern char _end_stack[];
extern char _end[];
extern uint64 tmp_pml4t[];

#define BOCHS_DG()    __asm__ __volatile__ ("xchg %%bx,%%bx \n\t":: :);

#define NULL 0
#define FALSE 0
#define TRUE 1

//取通过结构成员偏移量
#define OFFSETOF(type, member) ((uint8*)&((type*)0)->member)
//通过成员计算结构起始地址
#define CONTAINER_OF(ptr,type,member) (type*)((uint8*)ptr-OFFSETOF(type,member))

// 开启中断 (STI)
static inline void sti(void) {
    __asm__ __volatile__("sti \n\t" ::: "memory");
}

// 关闭中断 (CLI)
static inline void cli(void) {
    __asm__ __volatile__("cli \n\t" ::: "memory");
}

// 开启对用户态访问的支持 (STAC)
static inline void stac(void) {
    __asm__ __volatile__("stac \n\t" ::: "memory");
}

// 关闭对用户态访问的支持 (CLAC)
static inline void clac(void) {
    __asm__ __volatile__("clac \n\t" ::: "memory");
}

// CPU等待指令 (PAUSE)
static inline void pause(void) {
    __asm__ __volatile__("pause \n\t" ::: "memory");
}

// 读写操作内存屏障 (MFENCE)
static inline void mfence(void) {
    __asm__ __volatile__("mfence \n\t" ::: "memory");
}

// 写操作单独内存屏障 (SFENCE)
static inline void sfence(void) {
    __asm__ __volatile__("sfence \n\t" ::: "memory");
}

// 读操作单独内存屏障 (LFENCE)
static inline void lfence(void) {
    __asm__ __volatile__("lfence \n\t" ::: "memory");
}

//设置bit位
static inline bts(uint64 *addr,uint64 nr) {
    __asm__ __volatile__(
        "lock \n\t"
        "btsq   %1,%0 \n\t"
        :"+m"(*addr)
        :"ir"(nr)
        :"memory"
        );
}

//清除bit位
static inline btr(uint64 *addr,uint64 nr) {
    __asm__ __volatile__(
        "lock \n\t"
        "btrq   %1,%0 \n\t"
        :"+m"(*addr)
        :"ir"(nr)
        :"memory"
        );
}

//位测试
static inline boolean bt(uint64 var,uint64 nr) {
    boolean ret;
    __asm__ __volatile__(
        "btq   %2,%1 \n\t"
        "setc  %0 \n\t"
        :"=r"(ret)
        :"m"(var),"ir"(nr)
        :"memory"
        );
    return ret;
}

//位扫描最低位1
static inline uint32 bsf(uint64 var) {
    uint32 ret;
    __asm__ __volatile__(
        "bsf    %1,%0   \n\t"
        :"=r"(ret)
        :"m"(var)
        :"memory"
        );
    return ret;
}

//位扫描最高位1
static inline uint32 bsr(uint64 var) {
    uint32 ret;
    __asm__ __volatile__(
        "bsr    %1,%0   \n\t"
        :"=r"(ret)
        :"m"(var)
        :"memory"
        );
    return ret;
}

// 自旋锁
static inline void spin_lock(volatile uint8 *lock_var) {
    __asm__ __volatile__ (
            "mov        $1,%%bl         \n\t"  // 将值1加载到BL寄存器中
            "1:                         \n\t"
            "xor        %%al,%%al       \n\t"  // 清空AL寄存器（设置为0）
            "lock                       \n\t"  // 确保后续的操作是原子的
            "cmpxchg    %%bl,%0         \n\t"  // 比较 lock_var 和 AL，若相等，则将 BL 写入 lock_var
            "jnz        1b              \n\t"  // 如果未能成功锁定，则跳转到标签1重试
            "pause                      \n\t"  // 优化的CPU等待，减少功耗和资源占用
            :
            :"m"(*lock_var)
            :"%rax","%rbx","memory"
            );
}

static inline void invlpg(void *vir_addr) {
    __asm__ __volatile__("invlpg (%0) \n\t" : : "r"(vir_addr) : "memory");
}

static inline void lgdt(void *gdt_ptr, uint16 code64_sel, uint16 data64_sel) {
    __asm__ __volatile__(
            "lgdtq       (%0)                \n\t"  // 加载 GDT 描述符地址
            "pushq       %q1                 \n\t"  // 压入代码段选择器
            "leaq        1f(%%rip), %%rax    \n\t"  // 获取返回地址
            "pushq       %%rax               \n\t"  // 压入返回地址
            "lretq                           \n\t"  // 执行长返回，切换到新代码段选择子
            "1:                              \n\t"  // 跳转目标标记
            "movw        %2, %%ss            \n\t"  // 设置堆栈段选择器
            "movw        %2, %%ds            \n\t"  // 设置数据段选择器
            "movw        %2, %%es            \n\t"  // 设置额外段选择器
            "movw        %2, %%gs            \n\t"  // 设置全局段选择器
            "movw        %2, %%fs            \n\t"  // 设置额外段选择器
            :
            : "r"(gdt_ptr), "r"(code64_sel), "r"(data64_sel)
            : "memory", "%rax"
            );
}

static inline void lidt(void *idt_ptr) {
    __asm__ __volatile__(
            "lidt (%0) \n\t"  // 加载 IDT 描述符地址
            :
            : "r"(idt_ptr)    // 输入：IDT 描述符的地址
            : "memory"        // 防止编译器重排序内存操作
            );
}

static inline void ltr(uint16 tss_sel) {
    __asm__ __volatile__(
            "ltr    %w0 \n\t"
            :
            : "r"(tss_sel)
            :
            );
}

static inline void rdtscp(uint32 *apic_id,uint64 *timestamp) {
    __asm__ __volatile__(
            "rdtscp                 \n\t"  // 执行 rdtscp 指令
            "shlq    $32, %%rdx     \n\t"  // 将高 32 位左移 32 位
            "orq     %%rdx, %%rax   \n\t"  // 合并高低位到 RAX
            : "=a" (*timestamp),"=c" (*apic_id)
            :
            : "rdx","memory"
            );
}

static inline uint64 get_cr0(void) {
    uint64 cr0;
    __asm__ __volatile__(
            "movq   %%cr0,%%rax \n\t"
            :"=a"(cr0)
            :
            : "memory"
            );
    return  cr0;
}

static inline void set_cr0(uint64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr0 \n\t"
            :
            :"r"(value)
            : "memory"
            );
}

static inline uint64 get_cr3(void) {
    uint64 cr3;
    __asm__ __volatile__(
            "movq   %%cr3,%%rax \n\t"
            : "=a"(cr3)         // 输出：将 CR3 的值存入 value
            :                   // 无输入
            : "memory"          // 通知编译器：此操作可能影响内存
            );
    return cr3;
}

static inline void set_cr3(uint64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr3 \n\t"
            :
            :"r"(value)
            : "memory"
            );
}

static inline uint64 get_cr4(void) {
    uint64 cr4;
    __asm__ __volatile__(
            "movq   %%cr4,%%rax \n\t"
            : "=a"(cr4)         // 输出：将 CR3 的值存入 value
            :                   // 无输入
            : "memory"          // 通知编译器：此操作可能影响内存
            );
    return cr4;
}

static inline void set_cr4(uint64 value) {
    __asm__ __volatile__(
            "movq   %0,%%cr4 \n\t"
            :
            :"r"(value)
            : "memory"
            );
}

static inline uint64 xgetbv(uint32 register_number) {
    uint64 xcr;
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

static inline void xsetbv(uint32 register_number,uint64 value) {
    __asm__ __volatile__(
            "xsetbv \n\t"
            :
            : "a"((uint32)(value & 0xFFFFFFFFUL)),"d"((uint32)(value >> 32)),"c"(register_number)
            : "memory"
            );
}

static inline uint64 rdmsr(uint32 register_number) {
    uint64 msr;
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

static inline void wrmsr(uint32 register_number,uint64 value) {
    __asm__ __volatile__(
            "wrmsr \n\t"
            :
            : "a"((uint32)(value & 0xFFFFFFFFUL)),"d"((uint32)(value >> 32)),"c"(register_number)
            : "memory"
            );
}

static inline void cpuid(uint32 in_eax, uint32 in_ecx,uint32 *out_eax, uint32 *out_ebx,uint32 *out_ecx, uint32 *out_edx) {
    __asm__ __volatile__(
            "cpuid \n\t"
            : "=a"(*out_eax),"=b"(*out_ebx),"=c"(*out_ecx),"=d"(*out_edx)
            : "a"(in_eax),"c"(in_ecx)
            : "memory"
            );
}

static inline void *mem_cpy(void *From, void *To, long Num) {
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

static inline int mem_cmp(void *FirstPart, void *SecondPart, long Count) {
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

static inline void *mem_set(void *Address, uint8 C, long Count) {
    int d0, d1;
    uint64 tmp = C * 0x0101010101010101UL;
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

static inline uint64 reverse_find_qword(void *address,uint64 count,uint64 value){
    uint64 result;
    __asm__ __volatile__(
            "std                  \n\t"
            "repz                 \n\t"
            "scasq                \n\t"
            "je  1f               \n\t"
            "inc   %%rcx          \n\t"
            "1:                   \n\t"
            :"=c"(result)
            :"a"(value),"c"(count),"D"((uint64)address+(count-1<<3))
            :"memory"
            );
    return result;
}

static inline uint64 forward_find_qword(void *address,uint64 count,uint64 value){
    uint64 result;
    __asm__ __volatile__(
            "cld                  \n\t"
            "repz                 \n\t"
            "scasq                \n\t"
            "je  1f               \n\t"
            "subq   %%rcx,%%rdx   \n\t"
            "movq   %%rdx,%%rcx   \n\t"
            "1:                   \n\t"
            :"=c"(result)
            :"a"(value),"c"(count),"d"(count),"D"(address)
            :"memory"
            );
    return result;
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

static inline void io_in8(uint16 port, uint8 *value) {
    __asm__ __volatile__(
            "inb %%dx,%%al \n\t"
            :"=a"(*value)
            :"d"(port)
            :"memory");
}

static inline void io_out8(uint16 port, uint8 value) {
    __asm__ __volatile__(
            "outb %%al,%%dx \n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

static inline void io_in32(uint16 port, uint32 *value) {
    __asm__ __volatile__(
            "inl %%dx,%%eax \n\t"
            :"=a"(*value)
            :"d"(port)
            :"memory");
}

static inline void io_out32(uint16 port, uint32 value) {
    __asm__ __volatile__(
            "outl %%eax,%%dx \n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

/*
 * 链表操作函数
 */
typedef struct list_head_t{
    struct list_head_t *prev;
    struct list_head_t *next;
}list_head_t;

static inline void list_add_head(list_head_t *head,list_head_t *new) {
    new->next = head->next;  // 新节点指向原头节点
    new->prev = head;        // 新节点前驱指向头节点
    head->next->prev = new;  // 原头节点的前驱指向新节点
    head->next = new;        // 头节点指向新节点
}

static inline void list_add_tail(list_head_t *head,list_head_t *new) {
    new->next = head;        // 新节点指向头节点（循环）
    new->prev = head->prev;  // 新节点前驱指向原尾节点
    head->prev->next = new;  // 原尾节点的后继指向新节点
    head->prev = new;        // 头节点的前驱指向新节点
}

static inline void list_del(list_head_t * node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
}

static inline void list_head_init(list_head_t *head) {
    head->prev = head;
    head->next = head;
}

static inline boolean list_find(list_head_t *head,list_head_t *node) {
    list_head_t *next = head;
    while (next->next != head) {
        if (next->next == node)
            return TRUE;
        next=next->next;
    }
    return FALSE;
}

static inline boolean list_empty(list_head_t *head) {
    if (head->next == NULL && head->prev == NULL)
        return 1;
    return 0;
}

