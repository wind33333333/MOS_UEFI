#ifndef __IDT_H__
#define __IDT_H__
#include "moslib.h"

#define SET_GATE_L(OFFSET,IST,TYPE) (IST | TYPE | SEL_CODE64 | DPL_0 | P | ((UINT64)(OFFSET) & 0x000000000000FFFF) | (((UINT64)(OFFSET) >> 16) << 48))
#define SET_GATE_H(OFFSET) ((UINT64)(OFFSET) >> 32)
#define SET_GATE(BASE,NUM,OFFSET,IST,TYPE) \
                do{\
                BASE[NUM*2] = SET_GATE_L(OFFSET,IST,TYPE);\
                BASE[NUM*2+1] = SET_GATE_H(OFFSET);\
                }while(0)

#define TYPE_CALL       (0xCUL << 40)
#define TYPE_INT        (0xEUL << 40)
#define TYPE_TRAP       (0xFUL << 40)
#define IST_0           (0x0UL << 32)
#define IST_1           (0x1UL << 32)
#define IST_2           (0x2UL << 32)
#define IST_3           (0x3UL << 32)
#define IST_4           (0x4UL << 32)
#define IST_5           (0x5UL << 32)
#define IST_6           (0x6UL << 32)
#define IST_7           (0x7UL << 32)
#define SEL_CODE64      (0x8UL << 16)

typedef struct{
    UINT16 limit;
    UINT64 *base;
}__attribute__((packed)) idt_ptr_t;

extern idt_ptr_t idt_ptr;

void init_idt(void);


#endif