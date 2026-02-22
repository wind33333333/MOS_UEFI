#include "scsi.h"
#include "slub.h"
#include "printk.h"

// 统一的 SCSI 任务执行器和错误处理逻辑
int32 scsi_execute(scsi_device_t *scsi_dev, scsi_task_t *task) {
    int retry_count = 3;
    do {
        // 调用底层绑定的真实发送函数 (BOT/UAS)
        scsi_dev->send_cmd_sync(scsi_dev->transport_context, task);

        // 统一执行状态处理逻辑
        if (task->status == 0) {
            // 如果成功，直接返回
            break;
        }else if(task->status == 2 && task->sense->flags_key == 0x06 && task->sense->asc == 0x29) {
            // 这Unit Attention (设备刚上电/复位) 是良性错误，静默重试
            retry_count--;
        }else{
            //其他错误处理
            color_printk(RED,BLACK,"send_cmd_sync error status:%#x  flags_key:%#x  asc:%#x  \n",task->status,task->sense->flags_key,task->sense->asc);
            while (1);
        }

    } while (retry_count > 0);

    return task->status;
}


/**
 * 发送 TEST UNIT READY 命令
 */
int32 scsi_test_unit_ready(scsi_device_t *scsi_dev) {
    //scsi_sense信息
    scsi_sense_t sense;

    //构造cdb
    scsi_cdb_test_unit_t cdb = {
        .opcode = SCSI_TEST_UNIT_READY,
    };

    //构造scsi_task
    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_test_unit_t),
        .lun = scsi_dev->lun,
        .dir = SCSI_DIR_NONE,
        .data_buf = NULL,
        .data_len = 0,
        .sense = &sense,
        .status = -1
    };

    //发送scsi命令
    scsi_execute(scsi_dev,&task);

    return task.status;
}

//获取scsi命令错误信息
int32 scsi_request_sense(scsi_device_t *scsi_dev,scsi_sense_t *sense) {
    scsi_cdb_request_sense_t cdb = {
        .opcode = SCSI_REQUEST_SENSE,
        .alloc_len = SCSI_SENSE_ALLOC_SIZE
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_request_sense_t),
        .lun = scsi_dev->lun,
        .dir = SCSI_DIR_IN,
        .data_buf = sense,
        .data_len = SCSI_SENSE_ALLOC_SIZE,
        .sense = NULL,
        .status = -1
    };

    scsi_execute(scsi_dev,&task);

    return task.status;
}


//获取u盘信息
int32 scsi_send_inquiry(scsi_device_t *scsi_dev, scsi_inquiry_t *inquiry) {
    scsi_sense_t sense;

    scsi_cdb_inquiry_t cdb = {
        .opcode = SCSI_INQUIRY,
        .alloc_len = sizeof(scsi_inquiry_t)
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_inquiry_t),
        .lun = scsi_dev->lun,
        .data_buf = inquiry,
        .data_len = sizeof(scsi_inquiry_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev,&task);

    return task.status;
}

/*
 * 获取 LUN 数量
*/
int32 scsi_report_luns(scsi_device_t *scsi_dev,scsi_report_luns_t *report_luns) {
    scsi_sense_t sense;

    scsi_cdb_report_luns_t cdb={
        .opcode = SCSI_REPORT_LUNS,
        .alloc_len =  asm_bswap32(SCSI_LUN_BUF_LEN),
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_report_luns_t),
        .lun = 0,
        .data_buf = report_luns,
        .data_len = SCSI_LUN_BUF_LEN,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev,&task);

    return task.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity10(scsi_device_t *scsi_dev,scsi_read_capacity10_t *read_capacity10) {
    scsi_sense_t sense;

    scsi_cdb_read_capacity10_t cdb = {
        .opcode = SCSI_READ_CAPACITY10,
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_read_capacity10_t),
        .lun = scsi_dev->lun,
        .data_buf = read_capacity10,
        .data_len = sizeof(scsi_read_capacity10_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev,&task);

    return task.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity16(scsi_device_t *scsi_dev,scsi_read_capacity16_t *read_capacity16) {
    scsi_sense_t sense;

    scsi_cdb_read_capacity16_t cdb = {
        .opcode = SCSI_READ_CAPACITY16,
        .service_action = SA_READ_CAPACITY_16,
        .lba = 0,
        .alloc_len = asm_bswap32(sizeof(scsi_read_capacity16_t))

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_read_capacity16_t),
        .lun = scsi_dev->lun,
        .data_buf = read_capacity16,
        .data_len = sizeof(scsi_read_capacity16_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev,&task);

    return task.status;
}

//scsi读扇区10
int32 scsi_read10(scsi_device_t *scsi_dev,void *data_buf,uint32 lba,uint16 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_READ10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .lun = scsi_dev->lun,
        .data_buf = data_buf,
        .data_len = block_count*scsi_dev->block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev, &task);

    return task.status;
}


//scsi写扇区10
int32 scsi_write10(scsi_device_t *scsi_dev,void *data_buf,uint32 lba,uint16 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_WRITE10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .lun = scsi_dev->lun,
        .data_buf = data_buf,
        .data_len = block_count*scsi_dev->block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev, &task);

    return task.status;
}

//scsi读扇区16
int32 scsi_read16(scsi_device_t *scsi_dev,void *data_buf,uint64 lba,uint32 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_READ16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .lun = scsi_dev->lun,
        .data_buf = data_buf,
        .data_len = block_count*scsi_dev->block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev, &task);

    return task.status;
}

//scsi写扇区16
int32 scsi_write16(scsi_device_t *scsi_dev,void *data_buf,uint64 lba,uint32 block_count) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_WRITE16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .lun = scsi_dev->lun,
        .data_buf = data_buf,
        .data_len = block_count*scsi_dev->block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    scsi_execute(scsi_dev, &task);

    return task.status;
}

//创建scsi_host
scsi_host_t *scsi_create_host(scsi_host_template_t *host_template,void* host_data,device_t *parent,uint8 max_lun,char *name) {
    scsi_host_t *scsi_host = kzalloc(sizeof(scsi_host_t));
    scsi_host->dev.name = name;
    scsi_host->dev.bus = 0;
    scsi_host->dev.bus_node.next = NULL;
    scsi_host->dev.bus_node.prev = NULL;
    scsi_host->dev.child_list.next = NULL;
    scsi_host->dev.child_list.prev = NULL;
    scsi_host->dev.child_node.next = NULL;
    scsi_host->dev.child_node.prev = NULL;
    scsi_host->dev.drv = NULL;
    scsi_host->dev.drv_data = NULL;
    scsi_host->dev.parent = parent;
    scsi_host->dev.type = NULL;

    scsi_host->hostt = host_template;
    scsi_host->hostdata = host_data;
    scsi_host->host_no = 0;
    scsi_host->max_lun = max_lun;
    scsi_host->host_status = 0;
    list_head_init(&scsi_host->devices_list);
    return scsi_host;
}