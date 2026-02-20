#include "scsi.h"

/**
 * 发送 TEST UNIT READY 命令
 */
int32 scsi_test_unit_ready(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*)) {
    //scsi_sense信息
    scsi_sense_t sense;

    //构造cdb
    scsi_cdb_test_unit_t cdb = {
        .opcode = SCSI_TEST_UNIT_READY,
    };

    //构造scsi_task
    scsi_task_t task={.cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_test_unit_t),
        .lun = lun,
        .dir = SCSI_DIR_NONE,
        .data_buf = NULL,
        .data_len = 0,
        .sense = &sense,
        .status = -1
    };

    //发送scsi命令
    do {
        send_scsi_cmd_sync(dev_context, &task);
    } while (task.status == 2 && sense.flags_key == 0x6 && sense.asc == 0x29 && sense.ascq == 0);

    return task.status;
}

//获取scsi命令错误信息
int32 scsi_request_sense(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),scsi_sense_t *sense) {
    scsi_cdb_request_sense_t cdb = {
        .opcode = SCSI_REQUEST_SENSE,
        .alloc_len = SCSI_SENSE_ALLOC_SIZE
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_request_sense_t),
        .lun = lun,
        .dir = SCSI_DIR_IN,
        .data_buf = sense,
        .data_len = SCSI_SENSE_ALLOC_SIZE,
        .sense = NULL,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context,&task);

    return task.status;
}


//获取u盘信息
int32 scsi_send_inquiry(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*), scsi_inquiry_t *inquiry) {
    scsi_sense_t sense;

    scsi_cdb_inquiry_t cdb = {
        .opcode = SCSI_INQUIRY,
        .alloc_len = sizeof(scsi_inquiry_t)
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_inquiry_t),
        .lun = lun,
        .data_buf = inquiry,
        .data_len = sizeof(scsi_inquiry_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context,&task);

    return task.status;
}

/*
 * 获取 LUN 数量
*/
int32 scsi_report_luns(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),scsi_report_luns_t *report_luns) {
    scsi_sense_t sense;

    scsi_cdb_report_luns_t cdb={
        .opcode = SCSI_REPORT_LUNS,
        .alloc_len =  asm_bswap32(SCSI_LUN_BUF_LEN),
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_report_luns_t),
        .lun = lun,
        .data_buf = report_luns,
        .data_len = SCSI_LUN_BUF_LEN,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context,&task);

    return task.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity10(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),scsi_read_capacity10_t *read_capacity10) {
    scsi_sense_t sense;

    scsi_cdb_read_capacity10_t cdb = {
        .opcode = SCSI_READ_CAPACITY10,
    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_read_capacity10_t),
        .lun = lun,
        .data_buf = read_capacity10,
        .data_len = sizeof(scsi_read_capacity10_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}

/**
 * 获取 U 盘容量
 */
int32 scsi_read_capacity16(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),scsi_read_capacity16_t *read_capacity16) {
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
        .lun = lun,
        .data_buf = read_capacity16,
        .data_len = sizeof(scsi_read_capacity16_t),
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}

//scsi读扇区10
int32 scsi_read10(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),uint32 lba,void *data_buf,uint16 block_count,uint16 block_size) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_READ10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .lun = lun,
        .data_buf = data_buf,
        .data_len = block_count*block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}


//scsi写扇区10
int32 scsi_write10(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),uint32 lba,void *data_buf,uint16 block_count,uint16 block_size) {
    scsi_sense_t sense;

    scsi_cdb_rw10_t cdb = {
        .opcode = SCSI_WRITE10,
        .lba = asm_bswap32(lba),
        .transfer_length = asm_bswap16(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw10_t),
        .lun = lun,
        .data_buf = data_buf,
        .data_len = block_count*block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}

//scsi读扇区16
int32 scsi_read16(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),uint64 lba,void *data_buf,uint32 block_count,uint32 block_size) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_READ16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .lun = lun,
        .data_buf = data_buf,
        .data_len = block_count*block_size,
        .dir = SCSI_DIR_IN,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}

//scsi写扇区16
int32 scsi_write16(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*),uint64 lba,void *data_buf,uint32 block_count,uint32 block_size) {
    scsi_sense_t sense;

    scsi_cdb_rw16_t cdb = {
        .opcode = SCSI_WRITE16,
        .lba = asm_bswap64(lba),
        .transfer_length = asm_bswap32(block_count)

    };

    scsi_task_t task={
        .cdb = &cdb,
        .cdb_len = sizeof(scsi_cdb_rw16_t),
        .lun = lun,
        .data_buf = data_buf,
        .data_len = block_count*block_size,
        .dir = SCSI_DIR_OUT,
        .sense = &sense,
        .status = -1
    };

    send_scsi_cmd_sync(dev_context, &task);

    return task.status;
}