;fat32文件系统读取函数
;rdi文件储存区指针
;rsi文件名指针
;rax返回0没找到文件，1找到文件

read_file:
    push    rbp
    mov     rbp,rsp
    sub     rsp,56
    push    rbx
    push    rcx
    push    rdx

    mov [rbp-PFlileAddr],rdi
    mov [rbp-PFileName],rsi

    ;计算文件系统
    movzx eax,word [BPB_RsvdSecCnt]
	add eax,[BPB_HiddSec]
	mov [rbp-Fat32TStartSec],eax                  ;FAT32 1表起始扇区

    mov eax,[BPB_FATSz32]
    movzx ebx,byte [BPB_NumFATs]
    mul ebx
    mov ebx,[BPB_TotSec32]
    movzx ecx,word [BPB_RsvdSecCnt]
    sub ebx,eax
    sub ebx,ecx
    mov [rbp-RootSecNum],ebx                      ;根目录数据区总扇区数

    add eax,[rbp-Fat32TStartSec]
    mov [rbp-RootStartSec],eax                   ;根目录数据区起始扇区

    movzx eax,word [BPB_BytesPerSec]
    movzx ebx,byte [BPB_SecPerClus]
    mul ebx
    mov [rbp-ClusSize],eax                         ;一个簇字节数

;========读取根目录
    mov eax,[rbp-RootStartSec]
    mov [rbp-NextRootSec],eax
readnextroot:
    mov rdi,fat32buf
    mov esi,[rbp-NextRootSec]
    mov edx,1
    call read_disk

;=========遍历根目录查找kernel.bin
    mov rax,fat32buf
    mov [rbp-PFat32TBuf],rax
    cld
nextdirectory:
    mov rsi,[rbp-PFileName]
    mov rdi,[rbp-PFat32TBuf]
    mov rcx,11
    repe cmpsb
    je  kernelequal                       ;是否找到文件
    add qword [rbp-PFat32TBuf],32
    cmp qword [rbp-PFat32TBuf],fat32buf+512   ;当前扇区目录项是否到底了
    jne  nextdirectory

    inc dword [rbp-NextRootSec]
    dec dword [rbp-RootSecNum]
    jne readnextroot                      ;读取下一个根目录扇区到缓存区

    xor rax,rax
    jmp nofile                       ;整个根目录没有找到文件

kernelequal:                              ;成功找到文件
    mov rsi,[rbp-PFat32TBuf]
    movzx eax,word [rsi+26]
    movzx edx,word [rsi+20]
    shl edx,16
    or eax,edx
    mov [rbp-NextClus],eax                     ;文件起始簇号

nextsec:
    mov eax,[rbp-NextClus]
    sub eax,2
    movzx ebx,byte [BPB_SecPerClus]
    mul ebx
    add eax,[rbp-RootStartSec]                  ;文件磁盘起始扇区号

    mov esi,eax
    mov rdi,[rbp-PFlileAddr]
    movzx edx,byte [BPB_SecPerClus]
    call read_disk                           ;读取文件

    mov edx,0
    mov eax,[rbp-NextClus]
    shl eax,2
    movzx ebx,word[BPB_BytesPerSec]
    div ebx
    mov [rbp-ClusOffset],edx                     ;rax fat32扇区号偏移，rdx fat32扇区内偏移

    mov esi,[rbp-Fat32TStartSec]
    add esi,eax
    mov rdi,fat32buf
    mov edx,1
    call read_disk                          ;读取簇号对应的fat32表

    mov esi,[rbp-ClusOffset]
    mov eax,[fat32buf+rsi]
    cmp eax,0x0fffffff                  ;簇号链表是否为结束符
    jz  okfile

    mov [rbp-NextClus],eax
    mov eax,[rbp-ClusSize]
    add qword [rbp-PFlileAddr],rax
    jmp nextsec                         ;继续读取下一个簇号数据到内存

okfile:
    mov rax,1
nofile:
    pop rdx
    pop rcx
    pop rbx
    leave
    ret

;==============================局部变量
Fat32TStartSec:    equ 52                  ;fat32表起始扇区
RootStartSec:      equ 48                  ;根目录和数据区起始扇区
RootSecNum:        equ 44                  ;根目录数据区扇区总数量
NextRootSec:       equ 40                  ;下一个根目录扇区
NextClus:          equ 36                  ;簇号
ClusOffset:        equ 32                  ;fat32簇号在扇区内的偏移
ClusSize:          equ 28                  ;每簇字节数
PFlileAddr:        equ 24                  ;文件储存区指针
PFat32TBuf:        equ 16                  ;fat32buf指针
PFileName:         equ 8                   ;文件名指针

;=======boot fat32结构偏移
    BS_OEMName      equ 0x7C00 + 0x03
    BPB_BytesPerSec equ 0x7C00 + 0x0B
    BPB_SecPerClus  equ 0x7C00 + 0x0D
    BPB_RsvdSecCnt  equ 0x7C00 + 0x0E
    BPB_NumFATs     equ 0x7C00 + 0x10
    BPB_RootEntCnt  equ 0x7C00 + 0x11
    BPB_TotSec16    equ 0x7C00 + 0x13
    BPB_Media       equ 0x7C00 + 0x15
    BPB_FATSz16     equ 0x7C00 + 0x16
    BPB_SecPerTrk   equ 0x7C00 + 0x18
    BPB_NumHeads    equ 0x7C00 + 0x1A
    BPB_HiddSec     equ 0x7C00 + 0x1C
    BPB_TotSec32    equ 0x7C00 + 0x20
    BPB_FATSz32     equ 0x7C00 + 0x24
    BPB_ExtFlags    equ 0x7C00 + 0x28
    BPB_FSVer       equ 0x7C00 + 0x2A
    BPB_RootClus    equ 0x7C00 + 0x2C
    BPB_FSInfo      equ 0x7C00 + 0x30
    BPB_BkBootSec   equ 0x7C00 + 0x32
    BPB_Reserved    equ 0x7C00 + 0x34
    BS_DrvNum       equ 0x7C00 + 0x40
    BS_Reserved1    equ 0x7C00 + 0x41
    BS_BootSig      equ 0x7C00 + 0x42
    BS_VolID        equ 0x7C00 + 0x43
    BS_VolLab       equ 0x7C00 + 0x47
    BS_FilSysType   equ 0x7C00 + 0x52

;=======fat32根目录缓冲区
    fat32buf                equ 0x700                    ;512byte