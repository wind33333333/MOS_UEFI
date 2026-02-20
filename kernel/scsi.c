#include "scsi.h"

#include "uas.h"

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


//获取u盘信息
int32 scsi_send_inquiry(void *dev_context,uint8 lun,void (*send_scsi_cmd_sync)(void*, scsi_task_t*)) {
    scsi_sense_t sense;

    scsi_cdb_inquiry_t cdb = {
        .opcode = SCSI_INQUIRY,
        .alloc_len = sizeof(scsi_inquiry_t)
    };

    scsi_inquiry_t *inquiry = kzalloc(sizeof(scsi_inquiry_t));

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

    kfree(inquiry);
    return 0;
}

