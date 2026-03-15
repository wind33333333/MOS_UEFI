#include "usb-storage.h"
#include "xhci.h"
#include "usb.h"
#include "printk.h"
#include "scsi.h"
#include "uas.h"
#include "bot.h"

//测试逻辑单元是否有效
/*

//获取最大逻辑单元
static inline uint8 bot_msc_read_max_lun(xhcd_t *xhcd, usb_dev_t *usb_dev,
                                         usb_bot_msc_t *bot_msc) {
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_interface, setup_stage_calss, setup_stage_in, usb_req_get_max_lun, 0, 0,
                    bot_msc->interface_num,in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    uint8 *max_lun = kzalloc(64);
    data_stage_trb(&trb, va_to_pa(max_lun), 1, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhcd, &trb);

    uint8 lun_count = ++*max_lun;
    kfree(max_lun);
    return lun_count;
}
*/

extern scsi_host_template_t uas_host_template;
extern scsi_host_template_t bot_host_template;

//初始化端点
int usb_endpoint_init(usb_if_alt_t *if_alt) {
    usb_dev_t *udev = if_alt->usb_if->udev;
    xhci_hcd_t *xhcd = udev->xhcd;

    //配置端点
    uint8 max_ep_num = 0;
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep = &if_alt->eps[i];
        uint8 ep_dci = ep->ep_dci;

        usb_tx_begin(udev);

        //指针放到udev.eps中
        udev->eps[ep_dci] = ep;

        uint64 tr_dequeue_ptr = 0;
        uint32 max_streams = ep->max_streams > MAX_STREAMS ? MAX_STREAMS : ep->max_streams;
        if (max_streams) {
            // 有流：分配Stream Context Array和per-stream rings
            uint32 streams_count = 1 << max_streams;
            uint32 streams_ctx_array_count = 1 << (max_streams + 1);
            xhci_stream_ctx_t *stream_ctx_array = kzalloc(streams_ctx_array_count * sizeof(xhci_stream_ctx_t));
            xhci_ring_t *stream_rings = kzalloc(streams_ctx_array_count * sizeof(xhci_ring_t)); //streams0 保留内存需要对齐;
            ep->stream_rings = stream_rings;
            ep->streams_count = streams_count;
            ep->lsa = 1;
            ep->hid = 1;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID从1开始
                xhci_ring_init(&stream_rings[s]);
                stream_ctx_array[s].tr_dequeue = va_to_pa(stream_rings[s].ring_base) | 1 | 1 << 1;
                stream_ctx_array[s].reserved = 0;
            }
            // Stream ID 0保留，通常设为0或无效
            stream_ctx_array[0].tr_dequeue = 0;
            stream_ctx_array[0].reserved = 0;
            tr_dequeue_ptr = va_to_pa(stream_ctx_array);
        } else {
            // 无流：单个Transfer Ring
            xhci_ring_init(&ep->transfer_ring);
            tr_dequeue_ptr = va_to_pa(ep->transfer_ring.ring_base) | 1; // DCS=1
        }

        ep->ep_type = ep->ep_type;
        ep->cerr = 3;
        ep->max_streams = max_streams;
        ep->trq_phys_addr = tr_dequeue_ptr;

        usb_prep_add_ep(udev,ep);
    }

    usb_commit_tx(udev,USB_TX_CMD_CFG_EP);

    return 0;
}

//u盘驱动程序
int32 usb_storage_probe(usb_if_t *uif,usb_id_t *id) {
    usb_dev_t *udev = uif->udev;

    color_printk(GREEN,BLACK,"usb_dev m:%s p:%s s:%s slot_id:%d port_id:%d port_speed:%d  \n",udev->manufacturer,udev->product,udev->serial_number,udev->slot_id,udev->port_id,udev->port_speed);
    color_printk(GREEN,BLACK,"usb_if if_num:%d alt_count:%d \n",uif->if_num,uif->alt_count);

    //u盘是否支持uas协议，优先设置为uas协议
    usb_if_alt_t *alts = uif->alts;
    for (uint8 i = 0; i < uif->alt_count; i++) {
        if (alts[i].if_protocol == 0x62) uif->cur_alt = &alts[i];
    }

    scsi_host_t *shost;

    if (uif->cur_alt->if_protocol == 0x62) {        //uas协议初始化流程


        //创建uas协议似有数据
        uas_data_t *uas_data = kzalloc(sizeof(uas_data_t));
        uas_data->usb_if = uif;

        uint32 mini_streams = 1<<MAX_STREAMS;
        //解析pipe端点
        for (uint8 i = 0; i < 4; i++) {
            usb_ep_t *ep = &uif->cur_alt->eps[i];
            uint8 ep_dci = ep->ep_dci;
            uint32 streams = ep->streams_count;
            if (streams && streams < mini_streams) mini_streams = streams;
            usb_uas_pipe_usage_desc_t *pipe_usage_desc = ep->extras_desc;
            switch (pipe_usage_desc->pipe_id) {
                case USB_UAS_PIPE_COMMAND_OUT:
                    uas_data->cmd_pipe = ep_dci ;     //命令pipe
                    break;
                case USB_UAS_PIPE_STATUS_IN:
                    uas_data->status_pipe = ep_dci ; //状态pipe
                    break;
                case USB_UAS_PIPE_BULK_IN:
                    uas_data->data_in_pipe = ep_dci ; //接收数据pipe
                    break;
                case USB_UAS_PIPE_BULK_OUT:
                    uas_data->data_out_pipe = ep_dci ; //发送数据pipe
            }
        }

        //初始化tag_bitmap
        uas_data->tag_bitmap = 0xFFFFFFFFFFFFFFFFUL;
        uas_data->tag_bitmap <<= (mini_streams-1);
        uas_data->tag_bitmap <<= 1;

        //创建scsi_host
        //shost = scsi_create_host(&uas_host_template,uas_data,&usb_if->dev,0,"uas_host");

    } else {        //bot协议初始化流程
        //创建bot_data
        bot_data_t *bot_data = kzalloc(sizeof(bot_data_t));
        bot_data->usb_if = uif;
        for (uint8 i = 0; i < 2; i++) {
            usb_ep_t *ep = &uif->cur_alt->eps[i];
            uint8 ep_dci = ep->ep_dci;
            if (ep_dci & 1) {
                bot_data->pipe_in = ep_dci;
            } else {
                bot_data->pipe_out = ep_dci;
            }
        }

        //创建scsi_host
        //shost = scsi_create_host(&bot_host_template,bot_data,&usb_if->dev,99,"bot_host");

    }

    //scsi_add_host(shost);
}

void usb_storage_remove(usb_if_t *usb_if) {

}

usb_drv_t *create_us_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t)*3);
    id_table[0].if_class = 0x8;
    id_table[0].if_subclass = 0x6;
    id_table[0].if_protocol = 0x50;
    id_table[1].if_class = 0x8;
    id_table[1].if_subclass = 0x6;
    id_table[1].if_protocol = 0x62;
    usb_drv->drv.name = "usb_storage";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_storage_probe;
    usb_drv->remove = usb_storage_remove;
    return usb_drv;
}
