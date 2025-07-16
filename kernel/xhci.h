#include <stdint.h>
#pragma once

#include "moslib.h"

#pragma pack(push,1)
/* 功能寄存器 (偏移量: 0x00 - 0x3F) */
typedef struct {
    uint8_t caplength;          /* 0x00: 功能寄存器长度，指示操作寄存器起始偏移 */
    uint8_t reserved1;          /* 0x01: 保留 */
    uint16_t hciversion;        /* 0x02: 主机控制器接口版本号 */
    uint32_t hcsparams1;        /* 0x04: 结构参数1，定义端口数量、插槽数等 */
    uint32_t hcsparams2;        /* 0x08: 结构参数2 */
    uint32_t hcsparams3;        /* 0x0C: 结构参数3 */
    uint32_t hccparams1;        /* 0x10: 能力参数1，例如64位寻址支持 */
    uint32_t dboff;             /* 0x14: 门铃寄存器数组偏移量 */
    uint32_t rtsoff;            /* 0x18: 运行时寄存器集偏移量 */
    uint32_t hccparams2;        /* 0x1C: 能力参数2 */
    uint8_t reserved2[0x20];    /* 0x20 - 0x3F: 保留 */
} xhci_cap_regs_t;

/* 操作寄存器 (偏移量: CAPLENGTH - CAPLENGTH + N) */
typedef struct {
    uint32_t usbcmd;            /* CAPLENGTH + 0x00: USB命令寄存器，控制主机运行/停止 */
    uint32_t usbsts;            /* CAPLENGTH + 0x04: USB状态寄存器，报告控制器状态 */
    uint32_t pagesize;          /* CAPLENGTH + 0x08: 页面大小 */
    uint8_t reserved1[0x08];    /* CAPLENGTH + 0x0C - 0x13: 保留 */
    uint32_t dnctrl;            /* CAPLENGTH + 0x14: 设备通知控制 */
    uint64_t crcr;              /* CAPLENGTH + 0x18: 命令环控制 */
    uint8_t reserved2[0x10];    /* CAPLENGTH + 0x20 - 0x2F: 保留 */
    uint64_t dcbaap;            /* CAPLENGTH + 0x30: 设备上下文基地址数组指针 */
    uint32_t config;            /* CAPLENGTH + 0x38: 配置最大设备插槽数 */
    uint8_t reserved3[0x3C4];   /* CAPLENGTH + 0x3C - 0x3FF: 保留 */
    /* 端口寄存器 (每个端口，从CAPLENGTH + 0x400开始) */
    struct {
        uint32_t portsc;        /* 端口状态和控制 */
        uint32_t portpmsc;      /* 端口电源管理状态和控制 */
        uint32_t portli;        /* 端口链接信息 */
        uint32_t porthlpmc;     /* 端口硬件LPM控制 */
    } portregs[255];            /* 支持最多255个端口 */
} xhci_op_regs_t;

/* 运行时寄存器 (偏移量: RTSOFF - RTSOFF + M) */
typedef struct {
    uint32_t mfindex;           /* RTSOFF + 0x00: 微帧索引，用于时间同步 */
    uint8_t reserved[0x1C];     /* RTSOFF + 0x04 - 0x1F: 保留 */
    /* 中断器寄存器集 (每个中断器) */
    struct {
        uint32_t iman;          /* 中断器管理 */
        uint32_t imod;          /* 中断器调节 */
        uint32_t erstsz;        /* 事件环段表大小 */
        uint32_t reserved;      /* 保留 */
        uint64_t erstba;        /* 事件环段表基地址 */
        uint64_t erdp;          /* 事件环出队指针 */
    } interrupter[128];         /* 支持最多128个中断器 */
} xhci_runtime_regs_t;

/* 门铃寄存器 (偏移量: DBOFF - DBOFF + K) */
typedef struct {
    uint32_t doorbell[256];     /* DBOFF + n*4: 每个插槽的门铃寄存器 (最多256个插槽) */
} xhci_doorbell_regs_t;

/* xHCI MMIO空间 (从BAR0基地址开始) */
typedef struct {
    xhci_cap_regs_t cap;        /* 功能寄存器 */
    xhci_op_regs_t op;          /* 操作寄存器 (偏移量取决于CAPLENGTH) */
    /* 运行时寄存器和门铃寄存器通过DBOFF和RTSOFF偏移量访问 */
} xhci_mmio_t;

#pragma pack(pop)

void init_xhci(void);
