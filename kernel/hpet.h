#ifndef _HPET_H_
#define _HPET_H_

#include "cpu.h"
#include "moslib.h"
#include "printk.h"
#include "memory.h"
#include "ioapic.h"

#define ENABLE_HPET_TIMES(TIMS_CONF,TIMS_COMP,TIME,MODEL,IRQ) \
        do {   \
           (TIMS_CONF) = (((IRQ) << 9) | (1UL << 6) | ((MODEL) << 3) | (1UL << 2)); \
           io_mfence();                           \
           (TIMS_COMP) = (TIME);                                                  \
           io_mfence();                                 \
         }while(0)

#define DISABLE_HPET_TIMES(TIMS_CONF) \
        do {                 \
           (TIMS_CONF) = 0;           \
           io_mfence();\
        }while(0)

#define HPET_ONESHOT 0
#define HPET_PERIODIC 1

typedef struct {
    unsigned long baseaddr;
    UINT32 frequency;
    UINT32 timernum;
}HPET_ATTR;

HPET_ATTR hpet_attr = {0};


typedef struct {
    unsigned long *GCAP_ID;      // 000h ~ 007h 整体机能寄存器
    unsigned long *GEN_CONF;     // 010h ~ 017h 整体配置寄存器
    unsigned long *GINTR_STA;    // 020h ~ 027h 整体中断转态寄存器
    unsigned long *MAIN_CNT;     // 028h ~ 02Fh 主计数器
    unsigned long *TIM0_CONF;    // 100h ~ 107h 定时器0配置寄存器
    unsigned long *TIM0_COMP;    // 108h ~ 10Fh 定时器0对比寄存器
    unsigned long *TIM1_CONF;    // 120h ~ 127h 定时器1配置寄存器
    unsigned long *TIM1_COMP;    // 128h ~ 12Fh 定时器1对比寄存器
    unsigned long *TIM2_CONF;    // 140h ~ 147h 定时器2配置寄存器
    unsigned long *TIM2_COMP;    // 148h ~ 14Fh 定时器2对比寄存器
    unsigned long *TIM3_CONF;    // 160h ~ 167h 定时器3配置寄存器
    unsigned long *TIM3_COMP;    // 168h ~ 16Fh 定时器3对比寄存器
    unsigned long *TIM4_CONF;    // 180h ~ 187h 定时器4配置寄存器
    unsigned long *TIM4_COMP;    // 188h ~ 18Fh 定时器4对比寄存器
    unsigned long *TIM5_CONF;    // 1A0h ~ 1A7h 定时器5配置寄存器
    unsigned long *TIM5_COMP;    // 1A8h ~ 1AFh 定时器5对比寄存器
    unsigned long *TIM6_CONF;    // 1C0h ~ 1C7h 定时器6配置寄存器
    unsigned long *TIM6_COMP;    // 1C8h ~ 1CFh 定时器6对比寄存器
    unsigned long *TIM7_CONF;    // 1E0h ~ 1E7h 定时器7配置寄存器
    unsigned long *TIM7_COMP;    // 1E8h ~ 1EFh 定时器7对比寄存器
} HPET_Registers;

HPET_Registers hpetRegisters = {0};

void hpetInit(UINT8 bspFlags);

#endif