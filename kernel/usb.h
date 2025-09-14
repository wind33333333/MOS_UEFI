#pragma once
#include "xhci.h"

//USB设备
typedef struct {
    UINT32 port_id;
    UINT32 slot_id;
    xhci_ring_t     ep0_trans_ring;               //端点0传输环虚拟地址 63-1位:为地址 0位:C
    list_head_t list;
}usb_dev_t;

#pragma pack(pop)