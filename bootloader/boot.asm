 org 0x7C00
 [bits  16]
    jmp bootstart
    nop
    BS_OEMName  	db  "MOS BOOT"
    BPB_BytesPerSec	dw  0
    BPB_SecPerClus	db  0
    BPB_RsvdSecCnt	dw  0
    BPB_NumFATs 	db  0
    BPB_RootEntCnt	dw  0
    BPB_TotSec16	dw  0
    BPB_Media	    db  0
    BPB_FATSz16	    dw  0
    BPB_SecPerTrk	dw  0
    BPB_NumHeads	dw  0
    BPB_HiddSec	    dd  0
    BPB_TotSec32	dd  0
    BPB_FATSz32 	dd  0
    BPB_ExtFlags	dw  0
    BPB_FSVer   	dw  0
    BPB_RootClus	dd  0
    BPB_FSInfo	    dw  0
    BPB_BkBootSec	dw  0
    BPB_Reserved	dd  0,0,0
    BS_DrvNum	    db  0
    BS_Reserved1	db  0
    BS_BootSig	    db  0
    BS_VolID	    dd  0
    BS_VolLab   	db  "           "
    BS_FilSysType	db  "FAT32   "

 bootstart:
	mov	ax,cs
	mov	ds,ax
	mov	es,ax
	mov ax,BaseOfStack
	mov	ss,ax
	mov	sp,OffsetofStack


;=======	clear screen

	mov	ax,0600h
	mov	bx,0700h
	mov	cx,0
	mov	dx,0184fh
	int	10h

;=======	set focus

	mov	ax,0200h
	mov	bx,0000h
	mov	dx,0000h
	int	10h

;=======	display on screen : Start Booting......

	mov	ax,1301h
	mov	bx,000fh
	mov	dx,0000h
	mov	cx,10
	push ax
	mov	ax,ds
	mov	es,ax
	pop	ax
	mov	bp,StartBootMessage
	int	10h

;从dbr扇区+8开始读入loader.bin到内存0x10000
    mov word [count],loadersecnum
    mov word [bufferoff],OffsetOfLoader
    mov word [bufferseg],BaseOfLoader
    mov eax,[BPB_HiddSec]
    add eax,8
    mov dword [blockNum],eax
    mov si,packet_struct
    mov ah,0x42         ;读0x42 写0x43
    mov dl,0x80         ;驱动器号
    int 0x13
    jnc testloader    ;读取loader成功跳转到testloader

    mov	ax,	1301h
    mov	bx,	008ch
    mov	dx,	0100h
    mov	cx,	16
    push	ax
    mov	ax,	ds
    mov	es,	ax
    pop	ax
    mov	bp,	diskerr
    int	10h                 ;打印磁盘读取错误
 dhlt:
    hlt
    jmp	dhlt

testloader:
    mov ax,BaseOfLoader
    mov ds,ax
    cmp word [OffsetOfLoader],0x9090
    jne loadererr
    jmp  BaseOfLoader:OffsetOfLoader                     ;跳到loader入口

loadererr:
	mov	ax,	1301h
	mov	bx,	008ch
	mov	dx,	0100h
	mov	cx,	21
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	NoLoaderMessage
	int	10h                            ;没有找到loader.bin
loderr:
    hlt
    jmp loderr

    BaseOfStack	    equ	0x0
    OffsetofStack   equ	0x0                ;stack up 0x10000
    BaseOfLoader	equ	0x1000
    OffsetOfLoader	equ	0x0                ;loader 0x10000
    loadersecnum    equ 20                ;loader secnum 20

    ;int 13磁盘读写数据结构体
    packet_struct:
    packet_size:     db	10h                 ;packet大小，16个字节
    reserved:	     db 0
    count:		     dw	0		             ;读扇区数
    bufferoff:       dw	0                   ;偏移地址
    bufferseg:	     dw	0		            ;段地址
    blockNum:	     dd	0                   ;起始LBA块
    		         dd 0

;equ
startloader equ 0x10000

;=======	display messages

StartBootMessage:	db	"Start Boot"
NoLoaderMessage:	db	"ERROR:No LOADER Found"
diskerr             db  "Read Disk ERROR!"

;=======	fill zero until whole sector

boot	times	510 - ($ - $$)	db	0
	    dw	0xaa55