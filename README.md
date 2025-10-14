# ARMv8 Bare-Metal µC/OS Demo / ARMv8 裸機 µC/OS 示範專案

## Overview / 專案簡介
This repository contains a bare-metal firmware project targeting ARMv8-A (Cortex-A57) hardware. It brings up Micrium µC/OS-II on top of a minimal board support package and demonstrates task scheduling, interrupt handling, serial I/O, and basic networking peripherals under QEMU.

本專案提供針對 ARMv8-A（Cortex-A57）平台的裸機韌體，整合 Micrium µC/OS-II，並示範在 QEMU 上的多任務排程、中斷管理、序列埠輸出與網路周邊驅動。

## Repository Layout / 目錄結構
- `src/` – Core OS sources, BSP drivers, startup code, linker script, and demo tasks.
- `Makefile` – Unified build and execution entry point, replacing the legacy shell scripts.
- `bin/` – Generated kernel images (`kernel.elf`) after a successful build.
- `obj/` – Intermediate object files and stack-usage reports produced during compilation.

## Prerequisites / 先決條件
- AArch64 cross toolchain providing `aarch64-thunderx-elf-gcc` (or update `TOOLCHAIN` in the Makefile).
- GNU Make and standard build utilities (`as`, `objdump`, `size`, `objcopy`).
- QEMU with AArch64 support (`qemu-system-aarch64`).
- Optional: `sudo` privileges if you plan to configure the host networking bridges.

需要以下工具才能建置與執行：
- 支援 `aarch64-thunderx-elf-gcc` 的 AArch64 交叉編譯器（或調整 Makefile 中的 `TOOLCHAIN` 參數）。
- GNU Make 與基本建置工具 (`as`, `objdump`, `size`, `objcopy`)。
- 具備 AArch64 支援的 QEMU (`qemu-system-aarch64`)。
- 若要設定橋接網路，需要 `sudo` 權限以建立 TAP/bridge 介面。

## Build Instructions / 建置流程
```bash
make          # Build the firmware image into bin/kernel.elf
```
The Makefile will compile all sources, generate `kernel.elf`, and dump a mixed source/assembly listing to `os.list`. Objects are stored under `obj/`.

執行 `make` 即可完成編譯，輸出 `bin/kernel.elf` 與對應的 `os.list` 反組譯清單。

To clean all artifacts:
```bash
make clean
```
To clean and remove the final binary:
```bash
make remove
```

使用 `make clean` 移除中繼檔案，或使用 `make remove` 連同最終執行檔一併刪除。

## Running in QEMU / 在 QEMU 上執行
- Software emulation / 純軟體模擬：
  ```bash
  make run
  ```
- KVM acceleration (host must support hardware virtualization) / KVM 加速：
  ```bash
  make run-kvm
  ```
- Wait for GDB connection before starting the machine / 啟動並等待 GDB：
  ```bash
  make qemu-gdb
  ```
- Launch GDB manually / 單獨啟動 GDB：
  ```bash
  make gdb
  ```

Run targets print bilingual status messages to help confirm which configuration is active.

所有執行目標都會輸出中英文提示，方便確認目前的啟動模式。

## Optional Networking Setup / 選用的網路設定
If you need bridged networking identical to the original scripts, run:
```bash
sudo make setup-network
```
This creates the `br-lan` and `br-wan` bridges and associated TAP interfaces. Adjust NIC names (`enp0s3`, `enp0s8`) or addresses inside the Makefile if your host differs.

如需與原腳本相同的橋接網路，請執行 `sudo make setup-network`。若主機網卡名稱或 IP 配置不同，請修改 Makefile 中對應的參數。

### Dual NIC Diagnostics / 雙介面偵錯
- `make run` 會同時掛載 `qemu-lan`（LAN）與 `qemu-wan`（WAN）兩個 TAP，韌體將自動對 `192.168.1.103` 與 `10.3.5.103` 發送 ARP/ICMP 測試，並輸出 `IRQ delta` 與 `RX packets`，用以確認收包中斷是否正常。
- 若重新建立 TAP，記得 `ip link set qemu-xxx up` 並 `brctl addif` 到對應的 bridge，否則封包不會進入客體系統。
- 詳細流程與疑難排除可參考 `doc/dual_nic_ping_guide.zh.md`。

## Development Tips / 開發建議
- Inspect `os.list` after building to correlate C sources with the generated assembly.
- Update `CORE` or memory sizes in the Makefile if you target different virtual hardware.
- When porting to real hardware, replace the QEMU targets with board-specific boot flows.
- New contributors can start with `doc/ai_onboarding.zh.md`, which summarises project structure, common tweaks, and how to run automated ping diagnostics without sudo once the TAP interface is prepared.

建置後可透過 `os.list` 對照 C 原始碼與組合語言；若要在真實硬體上執行，請依需求調整 Makefile 中的 CPU 與記憶體設定，並替換成實體開機流程。首次接觸專案時，可先閱讀 `doc/ai_onboarding.zh.md`，快速掌握建置、修改與測試流程。

## Troubleshooting / 疑難排解
- **Toolchain not found**: ensure the `$(TOOLCHAIN)` prefix is correct or available in `PATH`.
- **QEMU fails to start**: verify you have `qemu-system-aarch64` and that virtualization flags (for KVM) are enabled in BIOS/UEFI.
- **Bridge creation errors**: double-check interface names and confirm you have adequate permissions; `ip addr show` helps verify adapters.

常見問題：
- **找不到編譯器**：確認 `$(TOOLCHAIN)` 前綴正確或已加入 `PATH`。
- **QEMU 啟動失敗**：檢查是否安裝 `qemu-system-aarch64`，並確認 BIOS/UEFI 已開啟虛擬化功能（若使用 KVM）。
- **橋接網路建立失敗**：確認網卡名稱與權限無誤，可用 `ip addr show` 檢查裝置。

## License / 授權
Original Micrium example code retains its upstream license headers. Additional changes in this repository follow the same terms as the upstream Micrium examples unless otherwise noted.

原始 Micrium 範例程式仍沿用上游授權條款；本儲存庫新增的內容若未另外聲明，亦依循相同的許可條件。
