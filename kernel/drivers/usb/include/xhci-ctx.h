#pragma once
#include "moslib.h"



struct usb_dev_t;
struct usb_if_alt_t;

int32 xhci_ctx_slot_cfg(struct usb_dev_t *udev);
int32 xhci_ctx_slot_ep0_eval(struct usb_dev_t *udev);
int32 xhci_ctx_eps_cfg(struct usb_if_alt_t *drop_uif_alt,struct usb_if_alt_t *add_uif_alt);
int32 xhci_ctx_deconfigure_all(struct usb_dev_t *udev );
int32 xhci_enable_slot_ep0(struct usb_dev_t *udev);