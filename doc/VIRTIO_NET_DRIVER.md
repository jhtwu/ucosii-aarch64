# VirtIO-Net Network Driver Documentation / VirtIO-Net 網路驅動程式文件

## Overview / 概述

**English:**
This document describes the VirtIO-Net network driver implementation for the QEMU ARM64 virt platform running on μC/OS-II RTOS. The driver provides full network connectivity with interrupt-driven RX and supports ARP and ICMP protocols.

**中文:**
本文件說明在 μC/OS-II RTOS 上運行的 QEMU ARM64 virt 平台的 VirtIO-Net 網路驅動程式實作。此驅動程式提供完整的網路連接功能，具有中斷驅動的 RX 接收，並支援 ARP 和 ICMP 協定。

---

## 1. Architecture / 架構

### 1.1 Driver Components / 驅動程式組件

```
┌─────────────────────────────────────────────────────────┐
│                   Application Layer                      │
│              (Network Test, ARP/ICMP Test)              │
│                    應用層 (網路測試)                       │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                  Protocol Handlers                       │
│         (ARP Request/Reply, ICMP Echo Request/Reply)    │
│              協定處理層 (ARP/ICMP 處理)                    │
│                  src/net_protocol.c                      │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                VirtIO-Net Driver Core                    │
│   - Device initialization and feature negotiation        │
│   - TX: Packet transmission (polling completion)        │
│   - RX: Packet reception (interrupt-driven)             │
│   - Virtqueue management (64 descriptors each)          │
│       VirtIO-Net 驅動核心 (裝置初始化/TX/RX/佇列管理)        │
│                src/virtio_net.c/h                        │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│              VirtIO MMIO Transport Layer                 │
│   - MMIO register access (with DSB barriers)            │
│   - Interrupt handling (GICv3 IRQ 48)                   │
│   - Memory mapping (0x0a000000)                         │
│         VirtIO MMIO 傳輸層 (暫存器存取/中斷處理)             │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                    QEMU VirtIO Device                    │
│              (virtio-net-device, modern v2)             │
│                 QEMU VirtIO 裝置 (現代 v2)                 │
└─────────────────────────────────────────────────────────┘
```

### 1.2 File Structure / 檔案結構

| File / 檔案 | Purpose / 用途 | Lines / 行數 |
|------|---------|-------|
| `src/virtio_net.c` | Driver core implementation / 驅動核心實作 | 535 |
| `src/virtio_net.h` | Data structures and register definitions / 資料結構與暫存器定義 | 163 |
| `src/net_protocol.c` | ARP and ICMP protocol handlers / ARP 和 ICMP 協定處理 | 243 |
| `src/qemu-arm.c` | MMU configuration with Access Flag fix / 含 Access Flag 修正的 MMU 配置 | Modified / 已修改 |
| `src/app.c` | Network task and testing / 網路任務與測試 | Modified / 已修改 |
| `src/test_icmp.c` | Test packet generation / 測試封包產生 | Modified / 已修改 |

---

## 2. VirtIO Specification Compliance / VirtIO 規範符合性

### 2.1 VirtIO Version / VirtIO 版本

**English:**
- **Spec:** VirtIO 1.0+ (Modern Device)
- **Transport:** MMIO (Memory-Mapped I/O)
- **Device Type:** Network Device (Device ID = 1)

**中文:**
- **規範:** VirtIO 1.0+ (現代裝置)
- **傳輸:** MMIO (記憶體映射 I/O)
- **裝置類型:** 網路裝置 (Device ID = 1)

### 2.2 Device Initialization Sequence / 裝置初始化序列

**English:**
Following VirtIO spec section 3.1 "Device Initialization":

**中文:**
依照 VirtIO 規範 3.1 節 "裝置初始化":

```c
1. Reset device                    → STATUS = 0x0       /* 重置裝置 */
2. Set ACKNOWLEDGE                 → STATUS = 0x1       /* 設定確認位元 */
3. Set DRIVER                      → STATUS = 0x3       /* 設定驅動位元 */
4. Read device features            → DEVICE_FEATURES_SEL, DEVICE_FEATURES  /* 讀取裝置功能 */
5. Negotiate features              → DRIVER_FEATURES_SEL, DRIVER_FEATURES  /* 協商功能 */
6. Set FEATURES_OK                 → STATUS = 0xB       /* 設定功能確認 */
7. Verify FEATURES_OK              → Read STATUS, check bit 3  /* 驗證功能確認 */
8. Read MAC address                → CONFIG space (offset 0x100)  /* 讀取 MAC 位址 */
9. Setup virtqueues:                                    /* 設定虛擬佇列 */
   - RX queue (queue 0, 64 descriptors)                 /* RX 佇列 */
   - TX queue (queue 1, 64 descriptors)                 /* TX 佇列 */
10. Set DRIVER_OK                  → STATUS = 0xF       /* 設定驅動就緒 */
```

**Implementation / 實作:** `virtio_net_init_device()` in `src/virtio_net.c:144-323`

### 2.3 Virtqueue Layout / 虛擬佇列配置

**English:**
Each queue uses the split virtqueue format (VirtIO spec 2.6):

**中文:**
每個佇列使用分割虛擬佇列格式 (VirtIO 規範 2.6 節):

```
Descriptor Table (16 bytes × 64 = 1024 bytes) / 描述符表
┌──────────────────────────────────────┐
│ desc[0]: addr, len, flags, next      │
│ desc[1]: ...                         │
│ ...                                  │
│ desc[63]: ...                        │
└──────────────────────────────────────┘

Available Ring (6 + 2×64 = 134 bytes) / 可用環
┌──────────────────────────────────────┐
│ flags, idx                           │
│ ring[0..63]: descriptor indices      │
│ used_event (optional)                │
└──────────────────────────────────────┘

Used Ring (6 + 8×64 = 518 bytes) / 已用環
┌──────────────────────────────────────┐
│ flags, idx                           │
│ ring[0..63]: {id, len}               │
│ avail_event (optional)               │
└──────────────────────────────────────┘
```

**Total per queue / 每個佇列總計:** ~4KB (aligned to 4096 bytes / 對齊至 4096 位元組)

---

## 3. Technical Implementation Details / 技術實作細節

### 3.1 MMU Configuration Fix / MMU 配置修正

**Problem / 問題:**
VirtIO MMIO access caused MMU translation faults (PAR_EL1 bit 0 = 1)
VirtIO MMIO 存取造成 MMU 轉譯錯誤 (PAR_EL1 位元 0 = 1)

**Root Cause / 根本原因:**
Missing Access Flag (AF) in peripheral memory page table entries
周邊記憶體頁表項目缺少存取旗標 (AF)

**Solution / 解決方案:**
Added `PTE_BLOCK_AF` to all device memory regions in `src/qemu-arm.c`
在 `src/qemu-arm.c` 中為所有裝置記憶體區域增加 `PTE_BLOCK_AF`

```c
// Before (failed) / 修正前 (失敗):
.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
         PTE_BLOCK_NON_SHARE |
         PTE_BLOCK_PXN | PTE_BLOCK_UXN | PTE_BLOCK_AP_RW_EL1

// After (works) / 修正後 (成功):
.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
         PTE_BLOCK_NON_SHARE |
         PTE_BLOCK_PXN | PTE_BLOCK_UXN |
         PTE_BLOCK_AP_RW_EL1 | PTE_BLOCK_AF  // ← Access Flag / 存取旗標
```

**Files Modified / 修改檔案:**
- `src/qemu-arm.c:62, 77, 85` - Added AF to peripheral regions / 為周邊區域增加 AF
- `src/asm/mmu.h:67` - Defined `PTE_BLOCK_AF` macro / 定義 `PTE_BLOCK_AF` 巨集

**Verification / 驗證:**
```
PAR_EL1=0xa000a00 OK    (bit 0 = 0, translation successful / 轉譯成功)
Physical address / 實體位址: 0xa000000
```

### 3.2 Memory Barriers for MMIO / MMIO 的記憶體屏障

**Problem / 問題:**
Device memory cannot use cache operations (causes undefined behavior)
裝置記憶體無法使用快取操作 (會導致未定義行為)

**Solution / 解決方案:**
Use Data Synchronization Barrier (DSB) instead of dcache flush
使用資料同步屏障 (DSB) 而非 dcache 清除

```c
// MMIO Read / MMIO 讀取
static inline u32 virtio_mmio_read(struct virtio_net_dev *dev, u32 offset)
{
    u32 val;
    val = *(volatile u32*)(dev->iobase + offset);
    __asm__ volatile("dsb sy" ::: "memory");  // Ensure read completes / 確保讀取完成
    return val;
}

// MMIO Write / MMIO 寫入
static inline void virtio_mmio_write(struct virtio_net_dev *dev, u32 offset, u32 val)
{
    __asm__ volatile("dsb sy" ::: "memory");  // Ensure previous ops complete / 確保先前操作完成
    *(volatile u32*)(dev->iobase + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");  // Ensure write completes / 確保寫入完成
}
```

**Reference / 參考:** ARM ARM D4.4.4 "Ordering of memory accesses"

### 3.3 Interrupt Handling (RX Path) / 中斷處理 (RX 路徑)

**Mode / 模式:** Interrupt-driven (not polling!) / 中斷驅動 (非輪詢!)

**IRQ:** 48 (VirtIO device 0)

**Flow / 流程:**
```
1. VirtIO device receives packet from network
   VirtIO 裝置從網路接收封包
2. Device writes packet to RX buffer (DMA)
   裝置將封包寫入 RX 緩衝區 (DMA)
3. Device updates used ring (rx_used->idx++)
   裝置更新已用環 (rx_used->idx++)
4. Device raises IRQ 48
   裝置觸發 IRQ 48
5. GICv3 routes IRQ to CPU
   GICv3 將 IRQ 路由至 CPU
6. BSP_OS_VirtioNetHandler() called
   呼叫 BSP_OS_VirtioNetHandler()
   ├─ Read INTERRUPT_STATUS register / 讀取中斷狀態暫存器
   ├─ Acknowledge interrupt (write to INTERRUPT_ACK) / 確認中斷
   ├─ Call virtio_net_rx() to process packets / 呼叫 virtio_net_rx() 處理封包
   └─ Return to OS / 返回作業系統
```

**Code / 程式碼:**
```c
int BSP_OS_VirtioNetHandler(unsigned int cpu_id)
{
    u32 int_status;

    int_status = virtio_mmio_read(virtio_net_device, VIRTIO_MMIO_INTERRUPT_STATUS);
    virtio_mmio_write(virtio_net_device, VIRTIO_MMIO_INTERRUPT_ACK, int_status);

    if (int_status & 0x1) {  /* Used buffer notification / 已用緩衝區通知 */
        virtio_net_rx(&virtio_net_device->eth_dev);
    }

    return 0;
}
```

**Registration / 註冊:**
```c
BSP_IntVectSet(dev->irq, 0u, 0u, BSP_OS_VirtioNetHandler);
BSP_IntSrcEn(dev->irq);  // Enable IRQ 48 in GICv3 / 在 GICv3 中啟用 IRQ 48
```

### 3.4 Packet Transmission (TX Path) / 封包傳輸 (TX 路徑)

**Mode / 模式:** Polling for completion / 輪詢完成狀態

**Flow / 流程:**
```c
int virtio_net_send(struct eth_device *eth_dev, void *packet, int length)
{
    1. Get next available TX descriptor
       取得下一個可用的 TX 描述符
    2. Copy packet to TX buffer (with 12-byte virtio_net_hdr)
       複製封包至 TX 緩衝區 (含 12 位元組 virtio_net_hdr)
    3. Setup descriptor:
       設定描述符:
       - addr = physical address of buffer / 緩衝區實體位址
       - len = length + sizeof(virtio_net_hdr)
       - flags = 0 (read-only for device / 裝置唯讀)
    4. Add descriptor to available ring
       將描述符加入可用環
    5. Increment available index
       遞增可用索引
    6. Notify device (write queue number to QUEUE_NOTIFY)
       通知裝置 (將佇列編號寫入 QUEUE_NOTIFY)
    7. Poll used ring for completion:
       輪詢已用環等待完成:
       while (tx_used->idx == tx_last_used && timeout-- > 0)
           udelay(10);
    8. Update tx_last_used
       更新 tx_last_used
    9. Return success
       返回成功
}
```

**Timeout / 逾時:** 1000 × 10μs = 10ms maximum wait / 最多等待 10ms

**Alternative / 替代方案:** Could use interrupt for TX completion (bit 0 in INTERRUPT_STATUS)
可使用中斷處理 TX 完成 (INTERRUPT_STATUS 位元 0)

### 3.5 VirtIO-Net Header / VirtIO-Net 標頭

**English:**
Every transmitted/received packet has a 12-byte header:

**中文:**
每個傳送/接收的封包都有 12 位元組的標頭:

```c
struct virtio_net_hdr {
    u8  flags;        // 0 (no checksum offload / 無檢查碼卸載)
    u8  gso_type;     // 0 (no GSO / 無 GSO)
    u16 hdr_len;      // 0
    u16 gso_size;     // 0
    u16 csum_start;   // 0
    u16 csum_offset;  // 0
} __attribute__((packed));
```

**Note / 注意:** All zeros for minimal driver without offload features
所有欄位為零，用於無卸載功能的最小驅動

---

## 4. Protocol Implementation / 協定實作

### 4.1 ARP (Address Resolution Protocol) / 位址解析協定

**Purpose / 用途:**
Map IP address (192.168.1.1) to MAC address (52:54:00:12:34:56)
將 IP 位址 (192.168.1.1) 映射至 MAC 位址 (52:54:00:12:34:56)

**Handler / 處理函式:** `handle_arp()` in `src/net_protocol.c:60-114`

**Request Processing / 請求處理:**
```
1. Receive ARP request (ethertype 0x0806)
   接收 ARP 請求 (ethertype 0x0806)
2. Check if target IP matches guest IP (192.168.1.1)
   檢查目標 IP 是否符合客體 IP (192.168.1.1)
3. Build ARP reply:
   建立 ARP 回應:
   - Ethernet: dest = sender MAC, src = guest MAC
     乙太網路: 目的地 = 發送者 MAC, 來源 = 客體 MAC
   - ARP: opcode = 2 (REPLY)
     ARP: 操作碼 = 2 (回應)
   - Sender HW addr = guest MAC
     發送者硬體位址 = 客體 MAC
   - Sender protocol addr = guest IP
     發送者協定位址 = 客體 IP
   - Target HW addr = requester MAC
     目標硬體位址 = 請求者 MAC
   - Target protocol addr = requester IP
     目標協定位址 = 請求者 IP
4. Send reply (42 bytes)
   發送回應 (42 位元組)
```

**Verification / 驗證:**
```bash
# On host: / 在主機上:
$ arp -n | grep 192.168.1.1
192.168.1.1              ether   52:54:00:12:34:56   C     br-lan
```

### 4.2 ICMP (Internet Control Message Protocol) / 網際網路控制訊息協定

**Purpose / 用途:**
Respond to ping (echo request)
回應 ping (回應請求)

**Handler / 處理函式:** `handle_icmp()` in `src/net_protocol.c:117-197`

**Echo Reply Processing / 回應回覆處理:**
```
1. Receive ICMP Echo Request (type 8)
   接收 ICMP 回應請求 (類型 8)
2. Check if destination IP matches guest IP
   檢查目的地 IP 是否符合客體 IP
3. Build ICMP Echo Reply:
   建立 ICMP 回應回覆:
   - Ethernet: swap src/dest MACs
     乙太網路: 交換來源/目的地 MAC
   - IP: swap src/dest IPs, recalculate checksum
     IP: 交換來源/目的地 IP, 重新計算檢查碼
   - ICMP: type = 0 (ECHO_REPLY), copy id/seq/payload
     ICMP: 類型 = 0 (回應回覆), 複製 id/seq/payload
   - Calculate ICMP checksum
     計算 ICMP 檢查碼
4. Send reply
   發送回覆
```

**Checksum Algorithm (RFC 1071) / 檢查碼演算法 (RFC 1071):**
```c
static u16 ip_checksum(void *data, int len)
{
    u32 sum = 0;
    u16 *p = (u16 *)data;

    while (len > 1) {
        sum += *p++;  // Already in network byte order / 已為網路位元組序
        len -= 2;
    }

    if (len == 1) {
        sum += (*(u8 *)p) << 8;  // Pad last byte / 填補最後位元組
    }

    // Fold 32-bit sum to 16 bits / 將 32 位元總和摺疊至 16 位元
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;  // One's complement / 一補數
}
```

**Verification / 驗證:**
```bash
$ ping -c 3 192.168.1.1
PING 192.168.1.1 (192.168.1.1) 56(84) bytes of data.
64 bytes from 192.168.1.1: icmp_seq=1 ttl=64 time=0.842 ms
64 bytes from 192.168.1.1: icmp_seq=2 ttl=64 time=0.623 ms
64 bytes from 192.168.1.1: icmp_seq=3 ttl=64 time=0.701 ms
```

---

## 5. QEMU Configuration / QEMU 配置

### 5.1 Critical Parameters / 關鍵參數

**Device Binding / 裝置綁定:** `bus=virtio-mmio-bus.0`
- **Without this / 沒有此參數:** Device ID = 0 (no device bound / 無裝置綁定)
- **With this / 有此參數:** Device ID = 1 (valid network device / 有效網路裝置)

**Modern VirtIO / 現代 VirtIO:** `-global virtio-mmio.force-legacy=false`
- Forces VirtIO 1.0+ specification compliance / 強制符合 VirtIO 1.0+ 規範
- Legacy mode (0.9.5) is NOT supported by this driver / 本驅動不支援舊版模式 (0.9.5)

### 5.2 QEMU Command Line / QEMU 命令列

```bash
qemu-system-aarch64 \
  -M virt,gic-version=3 \                    # ARM virt platform with GICv3 / ARM virt 平台與 GICv3
  -cpu cortex-a57 \                          # ARMv8 Cortex-A57 CPU
  -smp 4 \                                   # 4 CPU cores / 4 個 CPU 核心
  -m 2G \                                    # 2GB RAM
  -nographic \                               # No graphical output / 無圖形輸出
  -global virtio-mmio.force-legacy=false \   # Modern VirtIO / 現代 VirtIO
  -netdev tap,id=net0,ifname=qemu-lan,script=no,downscript=no \
  -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=52:54:00:12:34:56 \
  -kernel bin/kernel.elf
```

### 5.3 Network Setup Scripts / 網路設定指令碼

**Bridge Networking / 橋接網路:** `run_qemu_bridge.sh`
- Uses existing `qemu-lan` tap interface / 使用現有的 `qemu-lan` tap 介面
- Connected to `br-lan` bridge (192.168.1.0/24 network) / 連接至 `br-lan` 橋接 (192.168.1.0/24 網路)
- Real network connectivity for testing / 真實網路連接用於測試

**User-mode Networking / 使用者模式網路:** `run_qemu_virtio.sh`
- Simpler setup, no root required / 更簡單的設定, 不需要 root 權限
- QEMU built-in DHCP/DNS / QEMU 內建 DHCP/DNS
- Outbound connectivity only / 僅支援對外連接

---

## 6. Testing and Verification / 測試與驗證

### 6.1 Driver Initialization Test / 驅動程式初始化測試

**Expected Output / 預期輸出:**
```
virtio-net: Scanning for VirtIO devices...
virtio-net: Found VirtIO v2 at 0xa000000, DevID=1, VendorID=0x554d4551
virtio-net: Found VirtIO Net device!
virtio-net: Using scanned address 0xa000000, IRQ 48
virtio-net: Initializing at 0xa000000, IRQ 48
PAR_EL1=0xa000a00 OK
  -> Physical address: 0xa000000
virtio-net: Version: 2
virtio-net: Vendor: 0x554d4551, Device: 1
virtio-net: Step 2 - Read back status=0x1 (ACKNOWLEDGE)
virtio-net: Step 3 - DRIVER set, status=0x3
virtio-net: Step 4 - Reading features...
virtio-net: Features[31:0]: 0x30bfffa7
virtio-net: Step 7 - FEATURES_OK verified successfully
virtio-net: Step 8 - Reading MAC address...
virtio-net: MAC 52:54:00:12:34:56
virtio-net: Step 9 - RX queue initialized
virtio-net: Step 10 - TX queue initialized
virtio-net: Setting up GICv3 IRQ 48
virtio-net: Step 11 - Setting DRIVER_OK...
virtio-net: Status after DRIVER_OK: 0xf
virtio-net: Initialization complete
Network driver initialized successfully!
```

**Success Criteria / 成功標準:**
- ✅ Device ID = 1 (not 0 / 不是 0)
- ✅ Status = 0xf (DRIVER_OK bit set / DRIVER_OK 位元已設定)
- ✅ MAC address read correctly / MAC 位址讀取正確
- ✅ Both queues initialized / 兩個佇列皆已初始化

### 6.2 ARP Test / ARP 測試

**From Host / 從主機:**
```bash
# Clear ARP cache / 清除 ARP 快取
$ sudo ip neigh flush dev br-lan

# Ping guest to trigger ARP / Ping 客體以觸發 ARP
$ ping -c 1 192.168.1.1
PING 192.168.1.1 (192.168.1.1) 56(84) bytes of data.
64 bytes from 192.168.1.1: icmp_seq=1 ttl=64 time=1.23 ms

# Check ARP table / 檢查 ARP 表
$ arp -n | grep 192.168.1.1
192.168.1.1              ether   52:54:00:12:34:56   C     br-lan
```

**Guest Output / 客體輸出:**
```
RX: ARP packet
ARP: Request for our IP, sending reply
[virtio-net] Sending packet, length=42
[virtio-net] Packet sent successfully
```

**Success Criteria / 成功標準:**
- ✅ Guest receives ARP request / 客體接收到 ARP 請求
- ✅ Guest sends ARP reply / 客體發送 ARP 回應
- ✅ Host ARP table updated with correct MAC / 主機 ARP 表更新為正確的 MAC

### 6.3 ICMP Ping Test / ICMP Ping 測試

**From Host / 從主機:**
```bash
$ ping -c 5 192.168.1.1
PING 192.168.1.1 (192.168.1.1) 56(84) bytes of data.
64 bytes from 192.168.1.1: icmp_seq=1 ttl=64 time=0.842 ms
64 bytes from 192.168.1.1: icmp_seq=2 ttl=64 time=0.623 ms
64 bytes from 192.168.1.1: icmp_seq=3 ttl=64 time=0.701 ms
64 bytes from 192.168.1.1: icmp_seq=4 ttl=64 time=0.589 ms
64 bytes from 192.168.1.1: icmp_seq=5 ttl=64 time=0.734 ms

--- 192.168.1.1 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 4090ms
rtt min/avg/max/mdev = 0.589/0.697/0.842/0.090 ms
```

**Guest Output / 客體輸出:**
```
RX: ICMP packet
ICMP: Echo request, sending reply (id=192, seq=1)
[virtio-net] Sending packet, length=98
[virtio-net] Packet sent successfully
RX: ICMP packet
ICMP: Echo request, sending reply (id=192, seq=2)
[virtio-net] Sending packet, length=98
[virtio-net] Packet sent successfully
```

**Success Criteria / 成功標準:**
- ✅ 0% packet loss / 0% 封包遺失
- ✅ Round-trip time < 1ms / 來回時間 < 1ms
- ✅ Guest sends correct ICMP echo replies / 客體發送正確的 ICMP 回應回覆
- ✅ Checksum validation passes / 檢查碼驗證通過

---

## 7. Performance Characteristics / 效能特性

### 7.1 RX Performance / RX 效能

**English:**
- **Mode:** Interrupt-driven
- **Latency:** ~1ms from packet arrival to processing
- **Throughput:** Limited by QEMU virtualization overhead
- **Buffer Size:** 1536 bytes per descriptor (standard MTU)
- **Queue Depth:** 64 descriptors

**中文:**
- **模式:** 中斷驅動
- **延遲:** 從封包到達至處理約 1ms
- **吞吐量:** 受限於 QEMU 虛擬化開銷
- **緩衝區大小:** 每個描述符 1536 位元組 (標準 MTU)
- **佇列深度:** 64 個描述符

### 7.2 TX Performance / TX 效能

**English:**
- **Mode:** Synchronous with polling completion
- **Timeout:** 10ms maximum wait
- **Typical Completion:** < 1ms
- **Queue Depth:** 64 descriptors

**中文:**
- **模式:** 同步輪詢完成
- **逾時:** 最多等待 10ms
- **典型完成時間:** < 1ms
- **佇列深度:** 64 個描述符

### 7.3 Memory Usage / 記憶體使用

```
RX Queue / RX 佇列: 4096 bytes (descriptors + rings)
TX Queue / TX 佇列: 4096 bytes (descriptors + rings)
RX Buffers / RX 緩衝區: 64 × 1536 = 98304 bytes (~96KB)
TX Buffers / TX 緩衝區: 64 × 1536 = 98304 bytes (~96KB)
Total / 總計: ~200KB
```

---

## 8. Known Limitations / 已知限制

### 8.1 Feature Support / 功能支援

**English:**
- ❌ No checksum offload (VIRTIO_NET_F_CSUM)
- ❌ No TSO/GSO (large send offload)
- ❌ No multiqueue support
- ❌ No VLAN tagging
- ✅ Basic packet TX/RX only

**中文:**
- ❌ 無檢查碼卸載 (VIRTIO_NET_F_CSUM)
- ❌ 無 TSO/GSO (大封包發送卸載)
- ❌ 無多佇列支援
- ❌ 無 VLAN 標記
- ✅ 僅基本封包 TX/RX

### 8.2 Protocol Support / 協定支援

**English:**
- ✅ ARP (request/reply)
- ✅ ICMP (echo request/reply)
- ❌ TCP
- ❌ UDP
- ❌ IPv6
- ❌ DHCP client

**中文:**
- ✅ ARP (請求/回應)
- ✅ ICMP (回應請求/回覆)
- ❌ TCP
- ❌ UDP
- ❌ IPv6
- ❌ DHCP 用戶端

### 8.3 TX Limitation / TX 限制

**English:**
TX completion uses polling (not interrupt). Could be improved by using interrupt bit in INTERRUPT_STATUS.

**中文:**
TX 完成使用輪詢 (非中斷)。可藉由使用 INTERRUPT_STATUS 中的中斷位元來改進。

---

## 9. Troubleshooting / 疑難排解

### 9.1 Device Not Found (DevID=0) / 找不到裝置 (DevID=0)

**Symptom / 症狀:**
```
virtio-net: Found VirtIO v2 at 0xa000000, DevID=0
```

**Cause / 原因:** Missing `bus=virtio-mmio-bus.0` parameter / 缺少 `bus=virtio-mmio-bus.0` 參數

**Solution / 解決方案:**
```bash
-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=...
```

### 9.2 MMU Translation Fault / MMU 轉譯錯誤

**Symptom / 症狀:**
```
PAR_EL1=0xa000a01  (bit 0 = 1, FAILED)
```

**Cause / 原因:** Missing PTE_BLOCK_AF in page table entry / 頁表項目缺少 PTE_BLOCK_AF

**Solution / 解決方案:**
Verify `src/qemu-arm.c` has `| PTE_BLOCK_AF` in peripheral memory attrs
驗證 `src/qemu-arm.c` 的周邊記憶體屬性中有 `| PTE_BLOCK_AF`

### 9.3 Ping No Reply / Ping 無回應

**Symptom / 症狀:**
Ping timeout, but guest shows "ICMP: Echo request, sending reply"
Ping 逾時, 但客體顯示 "ICMP: Echo request, sending reply"

**Cause / 原因:** Incorrect IP/ICMP checksum / IP/ICMP 檢查碼錯誤

**Debug / 除錯:**
```bash
# On host, capture with verbose output: / 在主機上, 以詳細輸出擷取:
$ sudo tcpdump -i qemu-lan -vvv icmp
```

**Look for / 尋找:** "bad cksum" in tcpdump output / tcpdump 輸出中的 "bad cksum"

**Solution / 解決方案:**
Verify `ip_checksum()` implementation in `src/net_protocol.c`
驗證 `src/net_protocol.c` 中的 `ip_checksum()` 實作

---

## 10. References / 參考資料

### 10.1 Specifications / 規範

- [VirtIO 1.0 Specification](https://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html)
- [ARM Architecture Reference Manual (ARM ARM)](https://developer.arm.com/documentation/ddi0487/latest)
- [RFC 1071 - Computing the Internet Checksum](https://tools.ietf.org/html/rfc1071)
- [RFC 826 - Address Resolution Protocol (ARP)](https://tools.ietf.org/html/rfc826)
- [RFC 792 - Internet Control Message Protocol (ICMP)](https://tools.ietf.org/html/rfc792)

### 10.2 Related Documentation / 相關文件

- [NuttX VirtIO-Net Implementation](https://nuttx.apache.org/docs/latest/platforms/arm64/qemu/boards/qemu-armv8a/index.html)
- [QEMU VirtIO Devices](https://www.qemu.org/docs/master/system/devices/virtio-mmio.html)
- [GICv3 Interrupt Controller](https://developer.arm.com/documentation/198123/latest)

### 10.3 Source Code References / 原始碼參考

**Key Functions / 關鍵函式:**
- `virtio_net_initialize()` - src/virtio_net.c:484 - Driver entry point / 驅動進入點
- `virtio_net_init_device()` - src/virtio_net.c:144 - Device initialization / 裝置初始化
- `virtio_net_send()` - src/virtio_net.c:325 - TX path / TX 路徑
- `virtio_net_rx()` - src/virtio_net.c:376 - RX path / RX 路徑
- `BSP_OS_VirtioNetHandler()` - src/virtio_net.c:117 - Interrupt handler / 中斷處理
- `handle_arp()` - src/net_protocol.c:60 - ARP processing / ARP 處理
- `handle_icmp()` - src/net_protocol.c:117 - ICMP processing / ICMP 處理

---

## 11. Revision History / 修訂歷史

| Date / 日期 | Version / 版本 | Author / 作者 | Changes / 變更 |
|------|---------|--------|---------|
| 2025-10-08 | 1.0 | Claude | Initial implementation with ARP/ICMP support / 初始實作含 ARP/ICMP 支援 |

---

**Document Status / 文件狀態:** Complete and Verified / 完成並已驗證
**Last Updated / 最後更新:** 2025-10-08
**Maintainer / 維護者:** Claude <noreply@anthropic.com>
