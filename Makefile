BUILD:=./build
KERNEL:=./kernel

# --- 架构与内存模型 ---
CFLAGS := -m64
CFLAGS += -mcmodel=kernel         # 推荐使用 kernel 内存模型，而不是 large
CFLAGS += -mno-red-zone           # 【极其重要】禁止使用红区，防止中断破坏栈帧
CFLAGS += -mstackrealign          # 确保栈16字节对齐

# --- 裸机环境隔离 ---
CFLAGS += -nostdlib               # 不链接标准库
CFLAGS += -fno-builtin            # 不使用内建函数
CFLAGS += -ffreestanding          # 声明独立环境（非宿主环境）
CFLAGS += -fno-pic -fno-pie       # 禁用位置无关代码

# --- 安全机制（内核需自行掌控） ---
CFLAGS += -fno-stack-protector    # 禁用栈保护（后期内核实现相应功能后再开启）

# --- 禁用浮点/向量指令（防止污染内核态） ---
CFLAGS += -mno-mmx -mno-sse -mno-sse2

# --- 调试、优化与警告 ---
CFLAGS += -O0                     # 开发阶段禁用优化，稳定后可改为 -O2
CFLAGS += -g                      # 生成调试符号 (配合 GDB)
CFLAGS += -Wall -Wextra           # 开启所有常见警告，防患于未然

CFLAGS:=$(strip ${CFLAGS})


# 1. 编译选项：增加所有驱动的头文件寻宝地图
INCLUDES := -I $(KERNEL)/include/

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
START_OBJ := $(BUILD)/init/head.o
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
	ld -b elf64-x86-64 -z muldefs -o $@ $^ -T $(KERNEL)/init/Kernel.lds

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
		`# --- 1. 基础系统与 SMP 拓扑 (12 颗开机在线 + 4 颗热插拔空槽 = 16 核上限) ---` \
		`# 拓扑总槽位: 4 Sockets * 4 Cores * 1 Thread = 16 (APIC ID 0~15)` \
		`# 先用 cpus=1 仅拉起 BSP (Socket0 Core0, APIC 0)，再通过 -device 精准插入其余 11 颗开机核` \
		`# 效果：每个 Socket 保留 core-id=3 (APIC 3, 7, 11, 15) 作为热插拔槽位，` \
		`#       开机在线核心 APIC ID 依然保持空洞: (0,1,2), (4,5,6), (8,9,10), (12,13,14)` \
		-M q35 -m 8G -cpu max \
		-smp cpus=1,maxcpus=16,sockets=4,cores=4,threads=1 \
		-bios OVMF.fd \
		-net none -S -s -monitor telnet:localhost:4444,server,nowait \
		\
		`# --- 1.1 精确插入其余 11 颗开机在线核心 (每 Socket 留出 core-id=3 待热插拔) ---` \
		-device max-x86_64-cpu,id=cpu-s0c1,socket-id=0,core-id=1,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s0c2,socket-id=0,core-id=2,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s1c0,socket-id=1,core-id=0,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s1c1,socket-id=1,core-id=1,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s1c2,socket-id=1,core-id=2,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s2c0,socket-id=2,core-id=0,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s2c1,socket-id=2,core-id=1,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s2c2,socket-id=2,core-id=2,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s3c0,socket-id=3,core-id=0,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s3c1,socket-id=3,core-id=1,thread-id=0 \
		-device max-x86_64-cpu,id=cpu-s3c2,socket-id=3,core-id=2,thread-id=0 \
		\
		`# --- 2. NUMA 内存后端 ---` \
		-object memory-backend-ram,id=mem0,size=2G \
		-object memory-backend-ram,id=mem1,size=2G \
		-object memory-backend-ram,id=mem2,size=2G \
		-object memory-backend-ram,id=mem3,size=2G \
		\
		`# --- 3. NUMA 节点与内存绑定 ---` \
		-numa node,nodeid=0,memdev=mem0 \
		-numa node,nodeid=1,memdev=mem3 `# 故意把 node 1 指向最后一块内存 mem3` \
		-numa node,nodeid=2,memdev=mem1 \
		-numa node,nodeid=3,memdev=mem2 \
		\
		`# --- 4. 现代语法的 CPU 精确拓扑绑定 (自动覆盖开机核与热插拔槽位) ---` \
		-numa cpu,node-id=0,socket-id=0 `# APIC 0,1,2 开机 + APIC 3 热插拔` \
		-numa cpu,node-id=1,socket-id=3 `# APIC 12,13,14 开机 + APIC 15 热插拔` \
		-numa cpu,node-id=2,socket-id=1 `# APIC 4,5,6 开机 + APIC 7 热插拔` \
		-numa cpu,node-id=3,socket-id=2 `# APIC 8,9,10 开机 + APIC 11 热插拔` \
		\
		`# --- 5. NUMA 访问距离矩阵 (SLIT) ---` \
		-numa dist,src=0,dst=0,val=10 -numa dist,src=0,dst=1,val=40 -numa dist,src=0,dst=2,val=20 -numa dist,src=0,dst=3,val=30 \
		-numa dist,src=1,dst=0,val=40 -numa dist,src=1,dst=1,val=10 -numa dist,src=1,dst=2,val=30 -numa dist,src=1,dst=3,val=20 \
		-numa dist,src=2,dst=0,val=20 -numa dist,src=2,dst=1,val=30 -numa dist,src=2,dst=2,val=10 -numa dist,src=2,dst=3,val=40 \
		-numa dist,src=3,dst=0,val=30 -numa dist,src=3,dst=1,val=20 -numa dist,src=3,dst=2,val=40 -numa dist,src=3,dst=3,val=10 \
		\
		`# --- 6. 核心总线与中断控制器 ---` \
		-device intel-iommu,intremap=on,caching-mode=on \
		-device qemu-xhci,id=xhci,msi=on,msix=on,p2=8,p3=8 \
		\
		`# --- 7. 启动盘 (BOT, 根端口 1) ---` \
		-drive if=none,id=bootdisk,format=raw,file=fat:rw:./esp \
		-device usb-storage,drive=bootdisk,bus=xhci.0,port=1,bootindex=1 \
		\
		`# --- 8. 其他 USB 测试设备 ---` \
		-drive if=none,id=uas_backend,format=raw,file=/home/wind3/disk-uas.img \
		-device usb-uas,id=uas_dev,bus=xhci.0,port=2 \
		-device scsi-hd,bus=uas_dev.0,scsi-id=0,lun=0,drive=uas_backend \
		-device usb-kbd,id=vkbd,bus=xhci.0,port=3 \
		-device usb-mouse,id=mouse1,bus=xhci.0,port=4 \
		-device usb-hub,id=ext_hub,bus=xhci.0,port=5 \
		-drive if=none,id=hub_bot_disk,format=raw,file=/home/wind3/disk-bot.img \
		-device usb-storage,drive=hub_bot_disk,bus=xhci.0,port=5.1 \
		-drive if=none,id=hotplug_disk,format=raw,file=/home/wind3/disk-bot.img &

qemu-monitor:
	telnet localhost 4444

clean_all:
	-rm -rf build esp/efi esp/kernel.*
	-mkdir -p build esp/efi/boot

clean_kernel:
	-rm -rf build/* esp/kernel.*

clean_uefi:
	-rm -rf build/DEBUG_GCC build/RELEASE_GCC  esp/efi/boot/bootx64.efi


