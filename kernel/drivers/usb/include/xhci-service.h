#pragma once
#include "../../../include/moslib.h"
#include "../../../x64cpu/interrupt.h"

struct xhci_hcd_t;
irqreturn_e xhci_isr(cpu_registers_t *regs,void *dev_id);
void xhci_port_scan(struct xhci_hcd_t *xhcd);
void xhci_process_port_event(struct xhci_hcd_t *xhcd, uint8 port_num);


