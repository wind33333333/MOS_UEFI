#include "scsi.h"
#include "slub.h"
#include "printk.h"

// 统一的 SCSI 任务执行器和错误处理逻辑
int32 scsi_execute(scsi_cmnd_t *scmnd) {
    scsi_host_t *shost = scmnd->sdev->host;
    int retry_count = 3;
    do {
        // 调用底层绑定的真实发送函数 (BOT/UAS)
        shost->hostt->queue_command(shost,scmnd);

        // 统一执行状态处理逻辑
        if (scmnd->status == 0) {
            // 如果成功，直接返回
            break;
        }else if(scmnd->status == 2 && scmnd->sense->flags_key == 0x06 && scmnd->sense->asc == 0x29) {
            // 这Unit Attention (设备刚上电/复位) 是良性错误，静默重试
            retry_count--;
        }else{
            //其他错误处理
            color_printk(RED,BLACK,"send_cmd_sync error status:%#x  flags_key:%#x  asc:%#x  \n",scmnd->status,scmnd->sense->flags_key,scmnd->sense->asc);
            while (1);
        }

    } while (retry_count > 0);

    return scmnd->status;
}


/**
 * 发送 TEST UNIT READY 命令
 */
int32 scsi_test_unit_ready(scsi_device_t *sdev) {
    //scsi_sense信息
    scsi_sense_t sense;

    //构造cdb
    scsi_cdb_test_unit_t cdb = {
        .opcode = SCSI_TEST_UNIT_READY,
    };

    //构造scsi_scmnd
    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_test_unit_t),
        .dir = SCSI_DIR_NONE,
        .data_buf = NULL,
        .data_len = 0,
        .sense = &sense,
        .status = -1
    };

    //发送scsi命令
    scsi_execute(&scmnd);

    return scmnd.status;
}

//获取scsi命令错误信息
int32 scsi_request_sense(scsi_device_t *sdev,scsi_sense_t *sense) {
    scsi_cdb_request_sense_t cdb = {
        .opcode = SCSI_REQUEST_SENSE,
        .alloc_len = SCSI_SENSE_ALLOC_SIZE
    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_request_sense_t),
        .dir = SCSI_DIR_IN,
        .data_buf = sense,
        .data_len = SCSI_SENSE_ALLOC_SIZE,
        .sense = NULL,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}


//获取u盘信息
int32 scsi_send_inquiry(scsi_device_t *sdev, scsi_inquiry_t *inquiry) {
    scsi_sense_t sense;

    scsi_cdb_inquiry_t cdb = {
        .opcode = SCSI_INQUIRY,
        .alloc_len = sizeof(scsi_inquiry_t)
    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_inquiry_t),
        .data_buf = inquiry,
        .data_len = sizeof(scsi_inquiry_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

/*
 * 获取 LUN 数量
*/
int32 scsi_report_luns(scsi_device_t *sdev,scsi_report_luns_t *report_luns) {
    scsi_sense_t sense;

    scsi_cdb_report_luns_t cdb={
        .opcode = SCSI_REPORT_LUNS,
        .alloc_len =  asm_bswap32(SCSI_LUN_BUF_LEN),
    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_report_luns_t),
        .data_buf = report_luns,
        .data_len = SCSI_LUN_BUF_LEN,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity10(scsi_device_t *sdev,scsi_read_capacity10_t *read_capacity10) {
    scsi_sense_t sense;

    scsi_cdb_read_capacity10_t cdb = {
        .opcode = SCSI_READ_CAPACITY10,
    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_read_capacity10_t),
        .data_buf = read_capacity10,
        .data_len = sizeof(scsi_read_capacity10_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity16(scsi_device_t *sdev,scsi_read_capacity16_t *read_capacity16) {
    scsi_sense_t sense;

    scsi_cdb_read_capacity16_t cdb = {
        .opcode = SCSI_READ_CAPACITY16,
        .service_action = SA_READ_CAPACITY_16,
        .lba = 0,
        .alloc_len = asm_bswap32(sizeof(scsi_read_capacity16_t))

    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_read_capacity16_t),
        .data_buf = read_capacity16,
        .data_len = sizeof(scsi_read_capacity16_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

//scsi读扇区10
int32 scsi_read10(scsi_device_t *sdev,void *data_buf,uint32 lba,uint16 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_READ10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .data_buf = data_buf,
        .data_len = block_count*sdev->block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}


//scsi写扇区10
int32 scsi_write10(scsi_device_t *sdev,void *data_buf,uint32 lba,uint16 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_WRITE10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .data_buf = data_buf,
        .data_len = block_count*sdev->block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

//scsi读扇区16
int32 scsi_read16(scsi_device_t *sdev,void *data_buf,uint64 lba,uint32 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_READ16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .data_buf = data_buf,
        .data_len = block_count * sdev->block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

//scsi写扇区16
int32 scsi_write16(scsi_device_t *sdev,void *data_buf,uint64 lba,uint32 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_WRITE16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_cmnd_t scmnd={
        .sdev = sdev,
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .data_buf = data_buf,
        .data_len = block_count*sdev->block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(&scmnd);

    return scmnd.status;
}

// 定义设备类型，用于在统一设备树中区分身份
device_type_t scsi_host_type = {"scsi-host"};
device_type_t scsi_dev_type = {"scsi-dev"};

extern bus_type_t scsi_bus_type;

//创建scsi_host
scsi_host_t *scsi_create_host(scsi_host_template_t *host_template,void* host_data,device_t *parent,uint8 max_lun,char *name) {
    scsi_host_t *shost = kzalloc(sizeof(scsi_host_t));
    shost->dev.name = name;
    shost->dev.bus = 0;
    shost->dev.bus_node.next = NULL;
    shost->dev.bus_node.prev = NULL;
    shost->dev.child_list.next = NULL;
    shost->dev.child_list.prev = NULL;
    shost->dev.child_node.next = NULL;
    shost->dev.child_node.prev = NULL;
    shost->dev.drv = NULL;
    shost->dev.drv_data = NULL;
    shost->dev.parent = parent;
    shost->dev.type = NULL;

    shost->hostt = host_template;
    shost->hostdata = host_data;
    shost->host_no = 0;
    shost->max_lun = max_lun;
    shost->host_status = 0;
    list_head_init(&shost->devices_list);
    return shost;
}


// 工具函数：清理 INQUIRY 返回字符串尾部的空格 (0x20)
static void scsi_trim_string(char *str, int len) {
    int i = len - 1;
    while (i >= 0 && (str[i] == ' ' || str[i] == 0)) {
        str[i] = '\0';
        i--;
    }
}

// ============================================================================
// 步骤 2: 探测具体的 LUN (探针)
// ============================================================================
static void scsi_probe_lun(scsi_host_t *shost) {
    // 1. 预分配 LUN 句柄
    scsi_device_t *sdev = kzalloc(sizeof(scsi_device_t));
    sdev->host = shost;
    sdev->lun  = 0;     //lun0 所有scsi设备都必须存在

    /*//探测lun
    scsi_report_luns_t *report_luns = kzalloc(sizeof(scsi_report_luns_t));
    int32 status = scsi_report_luns(sdev,report_luns);
    if (status == 0 && report_luns->lun_list_length) {
        shost->max_lun = asm_bswap32(report_luns->lun_list_length);
    }
    kfree(report_luns);
    color_printk(GREEN,BLACK,"%s max lun:%d    \n",shost->dev.name,shost->max_lun);*/

    // 2. 发送 INQUIRY 命令查身份
    scsi_inquiry_t *inq = kzalloc(sizeof(scsi_inquiry_t));
    int32 status = scsi_send_inquiry(sdev, inq);
    if (status != 0) {
        kfree(inq);
        kfree(sdev); // 没有设备响应，说明这个 LUN 不存在
        return;
    }

    // 检查 Peripheral Qualifier (高 3 位)
    // 0x03 (011b) 表示：设备支持这个 LUN 地址，但当前没有物理介质连接 (如读卡器空槽)
    if ((inq->device_type >> 5) == 0x03) {
        kfree(inq);
        kfree(sdev);
        return;
    }

    // 3. 解析身份信息
    sdev->type = inq->device_type & 0x1F;
    sdev->removable = inq->rmb>>7;

    // 拷贝并清理字符串
    asm_mem_cpy(inq->vendor_id,sdev->vendor, 8);
    sdev->vendor[8] = '\0';
    scsi_trim_string(sdev->vendor, 8);

    asm_mem_cpy(inq->product_id,sdev->model,  16);
    sdev->model[16] = '\0';
    scsi_trim_string(sdev->model, 16);

    asm_mem_cpy(inq->revision,sdev->rev,  4);
    sdev->rev[4] = '\0';
    scsi_trim_string(sdev->rev, 4);

    kfree(inq);

    color_printk(GREEN,BLACK,"vendor:%s model:%s rev:%s   \n",sdev->vendor,sdev->model,sdev->rev);

    // 4. 发送 TEST UNIT READY (消费掉刚上电的 Unit Attention)
    scsi_test_unit_ready(sdev);

    /*// 5. 如果是磁盘设备 (Type 0)，尝试获取容量
    if (sdev->type == 0x00) {
        scsi_read_capacity10_t cap = {0};
        // 注意：这里哪怕没获取到容量(如读卡器没插卡)，也要把 sdev 留着，只是标记 is_ready = 0
        if (scsi_read_capacity10(sdev, &cap) == 0) {
            sdev->block_size = asm_bswap32(cap.block_size);
            sdev->max_lba    = asm_bswap32(cap.max_lba);
            sdev->is_ready   = 1;
        }
    }

    // 6. 组装设备树，将其挂载到 SCSI 总线上！
    sdev->dev.parent = &shost->dev;         // 认 Host 为父
    sdev->dev.bus    = &scsi_bus_type;      // 挂载到 SCSI 总线
    sdev->dev.type   = &scsi_dev_type;     // 身份标记为 scsi_device

    // 挂入 Host 的设备链表中，方便统一管理
    list_add_tail( &shost->devices_list,&sdev->siblings);

    // 7. 正式注册设备！
    // 【核心联动】这行代码会唤醒 SCSI 总线，总线会拿着 sdev->type 去找对应的驱动 (如 sd_driver)
    device_register(&sdev->dev);*/

}

// ============================================================================
// 步骤 1: 底层驱动交接入口
// ============================================================================
int32 scsi_add_host(scsi_host_t *shost) {
    // 完善主机的设备树信息
    // 注意：shost->dev.parent 已经在 usb-storage 中被指向了 usb_if
    shost->dev.bus  = NULL;             // Host 是总线提供者，不需要挂载到具体总线去相亲
    shost->dev.type = &scsi_host_type;  // 标记它的身份

    // 1. 将 Host 注册到系统的全局设备树中
    // 这会在操作系统的设备拓扑结构里建立一个物理节点
    //device_register(&shost->dev);

    // 2. 启动异步/同步扫描引擎，探查这个 Host 下面挂了哪些逻辑磁盘
    scsi_probe_lun(shost);

    return 0;
}