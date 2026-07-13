#include "usb-storage.h"
#include "usb-core.h"
#include "../include/usb-dev.h"
#include "../include/usb-bus.h"
#include "printk.h"
#include "scsi.h"
#include "usb-uas.h"
#include "usb-bot.h"
#include "slub.h"


extern scsi_host_template_t uas_host_template;
extern scsi_host_template_t bot_host_template;


//u盘驱动程序
int32 usb_storage_probe(usb_if_t *uif,usb_id_t *uid) {

    usb_if_alt_t *uas_if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY,0x62);
    usb_if_alt_t *bot_if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY,0x50);

    scsi_host_t *shost;

    if (uas_if_alt) {        //uas协议初始化流程

        color_printk(GREEN,BLACK,"uas mode  \n");

        // 创建 UAS 协议私有数据
        uas_data_t *uas_data = kzalloc(sizeof(uas_data_t));
        if (!uas_data) return -1;
        uas_data->uif = uif;

        // ==========================================================
        // 1. 解析 Pipe 端点与流能力侦测
        // ==========================================================
        for (uint8 i = 0; i < 4; i++) {
            usb_ep_t *ep = &uas_if_alt->eps[i];
            ep->ring_max_trbs = 256;

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

        uint8 streams_exp = usb_cfg_alt_streams(uas_if_alt,6);
        usb_enable_alt_if(uas_if_alt);

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
        bot_data->sense = kzalloc_dma(SCSI_SENSE_ALLOC_SIZE);
        bot_data->tag = 0;

        for (uint8 i = 0; i < 2; i++) {
            usb_ep_t *ep = &bot_if_alt->eps[i];
            ep->ring_max_trbs = 256;
            if (ep->ep_dci & 1) {
                bot_data->in_ep = ep;
            } else {
                bot_data->out_ep = ep;
            }
        }

        usb_cfg_alt_streams(bot_if_alt,0);
        usb_enable_alt_if(bot_if_alt);


        //uint8 max_lun = bot_get_max_lun(uif->udev,uif->if_num);

        //创建scsi_host
        shost = scsi_create_host(&bot_host_template,bot_data,&uif->dev,1,"bot_host");

    }

    scsi_add_host(shost);
}

void usb_storage_remove(usb_if_t *usb_if) {

}

usb_drv_t *create_usb_storage_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t)*2);
    id_table[0].match_flags = USB_MATCH_INT_CLASS | USB_MATCH_INT_SUBCLASS;
    id_table[0].if_class = 0x8;
    id_table[0].if_subclass = 0x6;
    usb_drv->drv.name = "usb_storage";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_storage_probe;
    usb_drv->remove = usb_storage_remove;
    return usb_drv;
}
