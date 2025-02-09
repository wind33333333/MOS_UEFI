BUILD:=./build
KERNEL:=./kernel

CFLAGS:= -m64 					# 64 位的程序
#CFLAGS+= -masm=intel			 #intel汇编编码
CFLAGS+= -fno-pic				#禁用要位置无关的代码
CFLAGS+= -fno-pie				#禁用要位置无关的可执行程序
#CFLAFS+= -fpie 				 #启用位置无关的可执行程序
#CFLAFS+= -mcmodel=kernel		#内核内存模型
CFLAGS+= -mcmodel=large 		#大内存模型
CFLAGS += -O0
#CFLAGS += -O3                   # 使用 -O3 优化选项
#CFLAGS+= -mavx2				 #开启avx256指令集优化
CFLAGS+= -fno-stack-protector	#不需要栈保护
CFLAGS+= -mstackrealign			#堆栈自动对齐16字节
CFLAGS+= -g						#开启调试符号
CFLAGS+= -fno-builtin			# 不需要 gcc 内置函数
CFLAGS+= -nostdlib				# 不需要标准库
#CFLAGS+= -nostdinc				 # 不需要标准头文件
CFLAGS:=$(strip ${CFLAGS})


$(BUILD)/%.bin: $(BOOTLOADER)/%.asm
	nasm $< -o $@

${BUILD}/kernel.bin: ${BUILD}/kernel.elf
	objcopy --set-section-flags .bss=alloc,load,contents \
			--set-section-flags .stack=alloc,load,contents \
 			-I elf64-x86-64 -S -R ".eh_frame" -R ".comment" -O binary $^ $@
	nm ${BUILD}/kernel.elf | sort > ${BUILD}/kernel.map

${BUILD}/kernel.elf: ${BUILD}/head.o ${BUILD}/main.o ${BUILD}/printk.o ${BUILD}/interrupt.o \
 				 ${BUILD}/ap.o ${BUILD}/idt.o ${BUILD}/acpi.o ${BUILD}/apic.o ${BUILD}/ioapic.o \
				 ${BUILD}/vmm.o ${BUILD}/gdt.o ${BUILD}/tss.o ${BUILD}/cpu.o ${BUILD}/memblock.o \
				 ${BUILD}/hpet.o ${BUILD}/apboot.o ${BUILD}/syscall.o ${BUILD}/buddy_system.o \
				 ${BUILD}/slub.o ${BUILD}/kpage_table.o
	ld -b elf64-x86-64 -z muldefs -o $@ $^ -T $(KERNEL)/Kernel.lds

$(BUILD)/%.o: $(BUILD)/%.s
	as --64 $< -o $@

$(BUILD)/%.s: $(KERNEL)/%.S
	gcc -E $< > $@

$(BUILD)/%.o: $(KERNEL)/%.c
	gcc ${CFLAGS} -c $< -o $@

debug-uefi:
	#编译正式版本把bash DEBUG改成RELEASE#
	bash -c "cd .. && source edksetup.sh && build -p MOS_UEFI/uefi_bootPkg/mosboot.dsc -t GCC -a X64 -b DEBUG"
	cp build/DEBUG_GCC/X64/bootx64.efi esp/efi/boot/bootx64.efi
	-mkfifo /tmp/serial.in /tmp/serial.out
	-pkill udk-gdb-server
	-pkill qemu-system-x86
	/opt/intel/udkdebugger/bin/udk-gdb-server &
	qemu-system-x86_64 -monitor telnet:localhost:4444,server,nowait \
					   -serial pipe:/tmp/serial \
					   -net none \
					   -M q35 \
					   -m 8G \
 					   -cpu max -smp sockets=2,cores=2,threads=2 \
 					   -bios OVMF_debug.fd \
 					   -device qemu-xhci,id=xhci \
                       -device usb-storage,bus=xhci.0,drive=usbdisk \
                       -drive if=none,id=usbdisk,format=raw,file=fat:rw:./esp &


debug-kernel: ${BUILD}/kernel.elf ${BUILD}/kernel.bin
	cp $(BUILD)/kernel.bin esp/kernel.bin
	-pkill udk-gdb-server
	-pkill qemu-system-x86
	qemu-system-x86_64 -monitor telnet:localhost:4444,server,nowait \
					   -S -s \
					   -net none \
					   -M q35 \
					   -m 8G \
					   -cpu max -smp sockets=2,cores=2,threads=2 \
					   -bios OVMF.fd \
 					   -device qemu-xhci,id=xhci \
                       -device usb-storage,bus=xhci.0,drive=usbdisk \
                       -drive if=none,id=usbdisk,format=raw,file=fat:rw:./esp &


qemu-monitor:
	telnet localhost 4444

clean_all:
	-rm -rf build esp/efi esp/kernel.*
	-mkdir -p build esp/efi/boot

#clion gdb uefi符号挂载
#source /opt/intel/udkdebugger/script/udk_gdb_script

