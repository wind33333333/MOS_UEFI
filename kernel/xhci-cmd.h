#pragma once
#include "moslib.h"
#include "xhci.h"

int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 port_num, uint8 *out_slot_id);
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id);
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx, uint8 dc);
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,xhci_submit_ring_t *transfer_ring);
int32 xhci_cmd_reset_dev(xhci_hcd_t *xhcd, uint8 slot_id);