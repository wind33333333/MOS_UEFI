#pragma once

#include "moslib.h"


#define ENABLE_HPET_TIMES(TIMS_CONF,TIMS_COMP,TIME,MODEL,IRQ) \
        do {   \
           (TIMS_CONF) = (((IRQ) << 9) | (1UL << 6) | ((MODEL) << 3) | (1UL << 2)); \
           MFENCE();                           \
           (TIMS_COMP) = (TIME);                                                  \
           MFENCE();                                 \
         }while(0)

#define DISABLE_HPET_TIMES(TIMS_CONF) \
        do {                 \
           (TIMS_CONF) = 0;           \
           MFENCE();\
        }while(0)

#define HPET_ONESHOT 0
#define HPET_PERIODIC 1

typedef struct {
    UINT64 address;
    UINT32 frequency;
    UINT32 time_number;
}hpet_t;

typedef struct {
    UINT64 *gcap_id;      // 000h ~ 007h 整体机能寄存器
    UINT64 *gen_conf;     // 010h ~ 017h 整体配置寄存器
    UINT64 *gintr_sta;    // 020h ~ 027h 整体中断转态寄存器
    UINT64 *main_cnt;     // 028h ~ 02Fh 主计数器
    UINT64 *tim0_conf;    // 100h ~ 107h 定时器0配置寄存器
    UINT64 *tim0_comp;    // 108h ~ 10Fh 定时器0对比寄存器
    UINT64 *tim1_conf;    // 120h ~ 127h 定时器1配置寄存器
    UINT64 *tim1_comp;    // 128h ~ 12Fh 定时器1对比寄存器
    UINT64 *tim2_conf;    // 140h ~ 147h 定时器2配置寄存器
    UINT64 *tim2_comp;    // 148h ~ 14Fh 定时器2对比寄存器
    UINT64 *tim3_conf;    // 160h ~ 167h 定时器3配置寄存器
    UINT64 *tim3_comp;    // 168h ~ 16Fh 定时器3对比寄存器
    UINT64 *tim4_conf;    // 180h ~ 187h 定时器4配置寄存器
    UINT64 *tim4_comp;    // 188h ~ 18Fh 定时器4对比寄存器
    UINT64 *tim5_conf;    // 1A0h ~ 1A7h 定时器5配置寄存器
    UINT64 *tim5_comp;    // 1A8h ~ 1AFh 定时器5对比寄存器
    UINT64 *tim6_conf;    // 1C0h ~ 1C7h 定时器6配置寄存器
    UINT64 *tim6_comp;    // 1C8h ~ 1CFh 定时器6对比寄存器
    UINT64 *tim7_conf;    // 1E0h ~ 1E7h 定时器7配置寄存器
    UINT64 *tim7_comp;    // 1E8h ~ 1EFh 定时器7对比寄存器
} hpet_registers_t;


extern hpet_registers_t hpetRegisters;
extern hpet_t hpet1;

void init_hpet(void);
