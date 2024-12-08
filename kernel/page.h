#ifndef __PAGE_INIT_H__
#define __PAGE_INIT_H__
#include "moslib.h"

void init_page(void);

extern UINT64 kernel_pml4t_phy_addr;

#endif