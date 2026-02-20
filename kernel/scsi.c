#include "scsi.h"

void scsi_build_test_unit_ready(scsi_task_t *task,uint8 lun,scsi_sense_t *sense) {
    scsi_cdb_test_unit_t *cdb = kzalloc(sizeof(scsi_cdb_test_unit_t));
    cdb->opcode = SCSI_TEST_UNIT_READY;
    task->cdb = cdb;
    task->cdb_len = sizeof(scsi_cdb_test_unit_t);
    task->lun = lun;
    task->dir = SCSI_DIR_NONE;
    task->data_buf = NULL;
    task->data_len = 0;
    task->sense = sense;
}

