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

/*
 * 获取 LUN 数量
*/
#define LUN_BUF_LEN 512
int32 scsi_report_luns(uas_data_t *uas_data) {
    scsi_sense_t scsi_sense;
    scsi_cdb_report_luns_t scsi_cdb_repotr_luns={0};
    scsi_cdb_repotr_luns.opcode = SCSI_REPORT_LUNS;        // REPORT LUNS
    scsi_cdb_repotr_luns.alloc_len = asm_bswap32(LUN_BUF_LEN); // 告诉设备我能收多少数据
    scsi_report_luns_t *scsi_report_luns = kzalloc(LUN_BUF_LEN);
    uas_cmd_params_t uas_cmd_params={&scsi_cdb_repotr_luns,sizeof(scsi_cdb_report_luns_t),0,scsi_report_luns,LUN_BUF_LEN,UAS_DIR_IN,&scsi_sense};
    uas_send_scsi_cmd_sync(uas_data,&uas_cmd_params);
    uint32 list_bytes = asm_bswap32(scsi_report_luns->lun_list_length);
    uint32 luns_count = list_bytes >> 3;
    kfree(scsi_report_luns);
    if (luns_count == 0) return 1;
    return luns_count;
}