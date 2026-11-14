#include "video_init.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "uefi_init.h"
#include "vmm_init.h"

extern position_t Pos;

__attribute__((aligned(4096))) uint64 tmp_page_buffer[1024];

void output_init(void) {
    Pos.XResolution = tmp_boot_info->horizontal_resolution;
    Pos.YResolution = tmp_boot_info->vertical_resolution;
    Pos.PixelsPerScanLine = tmp_boot_info->pixels_per_scan_line;
    Pos.XPosition = 0;
    Pos.YPosition = 0;
    Pos.XCharSize = 8;
    Pos.YCharSize = 16;
    Pos.FB_addr = tmp_directly_map(tmp_boot_info->frame_buffer_base,tmp_boot_info->frame_buffer_size,tmp_page_buffer);
    Pos.FB_length = tmp_boot_info->frame_buffer_size;
    Pos.lock = 0;

    for (uint64 i = 0; i < (Pos.PixelsPerScanLine * Pos.YResolution); i++) {
        Pos.FB_addr[i] = BLACK;
    }

    PR_OK("Out Put init Success!\n");

}

extern vm_space_t kernel_space;
void tmp_video_mem_map(void) {
    vm_map_range(&kernel_space,(uint64)pa_to_va(tmp_boot_info->frame_buffer_base),tmp_boot_info->frame_buffer_base,tmp_boot_info->frame_buffer_size,PAGE_KERNEL_MMIO_WC | SW_FLAG_MAX_1G);
}

void video_mem_map(void) {
    uint32 *new_fb_vaddr = ioremap_wc(tmp_boot_info->frame_buffer_base,tmp_boot_info->frame_buffer_size);
    vm_unmap_range(&kernel_space,tmp_boot_info->frame_buffer_base,tmp_boot_info->frame_buffer_size,UNMAP_FLAG_NONE);
    Pos.FB_addr = new_fb_vaddr;
    PR_INFO("Voide Memory Physics Address:%#lx -> Virtual Address:%#lx  Video Size:%#lx Resolution:%d * %d\n",tmp_boot_info->frame_buffer_base,Pos.FB_addr,Pos.FB_length,Pos.XResolution,Pos.YResolution);
}
