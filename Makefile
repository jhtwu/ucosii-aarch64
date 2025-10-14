# ======================================================================================
# Toolchain Configuration / 編譯工具鏈設定
# Defines the cross-compilation tools required to build the bare-metal AArch64 firmware.
# 定義建置裸機 AArch64 韌體所需的交叉編譯工具。
# ======================================================================================
TOOLCHAIN = aarch64-linux-gnu
# TOOLCHAIN = aarch64-thunderx-elf
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
CFLAGS = -std=gnu11 -w -nostartfiles -fno-exceptions -ffreestanding -fno-builtin -fno-stack-protector -mcpu=$(CORE) -static -g -I$(SRCDIR) -fstack-usage
AFLAGS = -g -I$(SRCDIR)
LINKER = $(CC) -o
LFLAGS = -w -T $(LFILE) -nostartfiles -nostdlib -fno-exceptions -mcpu=$(CORE) -static -g

# ======================================================================================
# Debugging / Emulation Tools / 除錯與模擬工具設定
# These options consolidate the behaviors formerly encoded in shell scripts.
# 下列參數整合原本 shell 腳本中的設定，方便統一管理。
# ======================================================================================
GDB             = $(TOOLCHAIN)-gdb
QEMU            = qemu-system-aarch64
QEMU_IMAGE      = $(BINDIR)/$(TARGET)
QEMU_BASE_FLAGS = -M virt,gic_version=3 -nographic -serial mon:stdio
QEMU_RUN_SMP    = 4
QEMU_RUN_MEMORY = 2048M
QEMU_SOFT_FLAGS = -cpu $(CORE) -smp $(QEMU_RUN_SMP) -m $(QEMU_RUN_MEMORY) -global virtio-mmio.force-legacy=false
QEMU_KVM_FLAGS  = -cpu host --enable-kvm -m 256M
QEMU_GDB_FLAGS  = -gdb tcp::2222 -S
QEMU_BRIDGE_TAP = qemu-lan
QEMU_BRIDGE_MAC = 52:54:00:12:34:56
QEMU_WAN_TAP    = qemu-wan
QEMU_WAN_MAC    = 52:54:00:65:43:21
QEMU_USER_NET_FLAGS = -netdev user,id=net0 -device virtio-net-device,netdev=net0
QEMU_BRIDGE_FLAGS = -netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC)
QEMU_WAN_BRIDGE_FLAGS = -netdev tap,id=net0,ifname=$(QEMU_WAN_TAP),script=no,downscript=no -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_WAN_MAC)
NET_MODE ?= bridge

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
APP_SRC      = $(SRCDIR)/app.c
APP_OBJECT   = $(OBJDIR)/$(notdir $(APP_SRC:.c=.o))
CORE_OBJECTS = $(filter-out $(APP_OBJECT), $(OBJECTS_ALL))
TESTDIR            = test
TEST_OBJDIR        = test_build
TEST_SUPPORT       = test_support
TEST_NAMES         = test_context_timer test_network_ping test_network_ping_wan test_udp_flood
TEST_SUPPORT_OBJ   = $(addprefix $(TEST_OBJDIR)/,$(addsuffix .o,$(TEST_SUPPORT)))
TEST_PROGRAM_OBJS  = $(addprefix $(TEST_OBJDIR)/,$(addsuffix .o,$(TEST_NAMES)))
TEST_CONTEXT_NAME  = test_context_timer
TEST_PING_NAME     = test_network_ping
TEST_PING_WAN_NAME = test_network_ping_wan
TEST_BINDIR        = test_bin
TEST_CONTEXT_BIN   = $(TEST_BINDIR)/$(TEST_CONTEXT_NAME).elf
TEST_PING_BIN      = $(TEST_BINDIR)/$(TEST_PING_NAME).elf
TEST_PING_WAN_BIN  = $(TEST_BINDIR)/$(TEST_PING_WAN_NAME).elf
TEST_CFLAGS        = $(filter-out -fstack-usage,$(CFLAGS))
rm           = rm -f

# ======================================================================================
# Phony Targets / 虛擬目標宣告
# ======================================================================================
.PHONY: all clean remove run run-kvm qemu qemu_gdb qemu-gdb gdb dqemu setup-network help test test-context test-ping test-ping-wan test-dual

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

$(TEST_OBJDIR)/%.o: $(TESTDIR)/%.c | $(TEST_OBJDIR)
	@$(CC) $(TEST_CFLAGS) -c $< -o $@
	@echo "Compiled $< successfully!"

$(TEST_OBJDIR):
	@mkdir -p $@

$(TEST_BINDIR)/test_%.elf: $(CORE_OBJECTS) $(TEST_SUPPORT_OBJ) $(TEST_OBJDIR)/test_%.o
	@mkdir -p $(@D)
	@$(LINKER) $@ $(LFLAGS) $(CORE_OBJECTS) $(TEST_SUPPORT_OBJ) $(TEST_OBJDIR)/test_$*.o -lgcc
	@echo "Built $@"

# ======================================================================================
# QEMU Convenience Targets / QEMU 便利目標
# ======================================================================================

# Run under software emulation / 使用純軟體模擬執行
run: $(BINDIR)/$(TARGET)
ifeq ($(NET_MODE),bridge)
	@echo "Launching QEMU (bridge networking) / 啟動 QEMU（橋接網路）"
	@if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "ERROR: TAP interface '$(QEMU_BRIDGE_TAP)' not found. Please create it before running."; \
		exit 1; \
	fi
	@echo "Using existing tap interface: $(QEMU_BRIDGE_TAP)"
	@if command -v brctl >/dev/null 2>&1; then \
		echo "Bridge status:"; \
		brctl show br-lan | grep -A 1 "bridge name" || brctl show br-lan; \
	else \
		echo "brctl not available; skipping bridge status output."; \
	fi
	timeout --foreground 60s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(QEMU_IMAGE)
else
	@echo "Launching QEMU (user-mode networking) / 啟動 QEMU（使用 user-mode 網路）"
	$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_USER_NET_FLAGS) -kernel $(QEMU_IMAGE)
endif

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

test: test-context test-ping test-ping-wan test-udp test-dual

test-context: $(TEST_CONTEXT_BIN)
	@echo "========================================="
	@echo "Running Test Case 1: Context Switch & Timer"
	@echo "========================================="
	@status=0; \
	output=$$(timeout --foreground 20s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -kernel $(TEST_CONTEXT_BIN) 2>&1) || status=$$?; \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	elif [ $$status -eq 124 ]; then \
		echo ""; echo "⚠ TEST TIMED OUT (no PASS marker)"; exit 1; \
	else \
		exit $$status; \
	fi

test-ping: $(TEST_PING_BIN)
	@echo "========================================="
	@echo "Running Test Case 2: Network Ping"
	@echo "========================================="
	if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_BRIDGE_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground 15s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(TEST_PING_BIN) 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
	fi; \
	if echo "$$output" | grep -q "Could not set up host tap"; then \
		echo "[FAIL] Unable to access TAP interface '$(QEMU_BRIDGE_TAP)'"; \
		echo "      Ensure it exists and is owned by $$USER:"; \
		echo "      sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		echo "      sudo ip link set $(QEMU_BRIDGE_TAP) up"; \
		exit 1; \
	fi; \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	elif [ $$status -eq 124 ]; then \
		echo ""; echo "⚠ TEST TIMED OUT (no PASS marker)"; exit 1; \
	else \
		exit $$status; \
	fi

test-ping-wan: $(TEST_PING_WAN_BIN)
	@echo "========================================="
	@echo "Running Test Case 2b: WAN Network Ping"
	@echo "========================================="
	if ! ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_WAN_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_WAN_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground 15s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_WAN_BRIDGE_FLAGS) -kernel $(TEST_PING_WAN_BIN) 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
	fi; \
	if echo "$$output" | grep -q "Could not set up host tap"; then \
		echo "[FAIL] Unable to access TAP interface '$(QEMU_WAN_TAP)'"; \
		echo "      Ensure it exists and is owned by $$USER:"; \
		echo "      sudo ip tuntap add dev $(QEMU_WAN_TAP) mode tap user $$USER"; \
		echo "      sudo ip link set $(QEMU_WAN_TAP) up"; \
		exit 1; \
	fi; \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	elif [ $$status -eq 124 ]; then \
		echo ""; echo "⚠ TEST TIMED OUT (no PASS marker)"; exit 1; \
	else \
		exit $$status; \
	fi

test-dual: $(BINDIR)/$(TARGET)
	@echo "========================================="
	@echo "Running Dual NIC Ping Diagnostics"
	@echo "========================================="
	@if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "ERROR: TAP interface '$(QEMU_BRIDGE_TAP)' not found. Please create it before running."; \
		exit 1; \
	fi
	@if ! ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "ERROR: TAP interface '$(QEMU_WAN_TAP)' not found. Please create it before running."; \
		exit 1; \
	fi
	@if command -v brctl >/dev/null 2>&1; then \
		echo "Bridge status:"; \
		brctl show br-lan | grep -A 1 "bridge name" || brctl show br-lan; \
		brctl show br-wan | grep -A 1 "bridge name" || brctl show br-wan; \
	else \
		echo "brctl not available; skipping bridge status output."; \
	fi
	timeout --foreground 60s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) \
		-netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC) \
		-netdev tap,id=net1,ifname=$(QEMU_WAN_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=$(QEMU_WAN_MAC) \
		-kernel $(QEMU_IMAGE)

test-udp: $(TEST_BINDIR)/test_udp_flood.elf
	@echo "========================================="
	@echo "Running Test Case 3: UDP Flood"
	@echo "========================================="
	if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_BRIDGE_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground 15s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(TEST_BINDIR)/test_udp_flood.elf 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
	fi; \
	if echo "$$output" | grep -q "Could not set up host tap"; then \
		echo "[FAIL] Unable to access TAP interface '$(QEMU_BRIDGE_TAP)'"; \
		echo "      Ensure it exists and is owned by $$USER:"; \
		echo "      sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		echo "      sudo ip link set $(QEMU_BRIDGE_TAP) up"; \
		exit 1; \
	fi; \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	elif [ $$status -eq 124 ]; then \
		echo ""; echo "⚠ TEST TIMED OUT (no PASS marker)"; exit 1; \
	else \
		exit $$status; \
	fi

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
	@rm -rf $(TEST_OBJDIR)
	@rm -rf $(TEST_BINDIR)
	@$(rm) os.list
	@echo "Cleanup complete!"

# Remove build outputs / 移除建置產物
remove: clean
	@$(rm) $(BINDIR)/$(TARGET)
	@echo "Executable removed!"
