;IDE磁盘扇区读函数lba48模式
;rdi 指向读取缓存区
;rsi 48位LBA逻辑扇区号
;rdx 读取的扇区数 16bit
read_disk:
    push rcx
	mov rcx,rdx

	mov dx,0x1f2		;0x1f2
	mov al,ch   ;要读取的扇区数高8位
	out dx,al
	mov al,cl	;要读取的扇区数低8位
	out dx,al

	inc	dx		;0x1f3
	mov rax,rsi
	ror	rax,24	;24-31位
	out dx,al
	rol	rax,24	;0-7位
	out dx,al
	inc dx		;0x1f4
	ror rax,32
	out dx,al	;32-39
	rol rax,24	;8-15位
	out dx,al
	inc dx		;0x1f5
	ror rax,32	;40-47位
	out dx,al
	rol rax,24	;16-23位
	out dx,al

	inc dx		;0x1f6
	mov	al,0x40	;lba寻址	主磁盘
	out dx,al

	inc dx		;0x1f7
	mov al,0x24	;读磁盘24h
	out dx,al
	cld
.readnextsec:
    push rcx
.waits:
	mov dx,0x1f7
	in al,dx
	and al,0x88
	cmp al,0x08
	jnz .waits ; 不忙，且硬盘已准备好数据传输

    mov rcx,256
	mov dx, 0x1f0 ; 读取端口号
	rep insw
    pop rcx
    loop .readnextsec

    pop rcx
	ret