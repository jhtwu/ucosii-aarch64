# 雲端測試指南

本指南說明如何在各種雲端平台上測試 ARMv8 µC/OS-II 專案。

---

## 📋 目錄

1. [雲端平台需求](#雲端平台需求)
2. [快速開始](#快速開始)
3. [平台特定設定](#平台特定設定)
4. [效能最佳化](#效能最佳化)
5. [CI/CD 整合](#cicd-整合)
6. [疑難排解](#疑難排解)

---

## 雲端平台需求

### ARM64 平台（推薦，支援 KVM 加速）

| 雲端服務商 | 實例類型 | KVM 支援 | 建議配置 |
|-----------|---------|---------|---------|
| **AWS** | Graviton2/3 (t4g, c7g, m7g) | ✅ 是 | t4g.medium 或以上 |
| **Google Cloud** | Tau T2A | ✅ 是 | t2a-standard-2 或以上 |
| **Azure** | Dpsv5/Epsv5 (Ampere Altra) | ✅ 是 | Standard_D2ps_v5 或以上 |
| **Oracle Cloud** | Ampere A1 | ✅ 是 | VM.Standard.A1.Flex (2 OCPU) |

### x86_64 平台（軟體模擬）

任何 x86_64 雲端實例均可運行，但效能較低：
- AWS EC2 t3/t4/m5/c5 系列
- Google Cloud N1/N2 系列
- Azure Dv3/Ev3 系列

**注意：** x86_64 平台僅能使用 QEMU 軟體模擬，無法使用 KVM 加速。

---

## 快速開始

### 步驟 1：安裝依賴套件

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    gcc-aarch64-linux-gnu \
    qemu-system-aarch64 \
    make \
    bridge-utils \
    iproute2 \
    git
```

**Amazon Linux 2023:**
```bash
sudo dnf install -y \
    gcc-aarch64-linux-gnu \
    qemu-system-aarch64 \
    make \
    bridge-utils \
    iproute2 \
    git
```

**Red Hat/CentOS:**
```bash
sudo yum install -y \
    gcc-aarch64-linux-gnu \
    qemu-system-aarch64 \
    make \
    bridge-utils \
    iproute2 \
    git
```

### 步驟 2：克隆專案並編譯

```bash
git clone <repository-url>
cd ucosii-aarch64
make
```

### 步驟 3：執行基本測試

```bash
# 方案 A：簡易測試（無需特權，適合 CI/CD）
make test

# 方案 B：完整網路測試（需一次性 sudo 設定）
# 先設定 TAP 介面（參考下方「網路設定」章節）
make test-dual
```

---

## 平台特定設定

### AWS EC2 (Graviton)

**1. 啟用嵌套虛擬化（Graviton2/3 預設支援）:**
```bash
# 檢查 KVM 可用性
ls -l /dev/kvm

# 加入 kvm 群組
sudo usermod -aG kvm $USER
# 登出後重新登入
```

**2. 安全群組設定:**
- 無需開放額外埠口（僅使用 SSH）

**3. 執行測試:**
```bash
make run
```

**4. 使用 AWS Systems Manager 自動化:**
```bash
# 建立 SSM 文件進行自動化測試
aws ssm create-document \
  --name "uCOS-II-Test" \
  --document-type "Command" \
  --content file://test-automation.json
```

### Google Cloud (Tau T2A)

**1. 建立 ARM64 實例:**
```bash
gcloud compute instances create ucos-test \
  --machine-type=t2a-standard-2 \
  --zone=us-central1-a \
  --image-family=ubuntu-2204-lts \
  --image-project=ubuntu-os-cloud
```

**2. 啟用嵌套虛擬化:**
```bash
# 檢查並啟用
sudo modprobe kvm
sudo chmod 666 /dev/kvm
```

**3. 執行測試:**
```bash
make test
```

### Azure (Ampere Altra)

**1. 建立 ARM64 VM:**
```bash
az vm create \
  --resource-group myResourceGroup \
  --name ucos-test \
  --size Standard_D2ps_v5 \
  --image Canonical:0001-com-ubuntu-server-jammy:22_04-lts-arm64:latest
```

**2. 啟用嵌套虛擬化:**
Azure ARM64 VM 預設支援，但需確認：
```bash
ls -l /dev/kvm
sudo usermod -aG kvm $USER
```

### Oracle Cloud (Ampere A1)

**1. Always Free Tier 可用:**
Oracle Cloud 提供 4 OCPU + 24GB RAM 的免費 Ampere A1 實例

**2. 建立實例後:**
```bash
# 安裝工具
sudo dnf install -y qemu-system-aarch64 gcc-aarch64-linux-gnu make

# 測試
make test
```

---

## 效能最佳化

### 自動偵測與最佳化

專案的 Makefile 會自動偵測環境並選擇最佳配置：

```bash
# 查看當前配置
make run
# 輸出會顯示：
# === Platform: ARM64 host with KVM acceleration ===
# === Network: KVM with vhost-net acceleration ===
```

### 效能層級

| 層級 | 條件 | 預期吞吐量 |
|-----|------|-----------|
| **Tier 1A** | ARM64 + KVM + vhost-net + Multi-queue | 500-1500+ Mbps |
| **Tier 1B** | ARM64 + KVM + vhost-net | 300-800 Mbps |
| **Tier 2** | ARM64 + KVM (無 vhost-net) | 200-500 Mbps |
| **Tier 3** | x86_64 軟體模擬 | 50-200 Mbps |

### 啟用完整效能

```bash
# 確認 KVM 和 vhost-net 可用
ls -l /dev/kvm /dev/vhost-net

# 加入群組（需登出重新登入）
sudo usermod -aG kvm $USER

# 或臨時變更權限
sudo chmod 666 /dev/kvm /dev/vhost-net

# 執行
make run
```

### Multi-queue 設定（進階）

```bash
# 建立 multi-queue TAP 介面
make setup-mq-tap

# 使用 4 個佇列執行
make run VIRTIO_QUEUES=4
```

**注意：** Multi-queue 需要驅動程式支援，目前專案尚未完整實作。

---

## CI/CD 整合

### GitHub Actions 範例

建立 `.github/workflows/test.yml`：

```yaml
name: µC/OS-II ARM64 Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Install Dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          gcc-aarch64-linux-gnu \
          qemu-system-aarch64 \
          make

    - name: Build
      run: make

    - name: Run Tests
      run: make test
      timeout-minutes: 5

    - name: Upload Artifacts
      if: always()
      uses: actions/upload-artifact@v3
      with:
        name: build-artifacts
        path: |
          bin/kernel.elf
          os.list
```

### GitLab CI 範例

建立 `.gitlab-ci.yml`：

```yaml
image: ubuntu:22.04

stages:
  - build
  - test

before_script:
  - apt-get update -qq
  - apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64 make

build:
  stage: build
  script:
    - make
  artifacts:
    paths:
      - bin/
      - os.list
    expire_in: 1 week

test:
  stage: test
  script:
    - make test
  timeout: 5 minutes
```

### AWS CodeBuild 範例

建立 `buildspec.yml`：

```yaml
version: 0.2

phases:
  install:
    runtime-versions:
      python: 3.9
    commands:
      - apt-get update
      - apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64 make

  build:
    commands:
      - make

  post_build:
    commands:
      - make test

artifacts:
  files:
    - bin/kernel.elf
    - os.list
```

### Docker 容器化測試

建立 `Dockerfile`：

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    qemu-system-aarch64 \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN make

CMD ["make", "test"]
```

**使用方式：**
```bash
docker build -t ucosii-aarch64-test .
docker run --rm ucosii-aarch64-test
```

---

## 疑難排解

### 問題 1：找不到 KVM 設備

**症狀：**
```
WARNING: KVM is not available
```

**解決方案：**
```bash
# 檢查 /dev/kvm 是否存在
ls -l /dev/kvm

# 若不存在，檢查是否為 ARM64 實例
uname -m  # 應顯示 aarch64

# 載入 KVM 模組
sudo modprobe kvm

# 加入 kvm 群組
sudo usermod -aG kvm $USER
# 登出後重新登入
```

### 問題 2：vhost-net 不可用

**症狀：**
```
WARNING: vhost-net is not available (TCP performance will be limited)
```

**解決方案：**
```bash
# 載入 vhost-net 模組
sudo modprobe vhost_net

# 變更權限
sudo chmod 666 /dev/vhost-net

# 或加入 kvm 群組（建議）
sudo usermod -aG kvm $USER
```

### 問題 3：TAP 介面忙碌中

**症狀：**
```
Device or resource busy
```

**解決方案：**
```bash
# 找出佔用的程序
sudo lsof | grep qemu-lan

# 終止 QEMU 程序
pkill -9 qemu-system-aarch64

# 重新啟動測試
make test-dual
```

### 問題 4：測試逾時

**症狀：**
測試卡住不動或逾時失敗

**解決方案：**
```bash
# 使用較短的逾時時間
make test QEMU_RUN_TIMEOUT=30

# 檢查 QEMU 是否正常啟動
qemu-system-aarch64 --version

# 增加除錯輸出
make run  # 手動觀察啟動流程
```

### 問題 5：編譯器找不到

**症狀：**
```
aarch64-linux-gnu-gcc: command not found
```

**解決方案：**
```bash
# Ubuntu/Debian
sudo apt-get install gcc-aarch64-linux-gnu

# 或使用其他工具鏈
# 修改 Makefile 第 6 行：
# TOOLCHAIN = aarch64-none-elf  # 視安裝的工具鏈而定
```

### 問題 6：網路測試失敗

**症狀：**
```
[FAIL] ARP response timeout
[FAIL] No interrupt activity detected
```

**解決方案：**
```bash
# 檢查 TAP 介面狀態
ip link show qemu-lan
ip link show qemu-wan

# 確認介面已啟動
sudo ip link set qemu-lan up
sudo ip link set qemu-wan up

# 檢查橋接狀態
brctl show

# 確認 IP 位址
ip addr show br-lan  # 應有 192.168.1.103
ip addr show br-wan  # 應有 10.3.5.103

# 重建 TAP 介面
sudo ip tuntap del dev qemu-lan mode tap
sudo ip tuntap add dev qemu-lan mode tap user $USER
sudo ip link set qemu-lan up
sudo brctl addif br-lan qemu-lan
```

---

## 效能基準測試

### 在雲端環境中測試效能

```bash
# 編譯並執行
make

# 觀察啟動訊息中的配置
make run
# 記錄：
# - Platform (ARM64 + KVM 或 x86_64 emulation)
# - Network (vhost-net 是否啟用)
# - VirtIO Queues (1 或 4)

# 執行網路測試並記錄結果
make test-dual
# 觀察：
# - ARP 回應時間 (arp_us)
# - Ping 回應時間 (ping_us min/max/avg)
# - 中斷計數 (IRQ delta)
```

### 預期效能指標

| 環境 | ARP 回應時間 | Ping 平均延遲 | 備註 |
|-----|------------|-------------|------|
| **AWS Graviton3 + KVM + vhost** | < 500 µs | < 200 µs | 最佳效能 |
| **GCP Tau T2A + KVM** | < 800 µs | < 300 µs | 良好效能 |
| **Oracle A1 + KVM** | < 1000 µs | < 400 µs | 免費層可用 |
| **x86_64 軟體模擬** | 1-5 ms | 1-3 ms | 僅供功能驗證 |

---

## 其他資源

- [主要 README](../README.md)
- [AI 上手指南](ai_onboarding.zh.md)
- [網路效能最佳化](NETWORK_PERFORMANCE.md)
- [雙網卡測試指南](dual_nic_ping_guide.zh.md)
- [VirtIO 驅動說明](VIRTIO_NET_DRIVER.md)

---

## 聯絡與支援

遇到問題時，請：
1. 檢查上方「疑難排解」章節
2. 查看 `os.list` 檔案進行除錯
3. 提交 Issue 並附上：
   - 雲端平台與實例類型
   - `uname -a` 輸出
   - `make run` 的完整輸出
   - 錯誤訊息

---

**祝測試順利！**
