#include "moslib.h"

// ==========================================================
// POSIX 核心错误码标准定义
// ==========================================================
#define EPERM        1   /* 操作不允许 (权限不足) */
#define ENOENT       2   /* 文件或目录不存在 / 页表项不存在 */
#define ESRCH        3   /* 进程不存在 (找不到指定的 PID) */
#define EINTR        4   /* 系统调用被中断 (被信号打断) */
#define EIO          5   /* 底层硬件或总线读写错误 */
#define ENXIO        6   /* 没有那个设备或地址 (设备端点无效) */
#define E2BIG        7   /* 参数列表过长 */
#define ENOEXEC      8   /* 可执行文件格式错误 (ELF 魔数不匹配) */
#define EBADF        9   /* 坏的文件描述符 (句柄无效或未打开) */
#define ECHILD       10  /* 没有子进程 (无法进行 wait 回收) */
#define EAGAIN       11  /* 资源暂时不可用 (非阻塞操作需重试) */
#define ENOMEM       12  /* 内存不足 / 内核堆或物理页耗尽 */
#define EACCES       13  /* 权限拒绝 (读写属性不符) */
#define EFAULT       14  /* 坏地址 / 非法用户内存指针 (Bad address) */
#define ENOTBLK      15  /* 需要块设备 */
#define EBUSY        16  /* 设备或资源繁忙 (已被占用) */
#define EEXIST       17  /* 文件已存在 / 页表已被占用 */
#define EXDEV        18  /* 跨设备的硬链接操作 */
#define ENODEV       19  /* 没有那个设备 (设备被拔出或未就绪) */
#define ENOTDIR      20  /* 不是一个目录路径 */
#define EISDIR       21  /* 是一个目录 (无法直接当文件读写) */
#define EINVAL       22  /* 无效参数 (传参越界或不合法) */
#define ENFILE       23  /* 系统打开文件表溢出 */
#define EMFILE       24  /* 当前进程打开的文件数超限 */
#define ENOTTY       25  /* 不恰当的设备控制操作 (ioctl) */
#define ETXTBSY      26  /* 纯文本/可执行文件正在运行被占用 */
#define EFBIG        27  /* 文件体积超出最大限制 */
#define ENOSPC       28  /* 设备无剩余空间 / 总线带宽耗尽 */
#define ESPIPE       29  /* 非法 seek 定位 (对管道或套接字寻址) */
#define EROFS        30  /* 只读文件系统 */
#define EMLINK       31  /* 软/硬链接数过多 */
#define EPIPE        32  /* 管道破裂 / 写入端已关闭 */
#define EDOM         33  /* 数学参数超出定义域 (Math argument out of domain) */
#define ERANGE       34  /* 数学结果超出范围 (Math result not representable) */
#define EDEADLK      35  /* 资源死锁将要发生 (Resource deadlock would occur) */
#define ENAMETOOLONG 36  /* 文件名或路径过长 (File name too long) */
#define ENOLCK       37  /* 系统无可用锁 (No record locks available) */
#define ENOSYS       38  /* 系统调用或驱动暂未实现该功能 */
#define ENOTEMPTY    39  /* 目录非空 (Directory not empty) */
#define ELOOP        40  /* 符号链接层数过多/死循环 (Too many symbolic links) */
#define ECOMM        70  /* 发送时发生底层通信错误 */
#define EPROTO       71  /* 协议错误 (如魔数或握手签名不匹配) */
#define EOVERFLOW    75  /* 数值溢出 / 缓冲区溢出 */
#define EILSEQ       84  /* 非法字节序列 (编码错误 / 相位错乱) */
#define ENOTSOCK     88  /* 非套接字上的套接字操作 (Socket operation on non-socket) */
#define EDESTADDRREQ 89  /* 需要目标地址 (Destination address required) */
#define EMSGSIZE     90  /* 消息太长 (Message too long) */
#define EPROTOTYPE   91  /* 套接字协议类型错误 (Protocol wrong type for socket) */
#define ENOPROTOOPT  92  /* 协议不可用 (Protocol not available) */
#define EPROTONOSUPPORT 93 /* 不支持该协议 (Protocol not supported) */
#define ESOCKTNOSUPPORT 94 /* 不支持该套接字类型 (Socket type not supported) */
#define ENOTSUP      95  /* 不支持的操作 (如不支持的页大小或特性) */
#define EPFNOSUPPORT 96  /* 不支持该协议族 (Protocol family not supported) */
#define EAFNOSUPPORT 97  /* 地址族不支持该协议 (Address family not supported) */
#define EADDRINUSE   98  /* 地址已被占用 (Address already in use) */
#define EADDRNOTAVAIL 99 /* 无法分配请求的地址 (Cannot assign requested address) */
#define ENETDOWN     100 /* 网络已瘫痪 (Network is down) */
#define ENETUNREACH  101 /* 网络不可达 (Network is unreachable) */
#define ENETRESET    102 /* 网络因重置而断开 (Network dropped connection on reset) */
#define ECONNABORTED 103 /* 软件导致连接中止 (Software caused connection abort) */
#define ECONNRESET   104 /* 连接被对端重置 (Connection reset by peer) */
#define ENOBUFS      105 /* 缓冲区满爆 / 发送环下溢出 */
#define EISCONN      106 /* 套接字已经处于连接状态 (Transport endpoint is already connected) */
#define ENOTCONN     107 /* 套接字未连接 (Transport endpoint is not connected) */
#define ESHUTDOWN    108 /* 传输端点已关闭 / 系统停机 */
#define ETOOMANYREFS 109 /* 引用次数过多 (Too many references: cannot splice) */
#define ETIMEDOUT    110 /* 操作或硬件响应等待超时 */
#define ECONNREFUSED 111 /* 连接被拒绝 (Connection refused) */
#define EHOSTDOWN    112 /* 主机已宕机 (Host is down) */
#define EHOSTUNREACH 113 /* 没有到达主机的路由 (No route to host) */
#define EALREADY     114 /* 操作已经在进行中 (Operation already in progress) */
#define EINPROGRESS  115 /* 异步操作已入队，正在后台执行 */
#define ECANCELED    125 /* 操作被软件主动中止 */