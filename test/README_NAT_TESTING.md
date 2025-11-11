# NAT Ping Test - 測試說明

## 概述 (Overview)

這個測試驗證 ucOS-II 在 AArch64 平台上的 NAT (Network Address Translation) 網關功能。測試模擬一個完整的網路拓撲，包括 LAN 客戶端通過 NAT 網關訪問 WAN 主機。

This test verifies the NAT (Network Address Translation) gateway functionality of ucOS-II on AArch64 platform. It simulates a complete network topology with LAN clients accessing WAN hosts through the NAT gateway.

## 網路拓撲 (Network Topology)

```
LAN Network (192.168.1.0/24)
    ├── LAN Client 1: 192.168.1.100 (模擬客戶端 / Simulated)
    ├── LAN Client 2: 192.168.1.101 (模擬客戶端 / Simulated)
    ├── NAT Gateway (LAN side): 192.168.1.1 (ucOS-II)
    └── Host TAP: 192.168.1.103 (Linux host)
          │
          │ (NAT Translation)
          │
WAN Network (10.3.5.0/24)
    ├── NAT Gateway (WAN side): 10.3.5.99 (ucOS-II)
    ├── WAN Target: 10.3.5.103 (Linux host)
    └── Host TAP: 10.3.5.103 (Linux host)
```

## 測試內容 (Test Coverage)

### 當前測試 (Current Tests)

1. **ICMP Ping 測試** - 驗證 NAT ICMP 轉發功能
   - LAN 客戶端 (192.168.1.100) 發送 ping 到 WAN 主機 (10.3.5.103)
   - NAT 網關將來源 IP 從 192.168.1.100 轉換為 10.3.5.99
   - 驗證 NAT 轉換表正確維護連接狀態

### 未來擴展 (Future Enhancements)

2. **TCP iperf 性能測試** (TODO)
   - 測試 TCP 連接的 NAT 轉發
   - 測量吞吐量和延遲

3. **UDP iperf 性能測試** (TODO)
   - 測試 UDP 流量的 NAT 轉發
   - 測量數據包丟失率

4. **NAT Session 超時測試** (TODO)
   - 驗證 NAT 會話超時機制

5. **並發連接測試** (TODO)
   - 測試多個並發 NAT 會話

## 使用方法 (Usage)

### 方法 1: 使用 Makefile (推薦 / Recommended)

```bash
# 1. 設置 TAP 網路介面 (需要 root 權限)
sudo ./test_nat_ping_helper.sh setup

# 2. 運行測試
make test-nat-ping

# 3. (可選) 在另一個終端監控流量
sudo ./test_nat_ping_helper.sh monitor

# 4. 清理
sudo ./test_nat_ping_helper.sh cleanup
```

### 方法 2: 使用輔助腳本 (Helper Script)

```bash
# 完整測試流程（設置 + 監控）
sudo ./test_nat_ping_helper.sh test
```

### 方法 3: 手動設置 (Manual Setup)

```bash
# 1. 創建 TAP 介面
sudo ip tuntap add dev qemu-lan mode tap user $USER
sudo ip addr add 192.168.1.103/24 dev qemu-lan
sudo ip link set qemu-lan up

sudo ip tuntap add dev qemu-wan mode tap user $USER
sudo ip addr add 10.3.5.103/24 dev qemu-wan
sudo ip link set qemu-wan up

# 2. 啟用 IP 轉發
sudo sysctl -w net.ipv4.ip_forward=1

# 3. 運行測試
make test-nat-ping

# 4. 清理
sudo ip link delete qemu-lan
sudo ip link delete qemu-wan
```

## 輔助腳本功能 (Helper Script Features)

`test_nat_ping_helper.sh` 提供以下功能：

### 命令 (Commands)

- `setup` - 設置 TAP 介面和路由 (Setup TAP interfaces and routing)
- `monitor` - 監控 ICMP 流量 (Monitor ICMP traffic on both interfaces)
- `respond` - 響應來自 NAT 網關的 ping (Respond to pings from NAT gateway)
- `cleanup` - 清理 TAP 介面 (Remove TAP interfaces)
- `test` - 運行完整測試流程 (Run full test with monitoring)

### 範例 (Examples)

```bash
# 設置網路
sudo ./test_nat_ping_helper.sh setup

# 在另一個終端監控流量
sudo ./test_nat_ping_helper.sh monitor

# 清理
sudo ./test_nat_ping_helper.sh cleanup
```

## 測試原理 (How It Works)

### 1. 數據包流程 (Packet Flow)

#### 出站 (Outbound - LAN → WAN)
```
[LAN Client 192.168.1.100]
    ↓ ICMP Echo Request
    ↓ Src: 192.168.1.100, Dst: 10.3.5.103
[NAT Gateway - LAN Interface 192.168.1.1]
    ↓ NAT Translation
    ↓ Src: 192.168.1.100 → 10.3.5.99 (SNAT)
[NAT Gateway - WAN Interface 10.3.5.99]
    ↓ ICMP Echo Request
    ↓ Src: 10.3.5.99, Dst: 10.3.5.103
[WAN Host 10.3.5.103]
```

#### 入站 (Inbound - WAN → LAN)
```
[WAN Host 10.3.5.103]
    ↓ ICMP Echo Reply
    ↓ Src: 10.3.5.103, Dst: 10.3.5.99
[NAT Gateway - WAN Interface 10.3.5.99]
    ↓ NAT Reverse Translation
    ↓ Dst: 10.3.5.99 → 192.168.1.100
[NAT Gateway - LAN Interface 192.168.1.1]
    ↓ ICMP Echo Reply
    ↓ Src: 10.3.5.103, Dst: 192.168.1.100
[LAN Client 192.168.1.100]
```

### 2. NAT 轉換表 (NAT Translation Table)

NAT 網關維護一個轉換表來追蹤連接：

| Protocol | LAN IP:Port | WAN Port | Dst IP:Port | Timeout |
|----------|-------------|----------|-------------|---------|
| ICMP | 192.168.1.100:0x1234 | 20000 | 10.3.5.103:0 | 60s |

### 3. 測試驗證 (Test Verification)

測試程式驗證以下功能：
- ✅ NAT 轉換表正確創建條目
- ✅ 出站數據包正確轉換來源 IP
- ✅ 入站數據包正確還原目標 IP
- ✅ NAT 統計資訊正確更新

## 相關文件 (Related Files)

- `test/test_nat_ping.c` - NAT ping 測試程式
- `test/test_nat_icmp.c` - NAT ICMP 轉發測試 (內部測試)
- `test/test_nat_udp.c` - NAT UDP 轉發測試 (內部測試)
- `test/test_network_config.h` - 網路配置定義
- `src/nat.c` / `src/nat.h` - NAT 實作
- `test_nat_ping_helper.sh` - 測試輔助腳本

## 配置參數 (Configuration)

所有 IP 配置都定義在 `test/test_network_config.h`：

```c
// LAN 配置
#define LAN_GUEST_IP {192u, 168u, 1u, 1u}    // NAT Gateway LAN IP
#define LAN_HOST_IP  {192u, 168u, 1u, 103u}  // Host TAP IP

// WAN 配置
#define WAN_GUEST_IP {10u, 3u, 5u, 99u}      // NAT Gateway WAN IP
#define WAN_HOST_IP  {10u, 3u, 5u, 103u}     // Host TAP IP
```

## 故障排除 (Troubleshooting)

### 問題 1: TAP 介面未找到
```
[SKIP] TAP interface 'qemu-lan' not available
```

**解決方法：**
```bash
sudo ./test_nat_ping_helper.sh setup
```

### 問題 2: 無法訪問 /dev/net/tun
```
[SKIP] Access to /dev/net/tun denied
```

**解決方法：**
```bash
sudo setcap cap_net_admin+ep $(command -v qemu-system-aarch64)
# 或者使用 sudo 運行測試
sudo make test-nat-ping
```

### 問題 3: 測試超時
```
⚠ NAT PING TEST TIMED OUT
```

**可能原因：**
- NAT 功能未正確初始化
- 網路配置錯誤
- TAP 介面未正確設置

**檢查方法：**
```bash
# 檢查 TAP 介面狀態
ip addr show qemu-lan
ip addr show qemu-wan

# 檢查路由
ip route | grep 192.168.1
ip route | grep 10.3.5

# 監控流量
sudo ./test_nat_ping_helper.sh monitor
```

## CI/CD 集成 (CI/CD Integration)

這個測試已經集成到 CI/CD pipeline 中：

- `.github/workflows/ci.yml` - 主 CI pipeline
- `.github/workflows/pr-check.yml` - Pull request 檢查

測試會在每次 push 和 pull request 時自動運行。

## Docker/LXC 考慮 (Docker/LXC Considerations)

**當前方案：** 使用 TAP 網路 + host-side scripting（不需要 Docker/LXC）

**優點：**
- ✅ 輕量級，無需額外容器
- ✅ 直接使用 Linux 網路功能
- ✅ 易於調試和監控

**未來擴展：** 如果需要測試更複雜的場景（如 iperf），可以考慮：
- 使用 Docker 容器模擬多個客戶端/服務器
- 使用 LXC 容器模擬完整的網路環境
- 使用 network namespace 隔離測試環境

## 性能測試規劃 (Performance Testing Roadmap)

### Phase 1: ICMP Ping (✅ 已完成 / Completed)
- 基本 NAT 轉發功能測試
- NAT 表管理驗證

### Phase 2: TCP iperf (TODO)
```bash
# 計劃實作 (Planned implementation)
make test-nat-iperf-tcp
```
- 測試 TCP 連接的 NAT 轉發
- 測量吞吐量
- 測試並發連接

### Phase 3: UDP iperf (TODO)
```bash
# 計劃實作 (Planned implementation)
make test-nat-iperf-udp
```
- 測試 UDP 流量的 NAT 轉發
- 測量數據包丟失率
- 測試高頻寬場景

## 參考資料 (References)

- [NAT Implementation Details](../src/nat.h)
- [Network Configuration](test_network_config.h)
- [RFC 3022 - Traditional IP Network Address Translator](https://tools.ietf.org/html/rfc3022)
- [ucOS-II Documentation](https://www.micrium.com/)
