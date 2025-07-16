#pragma once

#include "moslib.h"

void init_ioapic(void);

typedef struct{
    UINT8 *ioregsel;    //索引寄存器 8位
    UINT32 *iowin;      //数据寄存器 32位
    UINT32 *eoi;        //中断结束寄存器   32位
}ioapic_address_t;

extern ioapic_address_t ioapic_address;

#define IO_APIC_ID            0x0     //32位 读写 io_apic_id寄存器
#define IO_APIC_VER           0x1     //32位 只读 io_apic版本寄存器
#define IO_APIC_TBL0_LOW32    0x10    //中断定向投递寄存器0，低 32 位，读写
#define IO_APIC_TBL0_HIGH32   0x11    //中断定向投递寄存器0，高 32 位，读写
#define IO_APIC_TBL1_LOW32    0x12    //中断定向投递寄存器1，低 32 位，读写
#define IO_APIC_TBL1_HIGH32   0x13    //中断定向投递寄存器1，高 32 位，读写
#define IO_APIC_TBL2_LOW32    0x14    //中断定向投递寄存器2，低 32 位，读写
#define IO_APIC_TBL2_HIGH32   0x15    //中断定向投递寄存器2，高 32 位，读写
#define IO_APIC_TBL3_LOW32    0x16    //中断定向投递寄存器3，低 32 位，读写
#define IO_APIC_TBL3_HIGH32   0x17    //中断定向投递寄存器3，高 32 位，读写
#define IO_APIC_TBL4_LOW32    0x18    //中断定向投递寄存器4，低 32 位，读写
#define IO_APIC_TBL4_HIGH32   0x19    //中断定向投递寄存器4，高 32 位，读写
#define IO_APIC_TBL5_LOW32    0x1A    //中断定向投递寄存器5，低 32 位，读写
#define IO_APIC_TBL5_HIGH32   0x1B    //中断定向投递寄存器5，高 32 位，读写
#define IO_APIC_TBL6_LOW32    0x1C    //中断定向投递寄存器6，低 32 位，读写
#define IO_APIC_TBL6_HIGH32   0x1D    //中断定向投递寄存器6，高 32 位，读写
#define IO_APIC_TBL7_LOW32    0x1E    //中断定向投递寄存器7，低 32 位，读写
#define IO_APIC_TBL7_HIGH32   0x1F    //中断定向投递寄存器7，高 32 位，读写
#define IO_APIC_TBL8_LOW32    0x20    //中断定向投递寄存器8，低 32 位，读写
#define IO_APIC_TBL8_HIGH32   0x21    //中断定向投递寄存器8，高 32 位，读写
#define IO_APIC_TBL9_LOW32    0x22    //中断定向投递寄存器9，低 32 位，读写
#define IO_APIC_TBL9_HIGH32   0x23    //中断定向投递寄存器9，高 32 位，读写
#define IO_APIC_TBL10_LOW32   0x24    //中断定向投递寄存器10，低 32 位，读写
#define IO_APIC_TBL10_HIGH32  0x25    //中断定向投递寄存器10，高 32 位，读写
#define IO_APIC_TBL11_LOW32   0x26    //中断定向投递寄存器11，低 32 位，读写
#define IO_APIC_TBL11_HIGH32  0x27    //中断定向投递寄存器11，高 32 位，读写
#define IO_APIC_TBL12_LOW32   0x28    //中断定向投递寄存器12，低 32 位，读写
#define IO_APIC_TBL12_HIGH32  0x29    //中断定向投递寄存器12，高 32 位，读写
#define IO_APIC_TBL13_LOW32   0x2A    //中断定向投递寄存器13，低 32 位，读写
#define IO_APIC_TBL13_HIGH32  0x2B    //中断定向投递寄存器13，高 32 位，读写
#define IO_APIC_TBL14_LOW32   0x2C    //中断定向投递寄存器14，低 32 位，读写
#define IO_APIC_TBL14_HIGH32  0x2D    //中断定向投递寄存器14，高 32 位，读写
#define IO_APIC_TBL15_LOW32   0x2E    //中断定向投递寄存器15，低 32 位，读写
#define IO_APIC_TBL15_HIGH32  0x2F    //中断定向投递寄存器15，高 32 位，读写
#define IO_APIC_TBL16_LOW32   0x30    //中断定向投递寄存器16，低 32 位，读写
#define IO_APIC_TBL16_HIGH32  0x31    //中断定向投递寄存器16，高 32 位，读写
#define IO_APIC_TBL17_LOW32   0x32    //中断定向投递寄存器17，低 32 位，读写
#define IO_APIC_TBL17_HIGH32  0x33    //中断定向投递寄存器17，高 32 位，读写
#define IO_APIC_TBL18_LOW32   0x34    //中断定向投递寄存器18，低 32 位，读写
#define IO_APIC_TBL18_HIGH32  0x35    //中断定向投递寄存器18，高 32 位，读写
#define IO_APIC_TBL19_LOW32   0x36    //中断定向投递寄存器19，低 32 位，读写
#define IO_APIC_TBL19_HIGH32  0x37    //中断定向投递寄存器19，高 32 位，读写
#define IO_APIC_TBL20_LOW32   0x38    //中断定向投递寄存器20，低 32 位，读写
#define IO_APIC_TBL20_HIGH32  0x39    //中断定向投递寄存器20，高 32 位，读写
#define IO_APIC_TBL21_LOW32   0x3A    //中断定向投递寄存器21，低 32 位，读写
#define IO_APIC_TBL21_HIGH32  0x3B    //中断定向投递寄存器21，高 32 位，读写
#define IO_APIC_TBL22_LOW32   0x3C    //中断定向投递寄存器22，低 32 位，读写
#define IO_APIC_TBL22_HIGH32  0x3D    //中断定向投递寄存器22，高 32 位，读写
#define IO_APIC_TBL23_LOW32   0x3E    //中断定向投递寄存器23，低 32 位，读写
#define IO_APIC_TBL23_HIGH32  0x3F    //中断定向投递寄存器23，高 32 位，读写

