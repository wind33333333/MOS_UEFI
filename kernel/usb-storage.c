#include "usb-storage.h"
#include "xhci.h"
#include "usb-core.h"
#include "printk.h"
#include "scsi.h"
#include "uas.h"
#include "bot.h"


extern scsi_host_template_t uas_host_template;
extern scsi_host_template_t bot_host_template;


//u盘驱动程序
int32 usb_storage_probe(usb_if_t *uif,usb_id_t *id) {

    //u盘是否支持uas协议，优先设置为uas协议
    if (uif->alt_count > 1) {
        usb_if_alt_t *next_alts = uif->alts;
        for (uint8 i = 0; i < uif->alt_count; i++) {
            if (next_alts[i].if_protocol == 0x62) {
                usb_switch_alt_if(&next_alts[i]);
                break;
            }
        }
    }

    scsi_host_t *shost;

    if (uif->cur_alt->if_protocol == 0x62) {        //uas协议初始化流程

        color_printk(GREEN,BLACK,"uas mode  \n");

        // 创建 UAS 协议私有数据
        uas_data_t *uas_data = kzalloc(sizeof(uas_data_t));
        if (!uas_data) return -1;
        uas_data->uif = uif;

        // ==========================================================
        // 1. 解析 Pipe 端点与流能力侦测 (修复无差别误杀 Bug)
        // ==========================================================
        for (uint8 i = 0; i < 4; i++) {
            usb_ep_t *ep = &uif->cur_alt->eps[i];

            usb_uas_pipe_usage_desc_t *pipe_usage_desc = ep->extras_desc;
            if (!pipe_usage_desc) continue;

            // 路由赋值
            switch (pipe_usage_desc->pipe_id) {
                case USB_UAS_PIPE_COMMAND_OUT: uas_data->cmd_ep = ep;      break;
                case USB_UAS_PIPE_STATUS_IN:   uas_data->status_ep = ep;   break;
                case USB_UAS_PIPE_BULK_IN:     uas_data->data_in_ep = ep;  break;
                case USB_UAS_PIPE_BULK_OUT:    uas_data->data_out_ep = ep; break;
            }
        }

        //重新分配流
        usb_ep_t *streams_ep[3] = {
            uas_data->status_ep,
            uas_data->data_in_ep,
            uas_data->data_out_ep,
        };
        uint8 streams_exp = usb_alloc_streams(uif->udev,streams_ep,3,0);
        uint16 streams_pool_size = 0;
        //初始化tag_bitmap
        uas_data->tag_bitmap = 0xFFFFFFFFFFFFFFFFUL; //bit0 对应tag1
        if (streams_exp > 0) {
            uas_data->max_streams = 1<<streams_exp;
            streams_pool_size = uas_data->max_streams;
            uas_data->tag_bitmap <<= uas_data->max_streams; //标记可用流

        }else {
            uas_data->max_streams = 0;
            streams_pool_size = 1;
            uas_data->tag_bitmap <<= 1; //无流情况仅1可以用
        }

        //初始化cmd_iu和sense_iu内存池
        uas_data->cmd_iu_pool = kmalloc(sizeof(uas_cmd_iu_t)*streams_pool_size);
        uas_data->sense_iu_pool = kmalloc(sizeof(uas_sense_iu_t)*streams_pool_size);

        //创建scsi_host
        shost = scsi_create_host(&uas_host_template,uas_data,&uif->dev,0,"uas_host");


    } else {        //bot协议初始化流程
        //创建bot_data
        bot_data_t *bot_data = kzalloc(sizeof(bot_data_t));
        bot_data->uif = uif;
        bot_data->cbw = kzalloc_dma(sizeof(bot_csw_t));
        bot_data->csw = kzalloc_dma(sizeof(bot_csw_t));
        bot_data->tag = 0;

        for (uint8 i = 0; i < 2; i++) {
            usb_ep_t *ep = &uif->cur_alt->eps[i];
            if (ep->ep_dci & 1) {
                bot_data->in_ep = ep;
            } else {
                bot_data->out_ep = ep;
            }
        }

        uint8 max_lun = usb_get_bot_max_lun(uif->udev,uif->if_num);

        //创建scsi_host
        shost = scsi_create_host(&bot_host_template,bot_data,&uif->dev,max_lun,"bot_host");

    }

    scsi_add_host(shost);
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
