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


# 1. 编译选项：增加所有驱动的头文件寻宝地图
INCLUDES := -I $(KERNEL) \
            -I $(KERNEL)/drivers/usb/include/ \
            -I $(KERNEL)/drivers/scsi/ \
            -I $(KERNEL)/drivers/usb/xhci/

CFLAGS := -fno-stack-protector -mstackrealign -g -fno-builtin -nostdlib $(CFLAGS) $(INCLUDES)

# 2. 自动化获取所有源文件
# 递归搜索 kernel 目录下的所有 .c 和 .S 文件
C_SOURCES   := $(shell find $(KERNEL) -name "*.c")
ASM_SOURCES := $(shell find $(KERNEL) -name "*.S")

# 将源文件路径映射为 build 目录下的 .o 文件路径
# 示例: kernel/drivers/usb/core/usb-core.c -> build/drivers/usb/core/usb-core.o
OBJECTS := $(patsubst $(KERNEL)/%.c, $(BUILD)/%.o, $(C_SOURCES))
OBJECTS += $(patsubst $(KERNEL)/%.S, $(BUILD)/%.o, $(ASM_SOURCES))

# 3. 强制 head.o 在链接序列的最前端 (内核启动必须)
START_OBJ := $(BUILD)/head.o
OTHER_OBJS := $(filter-out $(START_OBJ), $(OBJECTS))

# ==============================================================================
# 自动化编译规则
# ==============================================================================

# 自动处理汇编文件并创建对应子目录
$(BUILD)/%.o: $(BUILD)/%.s
	@mkdir -p $(dir $@)
	as --64 $< -o $@

$(BUILD)/%.s: $(KERNEL)/%.S
	@mkdir -p $(dir $@)
	gcc -E $< > $@

# 自动处理 C 代码，并在 build/ 目录下递归创建相应的子目录
$(BUILD)/%.o: $(KERNEL)/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

# 链接内核：强制 START_OBJ 在最前面
${BUILD}/kernel.elf: $(START_OBJ) $(OTHER_OBJS)
	@mkdir -p $(dir $@)
	ld -b elf64-x86-64 -z muldefs -o $@ $^ -T $(KERNEL)/Kernel.lds

# 生成最终二进制镜像
${BUILD}/kernel.bin: ${BUILD}/kernel.elf
	objcopy --set-section-flags .bss=alloc,load,contents \
	      --set-section-flags .stack=alloc,load,contents \
	      -I elf64-x86-64 -S -R ".eh_frame" -R ".comment" -O binary $^ $@
	nm ${BUILD}/kernel.elf | sort > ${BUILD}/kernel.map

#clion gdb uefi符号挂载
#source /opt/intel/udkdebugger/script/udk_gdb_script
#编译正式版本把bash DEBUG改成RELEASE#
debug-uefi: clean_uefi
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
 					   -drive if=none,id=usbdisk,format=raw,file=fat:rw:./esp \
					   -device qemu-xhci \
                       -device usb-storage,drive=usbdisk &

#-device amd-iommu \amd cpu启用iommu
#-device intel-iommu,intremap=on,caching-mode=on \ intel cpu启用iommu
#-device usb-uas,id=uas,bus=xhci.0 \  禁用这一行u盘为bot协议，启用这一行u盘为uas协议
#device_add usb-storage,drive=hotplug_bot_disk,bus=xhci.0,id=my_bot_usb 动态挂在u盘
#device_del my_bot_usb 写在u盘
debug-kernel: clean_kernel ${BUILD}/kernel.elf ${BUILD}/kernel.bin
	cp $(BUILD)/kernel.bin esp/kernel.bin
	-pkill udk-gdb-server
	-pkill qemu-system-x86
	qemu-system-x86_64 \
	  `# --- 1. 基础系统与调试参数 ---` \
	  -M q35 -m 8G -cpu max -smp sockets=2,cores=2,threads=2 -bios OVMF.fd \
	  -net none -S -s -monitor telnet:localhost:4444,server,nowait \
	  `# --- 2. 核心总线与中断控制器 ---` \
	  -device intel-iommu,intremap=on,caching-mode=on \
	  -device qemu-xhci,id=xhci,msi=on,msix=on \
	  `# --- 3. 启动盘 (BOT, 根端口 1) ---` \
	  -drive if=none,id=bootdisk,format=raw,file=fat:rw:./esp \
	  -device usb-storage,drive=bootdisk,bus=xhci.0,bootindex=1 \
	  `# --- 4. UAS 高速测试盘 (必须直连根端口 2) ---` \
	  -drive if=none,id=uas_backend,format=raw,file=/home/wind3/disk-uas.img \
	  -device usb-uas,id=uas_dev,bus=xhci.0,port=2 \
	  -device scsi-hd,bus=uas_dev.0,scsi-id=0,lun=0,drive=uas_backend \
	  `# --- 5. 外部 Hub (插入根端口 3，它是低速的) ---` \
	  -device usb-hub,id=ext_hub,bus=xhci.0,port=3 \
	  `# --- 6. Hub 级联测试盘 (BOT 协议完美兼容低速，挂在 Hub 端口 1) ---` \
	  -drive if=none,id=hub_bot_disk,format=raw,file=/home/wind3/disk-bot.img \
	  -device usb-storage,drive=hub_bot_disk,bus=xhci.0,port=3.1 \
	  `# --- 7. 留空的热插拔盘 (等你在 Telnet 里玩) ---` \
	  -drive if=none,id=hotplug_disk,format=raw,file=/home/wind3/disk-bot.img &

qemu-monitor:
	telnet localhost 4444

clean_all:
	-rm -rf build esp/efi esp/kernel.*
	-mkdir -p build esp/efi/boot

clean_kernel:
	-rm -rf build/*.* esp/kernel.*

clean_uefi:
	-rm -rf build/DEBUG_GCC build/RELEASE_GCC  esp/efi/boot/bootx64.efi


