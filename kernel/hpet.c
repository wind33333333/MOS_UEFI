#include "hpet.h"

void init_hpet(UINT8 bsp_flags) {

    if (bsp_flags) {

        hpetRegisters.GCAP_ID = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0);
        hpetRegisters.GEN_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x10);
        hpetRegisters.GINTR_STA = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x20);
        hpetRegisters.MAIN_CNT = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0xF0);
        hpetRegisters.TIM0_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x100);
        hpetRegisters.TIM0_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x108);
        hpetRegisters.TIM1_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x120);
        hpetRegisters.TIM1_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x128);
        hpetRegisters.TIM2_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x140);
        hpetRegisters.TIM2_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x148);
        hpetRegisters.TIM3_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x160);
        hpetRegisters.TIM3_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x168);
        hpetRegisters.TIM4_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x180);
        hpetRegisters.TIM4_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x188);
        hpetRegisters.TIM5_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1A0);
        hpetRegisters.TIM5_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1A8);
        hpetRegisters.TIM6_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1C0);
        hpetRegisters.TIM6_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1C8);
        hpetRegisters.TIM7_CONF = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1E0);
        hpetRegisters.TIM7_COMP = (UINT64 *) LADDR_TO_HADDR(hpet_attr.baseaddr + 0x1E8);

        *hpetRegisters.GEN_CONF = 1;
        io_mfence();

        *hpetRegisters.MAIN_CNT = 0;
        io_mfence();

        hpet_attr.timernum = (*hpetRegisters.GCAP_ID >> 8 & 0x1F)+1;
        hpet_attr.frequency = *hpetRegisters.GCAP_ID >> 32;
        color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dhz  TimerNum: %d \n",hpet_attr.frequency,hpet_attr.timernum);


    }
    return;
}