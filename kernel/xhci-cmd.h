#pragma once
#include "moslib.h"

struct xhci_hcd_t;
struct xhci_input_ctrl_ctx_t;
struct xhci_submit_ring_t;

int32 xhci_cmd_enable_slot(struct xhci_hcd_t *xhcd, uint8 port_num, uint8 *out_slot_id);
int32 xhci_cmd_disable_slot(struct xhci_hcd_t *xhcd, uint8 slot_id);
int32 xhci_cmd_addr_dev(struct xhci_hcd_t *xhcd, uint8 slot_id, struct xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_cfg_ep(struct xhci_hcd_t *xhcd, uint8 slot_id, struct xhci_input_ctrl_ctx_t *in_ctx, uint8 dc);
int32 xhci_cmd_eval_ctx(struct xhci_hcd_t *xhcd, uint8 slot_id, struct xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_stop_ep(struct xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_reset_ep(struct xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_set_tr_deq_ptr(struct xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,struct xhci_submit_ring_t *transfer_ring);
int32 xhci_cmd_reset_dev(struct xhci_hcd_t *xhcd, uint8 slot_id);