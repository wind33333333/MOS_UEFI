#include "moslib.h"
#include "errno.h"
#include "vmm.h"

/**
 * @brief 从用户空间安全复制数据到内核空间 (Copy In)
 */
int32 copy_from_user(void *kdst, const void *usrc, uint32 size) {
    // 1. 基础指针与尺寸检查
    if (!usrc || !kdst || !size) {
        return EINVAL;
    }

    // 2. 防御整数溢出与越界：确保用户起始地址和结束地址都在合法的用户空间内
    uint64 user_start = (uint64)usrc;
    if (user_start >= vm_layout.user_map_end) {
        return EINVAL;
    }
    if ((uint64)size > (vm_layout.user_map_end - user_start)) {
        return EINVAL; // 防止 size 过大导致地址回绕溢出
    }

    // 3. 开启 SMAP 临时特权
    asm_stac();

    // 4. 执行内存拷贝
    // 注意：请确保你的 asm_mem_cpy 内部或此处有异常表保护机制，
    // 否则若遇到未映射的野指针，仍会触发 Kernel Panic。
    asm_mem_cpy(kdst,(void *)usrc,  size);

    // 5. 关闭 SMAP 特权
    asm_clac();

    return 0;
}

/**
 * @brief 从内核空间安全复制数据到用户空间 (Copy Out)
 */
int32 copy_to_user(void *udst,const void *ksrc,  uint32 size) {
    // 1. 基础指针与尺寸检查
    if (!ksrc || !udst || !size) {
        return EINVAL;
    }

    // 2. 防御整数溢出与越界
    uint64 user_start = (uint64)udst;
    if (user_start >= vm_layout.user_map_end) {
        return EINVAL;
    }
    if ((uint64)size > (vm_layout.user_map_end - user_start)) {
        return EINVAL;
    }

    // 3. 开启 SMAP 临时特权
    asm_stac();

    // 4. 执行内存拷贝
    asm_mem_cpy(udst,(void *)ksrc,  size);

    // 5. 关闭 SMAP 特权
    asm_clac();

    return 0;
}


/**
 * @brief 从用户空间安全复制以 '\0' 结尾的字符串到内核空间
 * @param usrc     用户空间源字符串指针
 * @param kdst     内核空间目标缓冲区
 * @param max_len  内核缓冲区最大长度（包含 '\0'）
 * @return         成功返回实际复制的字符数（不含 '\0'），失败返回负数错误码 (-EINVAL / -EFAULT)
 */
int32 strncpy_from_user(char *kdst,const char *usrc,  uint32 max_len) {
    // 1. 基础参数有效性检查
    if (!usrc || !kdst || max_len == 0) {
        return -EINVAL;
    }

    uint64 user_start = (uint64)usrc;

    // 2. 地址边界与防溢出检查
    // 确保起始地址在合法的用户空间范围内
    if (user_start >= vm_layout.user_map_end) {
        return -EFAULT;
    }

    // 如果 max_len 可能会导致读取范围超出用户空间边界，进行裁剪或拒绝
    if ((uint64)max_len > (vm_layout.user_map_end - user_start)) {
        max_len = (uint32)(vm_layout.user_map_end - user_start);
        if (max_len == 0) {
            return -EFAULT;
        }
    }

    // 3. 开启 SMAP 临时特权（允许 Ring 0 访问 Ring 3 内存）
    asm_stac();

    // 4. 逐字节复制用户态字符串
    char* end = asm_strncpy(kdst, usrc, max_len);

    // 6. 关闭 SMAP 特权，恢复严格防护
    asm_clac();

    // 5. 强制添加字符串结束符 '\0'，防止缓冲区溢出
    *end = '\0';

    return (int32)end - (int32)kdst;
}