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

//region 自旋锁
#define SPIN_LOCK(lock_var) \
    do {                \
    __asm__ __volatile__ ( \
        "mov        $1,%%bl     \n\t" \
        "1:\txor    %%al,%%al   \n\t" \
        "lock                   \n\t" \
        "cmpxchg    %%bl,%0     \n\t" \
        "jnz        1b          \n\t" \
        "pause                  \n\t" \
        ::"m"(lock_var):"%rax","%rbx"); \
    } while(0)
//endregion

#define container_of(ptr, type, member)                            \
({                                            \
    typeof(((type *)0)->member) * p = (ptr);                    \
    (type *)((UINT64)p - (UINT64)&(((type *)0)->member));        \
})


#define STI()        __asm__ __volatile__ ("sti	\n\t":::"memory")
#define CLI()        __asm__ __volatile__ ("cli	\n\t":::"memory")
#define PAUSE()       __asm__ __volatile__ ("pause	\n\t":::"memory")
#define MFENCE()    __asm__ __volatile__ ("mfence	\n\t":::"memory")
#define SFENCE()    __asm__ __volatile__ ("sfence	\n\t":::"memory")
#define LFENCE()    __asm__ __volatile__ ("lfence	\n\t":::"memory")

struct List {
    struct List *prev;
    struct List *next;
};

void list_init(struct List *list) {
    list->prev = list;
    list->next = list;
}

void list_add_to_behind(struct List *entry, struct List *new)    ////add to entry behind
{
    new->next = entry->next;
    new->prev = entry;
    new->next->prev = new;
    entry->next = new;
}

void list_add_to_before(struct List *entry, struct List *new)    ////add to entry behind
{
    new->next = entry;
    entry->prev->next = new;
    new->prev = entry->prev;
    entry->prev = new;
}

void list_del(struct List *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
}

long list_is_empty(struct List *entry) {
    if (entry == entry->next && entry->prev == entry)
        return 1;
    else
        return 0;
}

struct List *list_prev(struct List *entry) {
    if (entry->prev != NULL)
        return entry->prev;
    else
        return NULL;
}

struct List *list_next(struct List *entry) {
    if (entry->next != NULL)
        return entry->next;
    else
        return NULL;
}

/*
		From => To memory copy Num bytes
*/

void *memcpy(void *From, void *To, long Num) {
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

/*
		FirstPart = SecondPart		=>	 0
		FirstPart > SecondPart		=>	 1
		FirstPart < SecondPart		=>	-1
*/

int memcmp(void *FirstPart, void *SecondPart, long Count) {
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

/*
		set memory at Address with C ,number is Count
*/

void *mem_set(void *Address, UINT8 C, long Count) {
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

/*
		string copy
*/

char *strcpy(char *Dest, char *Src) {
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

/*
		string copy number bytes
*/

char *strncpy(char *Dest, char *Src, long Count) {
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

/*
		string cat Dest + Src
*/

char *strcat(char *Dest, char *Src) {
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

/*
		string compare FirstPart and SecondPart
		FirstPart = SecondPart =>  0
		FirstPart > SecondPart =>  1
		FirstPart < SecondPart => -1
*/

int strcmp(char *FirstPart, char *SecondPart) {
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

/*
		string compare FirstPart and SecondPart with Count Bytes
		FirstPart = SecondPart =>  0
		FirstPart > SecondPart =>  1
		FirstPart < SecondPart => -1
*/

int strncmp(char *FirstPart, char *SecondPart, long Count) {
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

/*

*/

int strlen(char *String) {
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

/*

*/

UINT64 bit_set(UINT64 *addr, UINT64 nr) {
    return *addr | (1UL << nr);
}

/*

*/

UINT64 bit_get(UINT64 *addr, UINT64 nr) {
    return *addr & (1UL << nr);
}

/*

*/

UINT64 bit_clean(UINT64 *addr, UINT64 nr) {
    return *addr & (~(1UL << nr));
}

/*

*/

UINT8 io_in8(UINT16 port) {
    UINT8 ret = 0;
    __asm__ __volatile__(    "inb	%%dx,	%0	\n\t"
                             "mfence			\n\t"
            :"=a"(ret)
            :"d"(port)
            :"memory");
    return ret;
}

/*

*/

UINT32 io_in32(UINT16 port) {
    UINT32 ret = 0;
    __asm__ __volatile__(    "inl	%%dx,	%0	\n\t"
                             "mfence			\n\t"
            :"=a"(ret)
            :"d"(port)
            :"memory");
    return ret;
}

/*

*/

void io_out8(UINT16 port, UINT8 value) {
    __asm__ __volatile__(    "outb	%0,	%%dx	\n\t"
                             "mfence			\n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

/*

*/

void io_out32(UINT16 port, UINT32 value) {
    __asm__ __volatile__(    "outl	%0,	%%dx	\n\t"
                             "mfence			\n\t"
            :
            :"a"(value), "d"(port)
            :"memory");
}

/*

*/

#define port_insw(port, buffer, nr)    \
__asm__ __volatile__("cld;rep;insw;mfence;"::"d"(port),"D"(buffer),"c"(nr):"memory")

#define port_outsw(port, buffer, nr)    \
__asm__ __volatile__("cld;rep;outsw;mfence;"::"d"(port),"S"(buffer),"c"(nr):"memory")



#endif
