# ARMv8 專案 AI 上手指南

> 目標：協助新加入的自動化/AI 工具快速理解專案結構、常見工作流程、測試方法與注意事項。完成本文步驟後，即可在不需要 sudo 的狀態下進行編譯、測試與雙網卡封包驗證。

---

## 1. 專案快速導覽

| 目錄 / 檔案 | 說明 |
|-------------|------|
| `src/` | 作業系統核心、BSP、網路驅動與 App 任務。主要改動都在此目錄。 |
| `Makefile` | 單一入口：建置 (`make`)、執行 (`make run`) 以及個別測試 (`make test-*`)。 |
| `test/` | QEMU 下的獨立測試程式（定時器、LAN Ping、WAN Ping、UDP Flood）。 |
| `doc/` | 說明文件。`dual_nic_ping_guide.zh.md` 與本文是目前的主要參考。 |
| `os.list` | `make` 後生成的混合反組譯檔，用來對照 C 原始碼與組合語言。 |

---

## 2. 常用指令速查

```bash
make                 # 編譯韌體，輸出 bin/kernel.elf
make clean           # 清除中繼檔
make test            # 依序執行所有測試 (context, LAN, WAN, UDP)
make run             # 啟動 QEMU，同步檢查 LAN/WAN 封包 & 中斷
make test-ping       # 只測 LAN Ping
make test-ping-wan   # 只測 WAN Ping
```

> 注意：`make run` 與 `make test-ping(-wan)` 會使用現成的 TAP 介面 (`qemu-lan`, `qemu-wan`)；若介面已設定給一般使用者，就不需要 sudo。

---

## 3. 建置與修改流程

1. **拉取最新程式碼**：確保兩個終端都在相同 commit。
2. **編譯 (`make`)**：確認 `bin/kernel.elf`、`os.list` 生成。
3. **修改程式**：常見位置
   - `src/app.c`：系統啟動後的流程，現在會自動執行 LAN/WAN Ping。
   - `src/net_ping.c`：共用的 Ping 驗證程式（ARP + ICMP + 中斷計數）。
   - `src/virtio_net.c`：VirtIO 驅動與統計欄位。
4. **跑測試 (`make test`)**：所有測試以 `timeout` 包裝，不會卡住。輸出 PASS/FAIL 判斷即可。
5. **執行 (`make run`)**：觀察 console，有以下關鍵訊息：
   ```
   [RESULT] ...
   [INFO] IRQ delta=5 RX packets=6
   ```
   LAN 與 WAN 都要看到非零的 IRQ / RX 才算成功。
6. **整理檔案**：必要時 `make clean`，檢查 git 狀態無多餘檔案再 commit。

---

## 4. 網路測試注意事項

1. **TAP 建立一次即可**：
   ```bash
   sudo ip tuntap add dev qemu-lan mode tap user $USER
   sudo ip tuntap add dev qemu-wan mode tap user $USER
   sudo ip link set qemu-lan up
   sudo ip link set qemu-wan up
   sudo brctl addif br-lan qemu-lan
   sudo brctl addif br-wan qemu-wan
   ```
   之後無須 sudo 即可 `make run`／`make test`。

2. **橋接 IP**：預設假設 host 端對 `br-lan`/`br-wan` 分別配置 `192.168.1.103` 與 `10.3.5.103`；若不同，請同步調整 `src/app.c` / `test/test_network_ping*.c`。

3. **中斷檢查**：`net_ping_run()` 會在結束時輸出 `IRQ delta` 與 `RX packets`。任一為 0 會直接判定 FAIL，方便 CI 或自動化流程。

4. **多終端一致性**：若在不同終端執行，請確認環境變數（尤其 `PATH`）一致，以免 `ip`、`qemu-system-aarch64` 找不到。

---

## 5. 快速除錯建議

| 症狀 | 建議排查 |
|------|-----------|
| `Device or resource busy` | 前一個 QEMU 未關閉，或 TAP 尚未釋放。可用 `ps`, `ip link` 確認。 |
| `[FAIL] ARP response timeout` | 檢查 bridge 設定、主機是否回應目標 IP。 |
| `[FAIL] No interrupt activity detected` | 封包未達客體：可能 TAP 未 `up`、未加入 bridge、或 host 防火牆阻擋 ARP/ICMP。 |
| `Property 'virtio-net-device.addr' not found` | 確保 QEMU 參數未使用舊版 `addr=` 選項（已移除）。 |

---

## 6. 常見修改情境

- **調整 Ping 次數 / 目標**：修改 `src/app.c` 或 `test/test_network_ping*.c` 的 `app_ping_targets` 陣列即可。
- **新增測試案例**：參考 `test/test_context_timer.c`，在 `test/` 下建立新檔案並於 `Makefile` 的 `TEST_NAMES` 增加項目。
- **引入額外統計**：在 `src/virtio_net.h` 新增欄位，再由 `src/virtio_net.c` 更新計數，最後於 `net_ping_run()` 中輸出。

---

## 7. Commit 前檢查清單

1. `make` / `make test` 均 PASS。
2. `git status` 無多餘檔案（如 `bin/`, `obj/`）。
3. README 或說明文件若有更新，請確認連結正確。
4. 記錄此次變更的測試結果於提交訊息或 PR 說明。

---

## 8. 延伸閱讀

- `doc/dual_nic_ping_guide.zh.md`：更詳細的 TAP/Bridge 操作與疑難排解。
- `os.list`：追蹤組合輸出與函式映射的最佳入口。
- `src/net_protocol.c`：ARP/ICMP 等協定處理實作。

---

祝順利！有改動記得更新文件，讓下一位自動化工具能更快接手。
