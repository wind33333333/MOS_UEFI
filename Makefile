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

debug-uefiboot: clean
	bash -c "cd .. && source edksetup.sh && build -p MOS_UEFI/uefi_bootPkg/mosboot.dsc -t GCC -a X64 -b DEBUG"
	cp build/DEBUG_GCC/X64/bootx64.efi esp/efi/boot/bootx64.efi
	-mkfifo /tmp/serial.in /tmp/serial.out && -pkill udk-gdb-server
	/opt/intel/udkdebugger/bin/udk-gdb-server &
	qemu-system-x86_64 -monitor telnet:localhost:4444,server,nowait \
					   -M q35 \
					   -m 8G \
					   -cpu max -smp cores=1,threads=1 \
					   -bios OVMF_debug.fd \
					   -drive format=raw,file=fat:rw:./esp \
					   -net none \
					   -serial pipe:/tmp/serial


debug-kernel: clean ${BUILD}/system ${BUILD}/kernel.bin
	bash -c "cd .. && source edksetup.sh && build -p MOS_UEFI/uefi_bootPkg/mosboot.dsc -t GCC -a X64 -b RELEASE"
	cp build/RELEASE_GCC/X64/bootx64.efi esp/efi/boot/bootx64.efi
	qemu-system-x86_64 -monitor telnet:localhost:4444,server,nowait \
					   -M q35 \
					   -m 8G \
					   -S -s \
					   -cpu max -smp cores=2,threads=2 \
					   -bios OVMF.fd \
					   -drive format=raw,file=fat:rw:./esp \
					   -net none

qemu-monitor:
	telnet localhost 4444

clean:
	-rm -rf build
	-rm -rf esp
	-mkdir -p build
	-mkdir -p esp/efi/boot


