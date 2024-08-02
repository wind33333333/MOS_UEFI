BUILD:=./build
BOOTLOADER:=./bootloader
KERNEL:=./kernel
MNT:=/home/wind3/mnt
HDD:=fat32.img
MBR1:=1048666				#主mbr代码引导=起始扇区号*512+90
MBR2:=1051738				#备份mbr代码引导=（起始扇区号+6)*512+90
LOADER:=1052672				#loader起始扇区
LOASERLEN:=10240			#loader长度

CFLAGS:= -m64 			# 64 位的程序
#CFLAGS+= -masm=intel	#intel汇编编码
CFLAGS+= -fno-builtin	# 不需要 gcc 内置函数
#CFLAGS+= -nostdinc		# 不需要标准头文件
CFLAGS+= -fno-pic		# 不需要位置无关的代码  position independent code
CFLAGS+= -fno-pie		# 不需要位置无关的可执行程序 position independent executable
CFLAGS+= -nostdlib		# 不需要标准库
CFLAGS+= -mcmodel=large #大内存模型
CFLAGS+= -fno-stack-protector	# 不需要栈保护
CFLAGS+= -g						#开启调试符号
#CFLAGS += -O3                  # 使用 -O3 优化选项
CFLAGS:=$(strip ${CFLAGS})

all: clean $(BUILD)/boot.bin $(BUILD)/loader.bin ${BUILD}/system ${BUILD}/kernel.bin HDDimg

HDDimg:
	dd if=$(BUILD)/boot.bin of=$(BUILD)/$(HDD) bs=1 seek=$(MBR1) skip=90 count=422 conv=notrunc
	dd if=$(BUILD)/boot.bin of=$(BUILD)/$(HDD) bs=1 seek=$(MBR2) skip=90 count=422 conv=notrunc
	dd if=$(BUILD)/loader.bin of=$(BUILD)/$(HDD) bs=1 seek=$(LOADER) skip=0 count=$(LOASERLEN) conv=notrunc
	sudo mount -o loop,offset=1048576,uid=1000,gid=1000 $(BUILD)/$(HDD) $(MNT)   #offset fat32分区起始地址=起始扇区号*512
	cp $(BUILD)/kernel.bin $(MNT)
	sudo umount $(MNT)

$(BUILD)/%.bin: $(BOOTLOADER)/%.asm
	nasm $< -o $@

${BUILD}/kernel.bin: ${BUILD}/system
	objcopy -I elf64-x86-64 -S -R ".eh_frame" -R ".comment" -O binary $^ $@
	nm ${BUILD}/system | sort > ${BUILD}/system.map

${BUILD}/system: ${BUILD}/head.o ${BUILD}/main.o ${BUILD}/printk.o ${BUILD}/interrupt.o \
 				${BUILD}/ioapic.o ${BUILD}/ap.o ${BUILD}/acpi.o ${BUILD}/idt.o ${BUILD}/apic.o \
				${BUILD}/memory.o ${BUILD}/gdt.o ${BUILD}/tss.o ${BUILD}/page.o ${BUILD}/cpuinfo.o \
				${BUILD}/hpet.o
	ld -b elf64-x86-64 -z muldefs -o $@ $^ -T $(KERNEL)/Kernel.lds

$(BUILD)/%.o: $(BUILD)/%.s
	as --64 $< -o $@

$(BUILD)/%.s: $(KERNEL)/%.S
	gcc -E $< > $@

$(BUILD)/%.o: $(KERNEL)/%.c
	gcc ${CFLAGS} -c $< -o $@

bochs: all
	bochs -q -f bochsrc

#qemu-gdb: all
	qemu-system-x86_64 -monitor telnet:127.0.0.1:4444,server,nowait \
					   -M pc \
					   -m 8G \
					   -boot c \
					   -S -s \
					   -cpu max -smp cores=2,threads=2 \
					   -hda $(BUILD)/$(HDD)

#qemu: all
	qemu-system-x86_64 -monitor telnet:127.0.0.1:4444,server,nowait \
					   -M pc \
					   -m 8G \
					   -boot c \
					   -cpu max -smp cores=2,threads=2 \
					   -hda $(BUILD)/$(HDD)

qemu-gdb: all
	qemu-system-x86_64 -monitor telnet:127.0.0.1:4444,server,nowait \
					   -M q35 \
					   -m 8G \
					   -boot c \
					   -S -s \
					   -cpu max -smp cores=2,threads=2 \
					   -drive if=pflash,format=raw,file=/usr/local/share/qemu/edk2-x86_64-code.fd,readonly=on \
					   -drive if=pflash,format=raw,file=/usr/local/share/qemu/edk2-x86_64-secure-code.fd,readonly=on \
					   -net none

qemu: all
	qemu-system-x86_64 -monitor telnet:127.0.0.1:4444,server,nowait \
					   -M q35 \
					   -m 8G \
					   -cpu max -smp cores=2,threads=2 \
					   -drive if=pflash,format=raw,file=/usr/local/share/qemu/edk2-x86_64-code.fd,readonly=on \
					   -drive if=pflash,format=raw,file=/usr/local/share/qemu/edk2-x86_64-secure-code.fd,readonly=on \
					   -net none


qemu-monitor:
	telnet 127.0.0.1 4444

clean:
#	-rm ./build/*
	find build/ -type f ! -name "$(HDD)" -exec rm {} +


