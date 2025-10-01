# ======================================================================================
# Toolchain Configuration / 編譯工具鏈設定
# Defines the cross-compilation tools required to build the bare-metal AArch64 firmware.
# 定義建置裸機 AArch64 韌體所需的交叉編譯工具。
# ======================================================================================
TOOLCHAIN = aarch64-thunderx-elf
# TOOLCHAIN = /home/dylan/desktop/kvm/buildroot/output/host/bin/aarch64-buildroot-linux-uclibc
TARGET    = kernel.elf
ARMARCH   = armv8-a
CORE      = cortex-a57
CC        = $(TOOLCHAIN)-gcc
AS        = $(TOOLCHAIN)-as
SIZE      = $(TOOLCHAIN)-size
DUMP      = $(TOOLCHAIN)-objdump
OBJCOPY   = $(TOOLCHAIN)-objcopy

# ======================================================================================
# Directory Layout / 目錄配置
# SRCDIR: source tree / 原始碼所在位置
# OBJDIR: intermediate objects / 中繼目標檔
# BINDIR: final binaries / 最終可執行檔
# ======================================================================================
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# ======================================================================================
# Build Flags / 編譯與連結參數
# Collect compiler, assembler, and linker options for the firmware image.
# 彙整編譯器、組譯器與連結器所需的參數。
# ======================================================================================
LFILE  = $(SRCDIR)/linker.ld
CFLAGS = -std=gnu11 -w -nostartfiles -fno-exceptions -mcpu=$(CORE) -static -g -I$(SRCDIR) -fstack-usage -mabi=ilp32
AFLAGS = -g -mabi=ilp32 -I$(SRCDIR)
LINKER = $(CC) -o
LFLAGS = -w -T $(LFILE) -nostartfiles -fno-exceptions -mcpu=$(CORE) -static -g -lc -mabi=ilp32

# ======================================================================================
# Debugging / Emulation Tools / 除錯與模擬工具設定
# These options consolidate the behaviors formerly encoded in shell scripts.
# 下列參數整合原本 shell 腳本中的設定，方便統一管理。
# ======================================================================================
GDB             = $(TOOLCHAIN)-gdb
QEMU            = qemu-system-aarch64
QEMU_IMAGE      = $(BINDIR)/$(TARGET)
QEMU_BASE_FLAGS = -M virt,gic_version=3 -nographic -serial mon:stdio
QEMU_SOFT_FLAGS = -cpu $(CORE) -m 384M
QEMU_KVM_FLAGS  = -cpu host --enable-kvm -m 256M
QEMU_GDB_FLAGS  = -gdb tcp::2222 -S

# ======================================================================================
# Source Discovery / 原始碼蒐集
# Automatically gather C and assembly sources for the build.
# 自動蒐集 C 及組合語言檔案以便建置。
# ======================================================================================
C_FILES      := $(wildcard $(SRCDIR)/*.c)
AS_FILES     := $(wildcard $(SRCDIR)/*.S)
OBJECTS_C    := $(addprefix $(OBJDIR)/,$(notdir $(C_FILES:.c=.o)))
OBJECTS_S    := $(addprefix $(OBJDIR)/,$(notdir $(AS_FILES:.S=.o)))
OBJECTS_ALL  := $(OBJECTS_S) $(OBJECTS_C)
rm           = rm -f

# ======================================================================================
# Phony Targets / 虛擬目標宣告
# ======================================================================================
.PHONY: all clean remove run run-kvm qemu qemu_gdb qemu-gdb gdb dqemu setup-network help

# ======================================================================================
# Default Build Target / 預設建置目標
# ======================================================================================
all: $(BINDIR)/$(TARGET)

# ======================================================================================
# Build Rules / 建置規則
# ======================================================================================

$(BINDIR)/$(TARGET): remove $(OBJECTS_ALL)
	@mkdir -p $(@D)
	@$(LINKER) $@ $(LFLAGS) $(OBJECTS_ALL)
	@echo "Linking complete!"
	@$(SIZE) $@
	$(DUMP) -S $(BINDIR)/$(TARGET) > os.list
	@if [ -d "/srv/tftpboot/" ]; then cp $(BINDIR)/$(TARGET) /srv/tftpboot/; echo "cp $(BINDIR)/$(TARGET) /srv/tftpboot/"; fi

$(OBJECTS_ALL): | $(OBJDIR)

$(OBJDIR):
	@mkdir -p $@

# C source compilation / C 原始碼編譯
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled $< successfully!"

# Assembly source compilation / 組合語言原始碼編譯
$(OBJDIR)/%.o: $(SRCDIR)/%.S
	@$(AS) $(AFLAGS) $< -o $@
	@echo "Assembled $< successfully!"

# ======================================================================================
# QEMU Convenience Targets / QEMU 便利目標
# ======================================================================================

# Run under software emulation / 使用純軟體模擬執行
run: $(BINDIR)/$(TARGET)
	@echo "Launching QEMU (software emulation) / 啟動 QEMU（純軟體模擬）"
	$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -kernel $(QEMU_IMAGE)

# Run with KVM acceleration / 啟動 KVM 加速模擬
run-kvm: $(BINDIR)/$(TARGET)
	@echo "Launching QEMU with KVM acceleration / 啟動支援 KVM 的 QEMU"
	$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_KVM_FLAGS) -kernel $(QEMU_IMAGE)

# Backward-compatible aliases / 向後相容別名
qemu: run
qemu_gdb: qemu-gdb

# Run QEMU and wait for GDB connection / 啟動 QEMU 並等待 GDB 連線
qemu-gdb: $(BINDIR)/$(TARGET)
	@echo "Launching QEMU in GDB wait mode / 啟動 QEMU 並等待 GDB 連線"
	$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -kernel $(QEMU_IMAGE) $(QEMU_GDB_FLAGS)

# Launch GDB connected to the built image / 啟動 GDB 並載入建置產物
gdb: $(OBJECTS_ALL)
	$(GDB) $(BINDIR)/$(TARGET)

# Launch QEMU with default debug server / 啟動含預設偵錯伺服器的 QEMU
dqemu: $(BINDIR)/$(TARGET)
	@echo "Launching QEMU with -s -S debug server / 使用 -s -S 啟動 QEMU"
	$(QEMU) -s -S $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -kernel $(QEMU_IMAGE)

# ======================================================================================
# Utility Targets / 其他常用目標
# ======================================================================================

# Setup host-side tap/bridge network for QEMU / 建立 QEMU 使用的橋接網路與 TAP 介面
setup-network:
	@echo "Configuring host network bridges for QEMU / 建立 QEMU 使用的橋接網路"
	sudo ip link add br-lan type bridge
	sudo ip tuntap add tap-lan mode tap user root
	sudo brctl addif br-lan enp0s3
	sudo brctl addif br-lan tap-lan
	sudo ip addr flush dev enp0s3
	sudo ifconfig br-lan 192.168.1.134
	sudo ifconfig br-lan up
	sudo ifconfig tap-lan up
	sudo ip link add br-wan type bridge
	sudo ip tuntap add tap-wan mode tap user root
	sudo brctl addif br-wan enp0s8
	sudo brctl addif br-wan tap-wan
	sudo ip addr flush dev enp0s3
	sudo ifconfig br-wan 172.17.5.134
	sudo ifconfig br-wan up
	sudo ifconfig tap-wan up

# User guidance / 使用說明
help:
	@echo "Available targets / 可用目標:"
	@echo "  make            - Build firmware / 編譯韌體"
	@echo "  make run        - Run in QEMU software emulation / 於 QEMU 軟體模擬執行"
	@echo "  make run-kvm    - Run with KVM acceleration / 以 KVM 加速執行"
	@echo "  make qemu-gdb   - Run QEMU and wait for GDB / 啟動 QEMU 並等待 GDB"
	@echo "  make gdb        - Launch GDB / 啟動 GDB"
	@echo "  make dqemu      - Run QEMU with default debug server / 預設偵錯模式"
	@echo "  make setup-network - Prepare host bridges / 建立主機橋接網路"
	@echo "  make clean      - Remove build artifacts / 清除建置產物"
	@echo "  make remove     - Remove binaries and objects / 移除可執行檔與目標檔"

# Clean up intermediate files / 清除暫存檔案
clean:
	@$(rm) $(OBJECTS_ALL)
	@$(rm) -rf $(BINDIR)/*
	@$(rm) -rf $(OBJDIR)
	@$(rm) os.list
	@echo "Cleanup complete!"

# Remove build outputs / 移除建置產物
remove: clean
	@$(rm) $(BINDIR)/$(TARGET)
	@echo "Executable removed!"
