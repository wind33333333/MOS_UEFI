#include "hpet.h"
#include "printk.h"
#include "memory.h"

hpet_registers_t hpetRegisters;
hpet_t hpet;

void init_hpet(UINT8 bsp_flags) {

    if (bsp_flags) {

        hpetRegisters.gcap_id = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0);
        hpetRegisters.gen_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x10);
        hpetRegisters.gintr_sta = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x20);
        hpetRegisters.main_cnt = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0xF0);
        hpetRegisters.tim0_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x100);
        hpetRegisters.tim0_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x108);
        hpetRegisters.tim1_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x120);
        hpetRegisters.tim1_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x128);
        hpetRegisters.tim2_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x140);
        hpetRegisters.tim2_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x148);
        hpetRegisters.tim3_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x160);
        hpetRegisters.tim3_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x168);
        hpetRegisters.tim4_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x180);
        hpetRegisters.tim4_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x188);
        hpetRegisters.tim5_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1A0);
        hpetRegisters.tim5_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1A8);
        hpetRegisters.tim6_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1C0);
        hpetRegisters.tim6_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1C8);
        hpetRegisters.tim7_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1E0);
        hpetRegisters.tim7_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1E8);

        *hpetRegisters.gen_conf = 1;
        io_mfence();

        *hpetRegisters.main_cnt = 0;
        io_mfence();

        hpet.time_number = (*hpetRegisters.gcap_id >> 8 & 0x1F)+1;
        hpet.frequency = *hpetRegisters.gcap_id >> 32;
        color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dhz  TimerNum: %d \n",hpet.frequency,hpet.time_number);


    }
    return;
}