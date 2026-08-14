#include "hpet.h"
#include "printk.h"
#include "vmm.h"
#include "acpi.h"

hpet_registers_t hpet_registers;
hpet_t hpet1;

#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL

// 定义 1 秒等于 10^15 飞秒
#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL

// 假设这些宏是你 HPET 的映射基址
#define HPET_CAP_REG   (HPET_BASE_VADDR + 0x00) // 能力寄存器
#define HPET_COUNT_REG (HPET_BASE_VADDR + 0xF0) // 主计数器寄存器

/**
 * @brief 使用 HPET 精确测算 CPU TSC 频率 (极限边缘对齐版)
 * @param wait_ms 期望的校准时长 (建议传 10 毫秒)
 * @return TSC 频率 (Hz)
 */
uint64 calibrate_tsc_via_hpet_formula(hpet_registers_t *hpet_registers, hpet_t *hpet, uint32 wait_ms) {
    uint64 target_hpet_delta = (hpet->frequency * wait_ms) / 1000;

    // =================================================================
    // 0. 预热 (Warm-up)
    // =================================================================
    // 把读取指令和 MMIO 内存页加载到 CPU L1 Cache 和 TLB 中，
    // 防止起跑时的 Cache Miss 导致几百纳秒的延迟干扰。
    volatile uint64 dummy_hpet = *hpet_registers->main_cnt;
    volatile uint64 dummy_tsc  = asm_rdtsc_start();
    (void)dummy_hpet; (void)dummy_tsc;

    // =================================================================
    // 1. 捕捉起跑线“边缘” (Start Edge Alignment)
    // =================================================================
    uint64 hpet_prev = *hpet_registers->main_cnt;
    uint64 hpet_start;
    uint64 tsc_start;

    // 疯狂轮询，直到 HPET 发生跳变的那一瞬间！
    while ((hpet_start = *hpet_registers->main_cnt) == hpet_prev) {
        asm_pause();
    }
    // 🌟 HPET 刚刚跳变！现在的物理时间精确卡在 HPET 的边界上！
    // 瞬间锁死 TSC！
    tsc_start = asm_rdtsc_start();


    // =================================================================
    // 2. 粗略等待大段时间流逝 (Bulk Wait)
    // =================================================================
    uint64 hpet_target = hpet_start + target_hpet_delta;
    uint64 hpet_current;

    while ((hpet_current = *hpet_registers->main_cnt) < hpet_target) {
        asm_pause();
    }


    // =================================================================
    // 3. 捕捉终点线“边缘” (End Edge Alignment)
    // =================================================================
    // 注意：上面的循环跳出时，hpet_current 可能卡在某个阶梯的中间。
    // 为了彻底消除误差，我们再等它翻转一次，抓取下一个完美的边界！
    hpet_prev = hpet_current;
    uint64 hpet_end;
    uint64 tsc_end;

    while ((hpet_end = *hpet_registers->main_cnt) == hpet_prev) {
        asm_pause();
    }
    // 🌟 HPET 再次跳变！瞬间锁死终点 TSC！
    tsc_end = asm_rdtsc_end(0);


    // =================================================================
    // 4. 数学计算
    // =================================================================
    uint64 delta_tsc  = tsc_end - tsc_start;
    uint64 delta_hpet = hpet_end - hpet_start;

    // TSC_Hz = (Delta_TSC * HPET_Hz) / Delta_HPET
    uint64 tsc_hz = (delta_tsc * hpet->frequency) / delta_hpet;

    return tsc_hz;
}

void init_hpet(void) {
    //hpet初始化
    hpett_t *hpett = acpi_get_table('TEPH');
    hpet1.address = (uint64) pa_to_va(hpett->acpi_generic_adderss.address);
    color_printk(
        GREEN,BLACK,
        "HPET MiniMumTick:%d Number:%d SpaceID:%d BitWidth:%d BiteOffset:%d AccessSize:%d Address:%#lX\n",
        hpett->minimum_tick, hpett->hpet_number, hpett->acpi_generic_adderss.space_id,
        hpett->acpi_generic_adderss.bit_width, hpett->acpi_generic_adderss.bit_offset,
        hpett->acpi_generic_adderss.access_size, hpett->acpi_generic_adderss.address);

    hpet_registers.gcap_id = (uint64 *) pa_to_va(hpet1.address + 0);
    hpet_registers.gen_conf = (uint64 *) pa_to_va(hpet1.address + 0x10);
    hpet_registers.gintr_sta = (uint64 *) pa_to_va(hpet1.address + 0x20);
    hpet_registers.main_cnt = (uint64 *) pa_to_va(hpet1.address + 0xF0);
    hpet_registers.tim0_conf = (uint64 *) pa_to_va(hpet1.address + 0x100);
    hpet_registers.tim0_comp = (uint64 *) pa_to_va(hpet1.address + 0x108);
    hpet_registers.tim1_conf = (uint64 *) pa_to_va(hpet1.address + 0x120);
    hpet_registers.tim1_comp = (uint64 *) pa_to_va(hpet1.address + 0x128);
    hpet_registers.tim2_conf = (uint64 *) pa_to_va(hpet1.address + 0x140);
    hpet_registers.tim2_comp = (uint64 *) pa_to_va(hpet1.address + 0x148);
    hpet_registers.tim3_conf = (uint64 *) pa_to_va(hpet1.address + 0x160);
    hpet_registers.tim3_comp = (uint64 *) pa_to_va(hpet1.address + 0x168);
    hpet_registers.tim4_conf = (uint64 *) pa_to_va(hpet1.address + 0x180);
    hpet_registers.tim4_comp = (uint64 *) pa_to_va(hpet1.address + 0x188);
    hpet_registers.tim5_conf = (uint64 *) pa_to_va(hpet1.address + 0x1A0);
    hpet_registers.tim5_comp = (uint64 *) pa_to_va(hpet1.address + 0x1A8);
    hpet_registers.tim6_conf = (uint64 *) pa_to_va(hpet1.address + 0x1C0);
    hpet_registers.tim6_comp = (uint64 *) pa_to_va(hpet1.address + 0x1C8);
    hpet_registers.tim7_conf = (uint64 *) pa_to_va(hpet1.address + 0x1E0);
    hpet_registers.tim7_comp = (uint64 *) pa_to_va(hpet1.address + 0x1E8);

    *hpet_registers.gen_conf = 1; //启用hpet
    *hpet_registers.main_cnt = 0;
    hpet1.time_number = (*hpet_registers.gcap_id >> 8 & 0x1F)+1;
    hpet1.frequency = FEMTOSECONDS_PER_SECOND / (*hpet_registers.gcap_id >> 32);
    color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dhz  TimerNum: %d \n",hpet1.frequency,hpet1.time_number);

    uint64 tsc_hz = calibrate_tsc_via_hpet_formula(&hpet_registers,&hpet1,10);
    color_printk(RED,BLACK,"tscHZ:%d \n",tsc_hz);

    uint64 target_tsc_delta = tsc_hz;
    // 4. 掐表死等：不断轮询 HPET 直到达到目标刻度
    uint64 tsc_current;

    // 3. 🏁 裁判鸣枪：同时读取起跑线数据
    uint64 m = 0;
    uint64 tsc_start = asm_rdtsc_start();
    while (1) {
        tsc_current = asm_rdtsc_end(0);
        if ((tsc_current - tsc_start) >= target_tsc_delta) {
            tsc_start = tsc_current;
            color_printk(RED,BLACK,"%ds ",++m);
        }
        asm_pause(); // 插入 pause 指令，防止 CPU 空转过热和乱序干扰
    }


    /*uint64 target_hpet_delta = hpet1.frequency;
    // 4. 掐表死等：不断轮询 HPET 直到达到目标刻度
    uint64 hpet_current;

    // 3. 🏁 裁判鸣枪：同时读取起跑线数据
    uint64 m = 0;
    uint64 hpet_start = *hpet_registers.main_cnt;
    while (1) {
        hpet_current = *hpet_registers.main_cnt;
        if ((hpet_current - hpet_start) >= target_hpet_delta) {
            hpet_start = hpet_current;
            color_printk(RED,BLACK,"%ds ",++m);
        }
        asm_pause(); // 插入 pause 指令，防止 CPU 空转过热和乱序干扰
    }*/

    return;
}