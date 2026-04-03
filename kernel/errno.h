#include "moslib.h"

// ==========================================================
// POSIX 错误码标准定义 (节选核心)
// ==========================================================
#define EPERM        1  /* Operation not permitted (操作不允许) */
#define EIO          5  /* I/O error (底层硬件/总线读写错误) */
#define EAGAIN      11  /* Try again (资源暂时不可用) */
#define ENOMEM      12  /* Out of memory (资源或内存耗尽) */
#define	EBUSY		16	/* Device or resource busy 设备或资源繁忙 */
#define ENODEV      19  /* No such device (设备被拔出或端点未激活) */
#define EINVAL      22  /* Invalid argument (上下文图纸参数配置错误) */
#define ENOSPC      28  /* No space left on device (总线带宽耗尽) */
#define ENOSYS      38  /* Function not implemented: 驱动暂未实现该功能 */
#define EPIPE       32  /* Broken pipe (管道破裂/设备STALL) */
#define ECOMM       70  /* Communication error on send (发送时发生通信错误) */
#define EPROTO      71  /* Protocol error (协议错误，如签名不匹配) */
#define EOVERFLOW   75  /* Value too large for defined data type (数值溢出/缓冲区溢出) */
#define EILSEQ      84  /* Illegal byte sequence (数据非法/相位错误) */
#define ENOBUFS     105  /* No buffer space available (环满爆/下溢出) */
#define ESHUTDOWN   108  /* Cannot send after transport endpoint shutdown (端点已关闭/停机) */
#define ETIMEDOUT   110  /* Connection timed out (事件环/传输环等待超时) */
#define EINPROGRESS 115 /* Operation now in progress 异步操作已入队，正在执行！*/
#define ECANCELED   125  /* Operation Canceled (操作被软件主动中止/Stop EP) */