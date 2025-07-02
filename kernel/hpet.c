#include "hpet.h"
#include "printk.h"
#include "vmm.h"
#include "acpi.h"

hpet_registers_t hpet_registers;
hpet_t hpet1;

void init_hpet(void) {
    //hpet初始化
    hpett_t *hpett = acpi_get_table('TEPH');
    hpet1.address = (UINT64) pa_to_va(hpett->acpi_generic_adderss.address);
    color_printk(
        GREEN,BLACK,
        "HPET MiniMumTick:%d Number:%d SpaceID:%d BitWidth:%d BiteOffset:%d AccessSize:%d Address:%#lX\n",
        hpett->minimum_tick, hpett->hpet_number, hpett->acpi_generic_adderss.space_id,
        hpett->acpi_generic_adderss.bit_width, hpett->acpi_generic_adderss.bit_offset,
        hpett->acpi_generic_adderss.access_size, hpett->acpi_generic_adderss.address);

    hpet_registers.gcap_id = (UINT64 *) pa_to_va(hpet1.address + 0);
    hpet_registers.gen_conf = (UINT64 *) pa_to_va(hpet1.address + 0x10);
    hpet_registers.gintr_sta = (UINT64 *) pa_to_va(hpet1.address + 0x20);
    hpet_registers.main_cnt = (UINT64 *) pa_to_va(hpet1.address + 0xF0);
    hpet_registers.tim0_conf = (UINT64 *) pa_to_va(hpet1.address + 0x100);
    hpet_registers.tim0_comp = (UINT64 *) pa_to_va(hpet1.address + 0x108);
    hpet_registers.tim1_conf = (UINT64 *) pa_to_va(hpet1.address + 0x120);
    hpet_registers.tim1_comp = (UINT64 *) pa_to_va(hpet1.address + 0x128);
    hpet_registers.tim2_conf = (UINT64 *) pa_to_va(hpet1.address + 0x140);
    hpet_registers.tim2_comp = (UINT64 *) pa_to_va(hpet1.address + 0x148);
    hpet_registers.tim3_conf = (UINT64 *) pa_to_va(hpet1.address + 0x160);
    hpet_registers.tim3_comp = (UINT64 *) pa_to_va(hpet1.address + 0x168);
    hpet_registers.tim4_conf = (UINT64 *) pa_to_va(hpet1.address + 0x180);
    hpet_registers.tim4_comp = (UINT64 *) pa_to_va(hpet1.address + 0x188);
    hpet_registers.tim5_conf = (UINT64 *) pa_to_va(hpet1.address + 0x1A0);
    hpet_registers.tim5_comp = (UINT64 *) pa_to_va(hpet1.address + 0x1A8);
    hpet_registers.tim6_conf = (UINT64 *) pa_to_va(hpet1.address + 0x1C0);
    hpet_registers.tim6_comp = (UINT64 *) pa_to_va(hpet1.address + 0x1C8);
    hpet_registers.tim7_conf = (UINT64 *) pa_to_va(hpet1.address + 0x1E0);
    hpet_registers.tim7_comp = (UINT64 *) pa_to_va(hpet1.address + 0x1E8);

    *hpet_registers.gen_conf = 1; //启用hpet
    *hpet_registers.main_cnt = 0;
    hpet1.time_number = (*hpet_registers.gcap_id >> 8 & 0x1F)+1;
    hpet1.frequency = *hpet_registers.gcap_id >> 32;
    color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dhz  TimerNum: %d \n",hpet1.frequency,hpet1.time_number);
    return;
}