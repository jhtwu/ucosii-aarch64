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
# Platform Configuration / 平台設定
# ======================================================================================
GIC_VERSION ?= 2

# Detect host architecture / 檢測主機架構
HOST_ARCH := $(shell uname -m)

# Check KVM availability / 檢查 KVM 可用性
KVM_AVAILABLE := $(shell [ -r /dev/kvm ] && [ -w /dev/kvm ] && echo yes || echo no)

# Check vhost-net availability for network acceleration / 檢查 vhost-net 網路加速可用性
VHOST_AVAILABLE := $(shell [ -r /dev/vhost-net ] && [ -w /dev/vhost-net ] && echo yes || echo no)

# Configure CPU and acceleration based on host architecture / 根據主機架構配置 CPU 和加速
ifeq ($(HOST_ARCH),aarch64)
    # On ARM64 host, check if KVM is available / ARM64 主機檢查 KVM 是否可用
    ifeq ($(KVM_AVAILABLE),yes)
        QEMU_CPU_FLAGS = -cpu host --enable-kvm
        PLATFORM_DESC = ARM64 host with KVM acceleration
    else
        QEMU_CPU_FLAGS = -cpu $(CORE)
        PLATFORM_DESC = ARM64 host (KVM unavailable, using software emulation)
        KVM_WARNING = yes
    endif
else
    # On other hosts (e.g., x86_64), use software emulation / 其他主機（如 x86_64）使用軟體模擬
    QEMU_CPU_FLAGS = -cpu $(CORE)
    PLATFORM_DESC = $(HOST_ARCH) with software emulation
endif

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
CFLAGS = -O3 -flto -fomit-frame-pointer -march=armv8-a+simd -ffunction-sections -fdata-sections -finline-functions -D_FORTIFY_SOURCE=0 -std=gnu11 -w -nostartfiles -fno-exceptions -ffreestanding -fno-builtin -fno-stack-protector -mcpu=$(CORE) -static -g -I$(SRCDIR) -fstack-usage
AFLAGS = -g -I$(SRCDIR)
LINKER = $(CC) -o
LFLAGS = -w -T $(LFILE) -nostartfiles -nostdlib -fno-exceptions -mcpu=$(CORE) -static -g -flto -Wl,--gc-sections

# ======================================================================================
# Debugging / Emulation Tools / 除錯與模擬工具設定
# These options consolidate the behaviors formerly encoded in shell scripts.
# 下列參數整合原本 shell 腳本中的設定，方便統一管理。
# ======================================================================================
GDB             = $(TOOLCHAIN)-gdb
QEMU            = qemu-system-aarch64
QEMU_IMAGE      = $(BINDIR)/$(TARGET)
QEMU_BASE_FLAGS = -M virt,gic_version=$(GIC_VERSION) -nographic -serial mon:stdio
QEMU_RUN_SMP    = 1
QEMU_RUN_MEMORY = 2048M
QEMU_SOFT_FLAGS = $(QEMU_CPU_FLAGS) -smp $(QEMU_RUN_SMP) -m $(QEMU_RUN_MEMORY) -global virtio-mmio.force-legacy=false
QEMU_GDB_FLAGS  = -gdb tcp::2222 -S
QEMU_BRIDGE_TAP = qemu-lan
QEMU_BRIDGE_MAC = 52:54:00:12:34:56
QEMU_WAN_TAP    = qemu-wan
QEMU_WAN_MAC    = 52:54:00:65:43:21

# Network performance tuning parameters / 網路效能調校參數
# For KVM with vhost-net: use vhost=on for kernel-space packet processing / KVM 配合 vhost-net 使用核心空間封包處理
# queues=N: enable multi-queue (requires multi_queue TAP interfaces) / 啟用多佇列（需要 multi_queue TAP 介面）
# For virtio-net-device: mrg_rxbuf, packed, event_idx, tx/rx_queue_size / virtio-net-device 效能參數

# Multi-queue support (auto-detected from TAP interfaces, or set manually) / 多佇列支援（自動偵測或手動設定）
# To create multi-queue TAP: sudo ip tuntap add dev tap-name mode tap multi_queue user $USER
# Auto-detect multi-queue support from TAP interface / 自動偵測 TAP 介面的 multi-queue 支援
TAP_HAS_MQ := $(shell ip tuntap show 2>/dev/null | grep -E '($(QEMU_BRIDGE_TAP)|$(QEMU_WAN_TAP))' | grep -q multi_queue && echo yes || echo no)

# Set default VIRTIO_QUEUES based on TAP capability / 根據 TAP 能力設定預設佇列數
ifeq ($(TAP_HAS_MQ),yes)
    VIRTIO_QUEUES ?= 4
else
    VIRTIO_QUEUES ?= 1
endif

ifeq ($(KVM_AVAILABLE)$(VHOST_AVAILABLE),yesyes)
    # Best performance: KVM + vhost-net / 最佳效能：KVM + vhost-net
    ifeq ($(VIRTIO_QUEUES),1)
        NETDEV_PERF_FLAGS = vhost=on
        VIRTIO_PERF_FLAGS = mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
        NET_ACCEL_STATUS = KVM with vhost-net acceleration
    else
        NETDEV_PERF_FLAGS = vhost=on,queues=$(VIRTIO_QUEUES)
        VIRTIO_PERF_FLAGS = mq=on,mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
        NET_ACCEL_STATUS = KVM with vhost-net and $(VIRTIO_QUEUES)-queue
    endif
else ifeq ($(KVM_AVAILABLE),yes)
    # KVM without vhost-net: still use optimized buffers / KVM 但無 vhost-net：仍使用優化緩衝區
    NETDEV_PERF_FLAGS =
    VIRTIO_PERF_FLAGS = mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
    NET_ACCEL_STATUS = KVM (vhost-net unavailable)
    VHOST_WARNING = yes
else
    # Software emulation: conservative settings / 軟體模擬：保守設定
    # Note: Use tx/rx_queue_size=256 for compatibility with older QEMU versions
    NETDEV_PERF_FLAGS =
    VIRTIO_PERF_FLAGS = mrg_rxbuf=on,event_idx=on,tx_queue_size=256,rx_queue_size=256
    NET_ACCEL_STATUS = Software emulation
endif

QEMU_USER_NET_FLAGS = -netdev user,id=net0 -device virtio-net-device,netdev=net0
QEMU_BRIDGE_FLAGS = -netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no,$(NETDEV_PERF_FLAGS) -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC),$(VIRTIO_PERF_FLAGS)
QEMU_WAN_BRIDGE_FLAGS = -netdev tap,id=net0,ifname=$(QEMU_WAN_TAP),script=no,downscript=no,$(NETDEV_PERF_FLAGS) -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_WAN_MAC),$(VIRTIO_PERF_FLAGS)
QEMU_RUN_TIMEOUT ?= 10
NET_MODE ?= bridge

# Helper for comma in conditionals / 條件式中使用逗號的輔助變數
comma := ,

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
TEST_NAMES         = test_context_timer test_network_init test_network_ping test_network_ping_lan test_network_ping_wan test_udp_flood test_nat_icmp test_nat_udp
TEST_SUPPORT_OBJ   = $(addprefix $(TEST_OBJDIR)/,$(addsuffix .o,$(TEST_SUPPORT)))
TEST_PROGRAM_OBJS  = $(addprefix $(TEST_OBJDIR)/,$(addsuffix .o,$(TEST_NAMES)))
TEST_CONTEXT_NAME  = test_context_timer
TEST_PING_NAME     = test_network_ping
TEST_PING_LAN_NAME = test_network_ping_lan
TEST_PING_WAN_NAME = test_network_ping_wan
TEST_NET_INIT_NAME = test_network_init
TEST_NAT_ICMP_NAME = test_nat_icmp
TEST_NAT_UDP_NAME  = test_nat_udp
TEST_BINDIR        = test_bin
TEST_CONTEXT_BIN   = $(TEST_BINDIR)/$(TEST_CONTEXT_NAME).elf
TEST_PING_BIN      = $(TEST_BINDIR)/$(TEST_PING_NAME).elf
TEST_PING_LAN_BIN  = $(TEST_BINDIR)/$(TEST_PING_LAN_NAME).elf
TEST_PING_WAN_BIN  = $(TEST_BINDIR)/$(TEST_PING_WAN_NAME).elf
TEST_NET_INIT_BIN  = $(TEST_BINDIR)/$(TEST_NET_INIT_NAME).elf
TEST_NAT_ICMP_BIN  = $(TEST_BINDIR)/$(TEST_NAT_ICMP_NAME).elf
TEST_NAT_UDP_BIN   = $(TEST_BINDIR)/$(TEST_NAT_UDP_NAME).elf
TEST_CFLAGS        = $(filter-out -fstack-usage,$(CFLAGS))
rm           = rm -f

# ======================================================================================
# Phony Targets / 虛擬目標宣告
# ======================================================================================
.PHONY: all clean remove run qemu qemu_gdb qemu-gdb gdb dqemu setup-network setup-mq-tap help test test-context test-net-init test-ping test-ping-lan test-ping-wan test-dual test-nat-icmp test-nat-udp

# ======================================================================================
# Default Build Target / 預設建置目標
# ======================================================================================
all: $(BINDIR)/$(TARGET)

# ======================================================================================
# Build Rules / 建置規則
# ======================================================================================

$(BINDIR)/$(TARGET): $(OBJECTS_ALL)
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
	@echo "=== Platform: $(PLATFORM_DESC) ==="
	@echo "=== GIC Version: $(GIC_VERSION) ==="
	@echo "=== Network: $(NET_ACCEL_STATUS) ==="
	@echo "=== VirtIO Queues: $(VIRTIO_QUEUES) (TAP multi-queue: $(TAP_HAS_MQ)) ==="
ifeq ($(KVM_WARNING),yes)
	@echo ""
	@echo "⚠️  WARNING: KVM is not available"
	@echo "    To enable KVM acceleration, run one of:"
	@echo "    1. sudo usermod -aG kvm $$USER  (then log out/in)"
	@echo "    2. sudo chmod 666 /dev/kvm"
	@echo "    3. Run with: sudo make run"
	@echo ""
endif
ifeq ($(VHOST_WARNING),yes)
	@echo ""
	@echo "⚠️  WARNING: vhost-net is not available (TCP performance will be limited)"
	@echo "    To enable vhost-net acceleration, run one of:"
	@echo "    1. sudo usermod -aG kvm $$USER  (then log out/in)"
	@echo "    2. sudo chmod 666 /dev/vhost-net"
	@echo "    3. Run with: sudo make run"
	@echo ""
endif
ifeq ($(NET_MODE),bridge)
	@echo "Launching QEMU (dual NIC bridge networking for NAT) / 啟動 QEMU（雙網卡橋接用於 NAT）"
	@if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "ERROR: TAP interface '$(QEMU_BRIDGE_TAP)' not found. Please create it before running."; \
		exit 1; \
	fi
	@if ! ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "ERROR: TAP interface '$(QEMU_WAN_TAP)' not found. Please create it before running."; \
		exit 1; \
	fi
	@# Validate TAP and QEMU configuration compatibility / 驗證 TAP 和 QEMU 設定相容性
	@if [ "$(VIRTIO_QUEUES)" != "1" ] && [ "$(TAP_HAS_MQ)" != "yes" ]; then \
		echo ""; \
		echo "ERROR: VIRTIO_QUEUES=$(VIRTIO_QUEUES) requires multi-queue TAP interfaces"; \
		echo "       但 TAP 介面不支援 multi-queue"; \
		echo ""; \
		echo "解決方法："; \
		echo "  1. 重新建立 multi-queue TAP: make setup-mq-tap"; \
		echo "  2. 或使用 single-queue 模式: make run VIRTIO_QUEUES=1"; \
		echo ""; \
		exit 1; \
	fi
	@if [ "$(VIRTIO_QUEUES)" = "1" ] && [ "$(TAP_HAS_MQ)" = "yes" ]; then \
		echo ""; \
		echo "ERROR: TAP 介面是 multi-queue，不能使用 VIRTIO_QUEUES=1"; \
		echo "       Multi-queue TAP interface cannot be used in single-queue mode"; \
		echo ""; \
		echo "解決方法："; \
		echo "  1. 使用 multi-queue 模式: make run (auto-detects) 或 make run VIRTIO_QUEUES=4"; \
		echo "  2. 或重新建立 single-queue TAP: sudo ip link del qemu-lan && sudo ip link del qemu-wan"; \
		echo "     然後: sudo ip tuntap add dev qemu-lan mode tap user $$USER"; \
		echo "           sudo ip tuntap add dev qemu-wan mode tap user $$USER"; \
		echo ""; \
		exit 1; \
	fi
	@echo "Using tap interfaces: $(QEMU_BRIDGE_TAP) (LAN), $(QEMU_WAN_TAP) (WAN)"
	timeout --foreground 60s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) \
		-netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no$(if $(NETDEV_PERF_FLAGS),$(comma)$(NETDEV_PERF_FLAGS)) \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC),$(VIRTIO_PERF_FLAGS) \
		-netdev tap,id=net1,ifname=$(QEMU_WAN_TAP),script=no,downscript=no$(if $(NETDEV_PERF_FLAGS),$(comma)$(NETDEV_PERF_FLAGS)) \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=$(QEMU_WAN_MAC),$(VIRTIO_PERF_FLAGS) \
		-kernel $(QEMU_IMAGE)
else
	@echo "Launching QEMU (user-mode networking) / 啟動 QEMU（使用 user-mode 網路）"
	$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_USER_NET_FLAGS) -kernel $(QEMU_IMAGE)
endif

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

test: test-context test-ping test-ping-wan

test-context: $(TEST_CONTEXT_BIN)
	@echo "========================================="
	@echo "Running Test Case 1: Context Switch & Timer"
	@echo "========================================="
	@status=0; \
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -kernel $(TEST_CONTEXT_BIN) 2>&1) || status=$$?; \
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

test-net-init: $(TEST_NET_INIT_BIN)
	@echo "========================================="
	@echo "Running Test Case: VirtIO Network Init"
	@echo "========================================="
	@for i in 1 2 3; do \
		printf "\n--- Run %d ---\n" "$$i"; \
		status=0; \
		output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) -netdev user,id=net0 -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC) -kernel $(TEST_NET_INIT_BIN) 2>&1) || status=$$?; \
		echo "$$output"; \
		if echo "$$output" | grep -q "\[PASS\]"; then \
			printf "Run %d: PASS\n" "$$i"; \
		else \
			printf "Run %d: FAIL\n" "$$i"; \
			exit 1; \
		fi; \
		done; \
	echo ""; \
	echo "✓ ALL RUNS PASSED";

test-ping-lan: $(TEST_PING_LAN_BIN)
	@echo "========================================="
	@echo "Running Test Case: LAN Ping (192.168.1.1 -> 192.168.1.103)"
	@echo "========================================="
	if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_BRIDGE_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		echo "      Note: This test requires TAP bridge networking"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(TEST_PING_LAN_BIN) 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
	fi; \
	if echo "$$output" | grep -q "Could not set up host tap"; then \
		echo "[FAIL] Unable to access TAP interface '$(QEMU_BRIDGE_TAP)'"; \
		echo "      Ensure it exists and is configured properly"; \
		exit 1; \
	fi; \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ LAN PING TEST PASSED - 192.168.1.103 is reachable from 192.168.1.1"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ LAN PING TEST FAILED - 192.168.1.103 is unreachable from 192.168.1.1"; exit 1; \
	elif [ $$status -eq 124 ]; then \
		echo ""; echo "⚠ LAN PING TEST TIMED OUT (no PASS marker)"; exit 1; \
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
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(TEST_PING_BIN) 2>&1) || status=$$?; \
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
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_WAN_BRIDGE_FLAGS) -kernel $(TEST_PING_WAN_BIN) 2>&1) || status=$$?; \
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
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) $(QEMU_BRIDGE_FLAGS) -kernel $(TEST_BINDIR)/test_udp_flood.elf 2>&1) || status=$$?; \
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

test-nat-icmp: $(TEST_NAT_ICMP_BIN)
	@echo "========================================="
	@echo "Running Test Case: NAT ICMP Forwarding"
	@echo "========================================="
	if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_BRIDGE_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	if ! ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_WAN_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_WAN_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) \
		-netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC) \
		-netdev tap,id=net1,ifname=$(QEMU_WAN_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=$(QEMU_WAN_MAC) \
		-kernel $(TEST_NAT_ICMP_BIN) 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
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

# Setup multi-queue TAP interfaces for better performance / 建立支援多佇列的 TAP 介面以獲得更好效能
setup-mq-tap:
	@echo "Creating multi-queue TAP interfaces for $(QEMU_BRIDGE_TAP) and $(QEMU_WAN_TAP)..."
	@echo "This will recreate the TAP interfaces with multi_queue support."
	@echo ""
	@if ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "Removing existing $(QEMU_BRIDGE_TAP)..."; \
		sudo ip link delete $(QEMU_BRIDGE_TAP); \
	fi
	@if ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "Removing existing $(QEMU_WAN_TAP)..."; \
		sudo ip link delete $(QEMU_WAN_TAP); \
	fi
	@echo "Creating $(QEMU_BRIDGE_TAP) with multi_queue..."
	sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap multi_queue user $$USER
	sudo ip link set $(QEMU_BRIDGE_TAP) up
	@echo "Creating $(QEMU_WAN_TAP) with multi_queue..."
	sudo ip tuntap add dev $(QEMU_WAN_TAP) mode tap multi_queue user $$USER
	sudo ip link set $(QEMU_WAN_TAP) up
	@echo ""
	@echo "✓ Multi-queue TAP interfaces created successfully!"
	@echo "  You can now run: make run VIRTIO_QUEUES=4"
	@echo ""
	@echo "To verify, run: ip tuntap show | grep -E '($(QEMU_BRIDGE_TAP)|$(QEMU_WAN_TAP))'"

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
	@echo "  make run        - Run in QEMU (auto-detects KVM on ARM64) / 於 QEMU 執行（ARM64 自動偵測 KVM）"
	@echo "  make qemu-gdb   - Run QEMU and wait for GDB / 啟動 QEMU 並等待 GDB"
	@echo "  make gdb        - Launch GDB / 啟動 GDB"
	@echo "  make dqemu      - Run QEMU with default debug server / 預設偵錯模式"
	@echo "  make setup-network - Prepare host bridges / 建立主機橋接網路"
	@echo "  make setup-mq-tap  - Create multi-queue TAP interfaces / 建立多佇列 TAP 介面"
	@echo "  make clean      - Remove build artifacts / 清除建置產物"
	@echo "  make remove     - Remove binaries and objects / 移除可執行檔與目標檔"
	@echo ""
	@echo "Network performance options / 網路效能選項:"
	@echo "  VIRTIO_QUEUES=N - Set number of virtio-net queues (auto-detected by default) / 設定 virtio-net 佇列數量（預設自動偵測）"
	@echo "                    Auto-detects from TAP interface: multi-queue TAP -> 4 queues, single-queue TAP -> 1 queue"
	@echo "                    從 TAP 介面自動偵測：multi-queue TAP -> 4 佇列，single-queue TAP -> 1 佇列"
	@echo "  Example: make run VIRTIO_QUEUES=4  (manual override / 手動覆蓋自動偵測)"
	@echo "  Note: Multi-queue requires multi_queue TAP interfaces / Multi-queue 需要 multi_queue TAP 介面"
	@echo "  Create multi-queue TAP: make setup-mq-tap / 建立 multi-queue TAP"

# Clean up intermediate files / 清除暫存檔案
clean:
	@$(rm) $(OBJECTS_ALL)
	@rm -rf $(BINDIR)/*
	@rm -rf $(TEST_BINDIR)
	@rm -rf $(TEST_OBJDIR)
	@rm -f $(OBJDIR)/*.o $(OBJDIR)/*.su
	@mkdir -p $(OBJDIR) $(BINDIR) $(TEST_OBJDIR) $(TEST_BINDIR)
	@$(rm) os.list
	@echo "Cleanup complete!"

# Remove build outputs / 移除建置產物
remove: clean
	@$(rm) $(BINDIR)/$(TARGET)
	@echo "Executable removed!"

test-nat-udp: $(TEST_NAT_UDP_BIN)
	@echo "========================================="
	@echo "Running Test Case: NAT UDP Forwarding"
	@echo "========================================="
	if ! ip link show $(QEMU_BRIDGE_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_BRIDGE_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_BRIDGE_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	if ! ip link show $(QEMU_WAN_TAP) >/dev/null 2>&1; then \
		echo "[SKIP] TAP interface '$(QEMU_WAN_TAP)' not available"; \
		echo "      Create it with: sudo ip tuntap add dev $(QEMU_WAN_TAP) mode tap user $$USER"; \
		exit 0; \
	fi; \
	status=0; \
	output=$$(timeout --foreground $(QEMU_RUN_TIMEOUT)s $(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SOFT_FLAGS) \
		-netdev tap,id=net0,ifname=$(QEMU_BRIDGE_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=$(QEMU_BRIDGE_MAC) \
		-netdev tap,id=net1,ifname=$(QEMU_WAN_TAP),script=no,downscript=no \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=$(QEMU_WAN_MAC) \
		-kernel $(TEST_NAT_UDP_BIN) 2>&1) || status=$$?; \
	if echo "$$output" | grep -qi "could not open /dev/net/tun"; then \
		echo "[SKIP] Access to /dev/net/tun denied."; \
		echo "      Options: run 'sudo setcap cap_net_admin+ep $$(command -v $(QEMU))'"; \
		echo "      or execute this target via sudo."; \
		exit 0; \
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
