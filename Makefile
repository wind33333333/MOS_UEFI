BUILD:=./build
KERNEL:=./kernel

CFLAGS:= -m64 					# 64 位的程序
#CFLAGS+= -masm=intel			#intel汇编编码
CFLAGS+= -fno-builtin			# 不需要 gcc 内置函数
#CFLAGS+= -nostdinc				# 不需要标准头文件
CFLAGS+= -fno-pic				# 不需要位置无关的代码  position independent code
CFLAGS+= -fno-pie				# 不需要位置无关的可执行程序 position independent executable
CFLAGS+= -nostdlib				# 不需要标准库
CFLAGS+= -mcmodel=large 		#大内存模型
CFLAGS+= -fno-stack-protector	# 不需要栈保护
CFLAGS+= -g						#开启调试符号
#CFLAGS += -O3                  # 使用 -O3 优化选项
CFLAGS:=$(strip ${CFLAGS})

#all: clean build_uefi_boot ${BUILD}/system ${BUILD}/kernel.bin
all: clean build_uefi_boot

build_uefi_boot:
	bash -c "cd .. && source edksetup.sh && build"
	cp build/DEBUG_GCC/X64/MOSBoot.efi ESP/EFI/Boot/bootx64.efi

$(BUILD)/%.bin: $(BOOTLOADER)/%.asm
	nasm $< -o $@

${BUILD}/kernel.bin: ${BUILD}/system
	objcopy -I elf64-x86-64 -S -R ".eh_frame" -R ".comment" -O binary $^ $@
	nm ${BUILD}/system | sort > ${BUILD}/system.map
	cp $(BUILD)/kernel.bin ESP/EFI/Boot/kernel.bin

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
					   -drive format=raw,file=fat:rw:./ESP \
					   -net none


qemu-monitor:
	telnet 127.0.0.1 4444

clean:
	-rm -rf ./build/*
	-rm -rf ./ESP/EFI/Boot/*


