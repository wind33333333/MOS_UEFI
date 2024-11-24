#include "hpet.h"
#include "printk.h"
#include "memory.h"

hpet_registers_t hpet_registers;
hpet_t hpet;

void init_hpet(void) {
    hpet_registers.gcap_id = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0);
    hpet_registers.gen_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x10);
    hpet_registers.gintr_sta = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x20);
    hpet_registers.main_cnt = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0xF0);
    hpet_registers.tim0_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x100);
    hpet_registers.tim0_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x108);
    hpet_registers.tim1_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x120);
    hpet_registers.tim1_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x128);
    hpet_registers.tim2_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x140);
    hpet_registers.tim2_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x148);
    hpet_registers.tim3_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x160);
    hpet_registers.tim3_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x168);
    hpet_registers.tim4_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x180);
    hpet_registers.tim4_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x188);
    hpet_registers.tim5_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1A0);
    hpet_registers.tim5_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1A8);
    hpet_registers.tim6_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1C0);
    hpet_registers.tim6_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1C8);
    hpet_registers.tim7_conf = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1E0);
    hpet_registers.tim7_comp = (UINT64 *) LADDR_TO_HADDR(hpet.address + 0x1E8);

    *hpet_registers.gen_conf = 1; //启用hpet
    *hpet_registers.main_cnt = 0;
    hpet.time_number = (*hpet_registers.gcap_id >> 8 & 0x1F)+1;
    hpet.frequency = *hpet_registers.gcap_id >> 32;
    color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dhz  TimerNum: %d \n",hpet.frequency,hpet.time_number);
    return;
}