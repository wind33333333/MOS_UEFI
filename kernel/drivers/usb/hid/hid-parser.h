#pragma once

#include "moslib.h"

struct hid_dev_t;

int32 hid_parse_report_desc(struct hid_dev_t *hdev, uint8 *desc, uint32 desc_len);
uint32 hid_extract_bits(const uint8 *report, uint32 bit_offset, uint32 bit_size);