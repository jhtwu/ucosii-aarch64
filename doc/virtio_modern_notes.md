# VirtIO Modern Driver Troubleshooting Notes

本文彙整針對 `virtio-net` 驅動在「只支援 VirtIO 1.0 (modern)」情境下所做的關鍵修正與測試結果，協助日後 debug 或回歸分析。

## 背景問題

- 測試環境的 QEMU 會同時產出 modern 與 legacy/mmio 裝置，原始驅動會嘗試用 legacy 流程握手，造成：
  - `DRIVER_OK` 偶發卡住。
  - 掃描到 `device_id == 0` 的 legacy 介面時，握手會不完整。
- 驅動使用 `malloc` 配置佇列時未保證 4K 對齊，VirtIO 1.0 會要求 ring descriptors 必須 page 對齊。
- `virtio_net_hdr` 未填寫 `num_buffers`（modern 規範新增的欄位），QEMU 會視為不合法。
- 驅動在初始化結尾強行註冊 IRQ，在未真正啟用中斷或在不同核心環境下，可能造成 `BSP_IntSrcEn()` 馬上被其他流程打斷，導致 handshake 熟悉後卻沒有後續輸出。
- 測試框架中 `test-net-init` 與 ping 類測試走的流程不同，導致 `test-net-init` 永遠 PASS，而 `test-ping-wan` 偶發 FAIL。

## 關鍵修改

1. **只與 modern 裝置協商**
   - 判斷特徵時強制要求 `VIRTIO_F_VERSION_1`，並同步協商 `VIRTIO_NET_F_MAC`。
   - 若裝置未提供 `VERSION_1`，直接中止避免回落至 legacy 流程。

2. **4KB 對齊佇列 & 記憶體配置**
   - 新增 `virtio_alloc_queue_mem()`，保證 descriptors / avail / used ring 皆以 4KB 對齊。
   - 設定 queue 前會先清除 `QUEUE_READY`，符合 VirtIO 1.0 的初始化要求。

3. **補齊 modern header 欄位**
   - `struct virtio_net_hdr` 加上 `num_buffers` 欄位（規範新增）。
   - 傳送封包前把 `num_buffers` 設為 0，避免 QEMU 視為壞格式。

4. **IRQ 流程改為可選**
   - 預設只依靠輪詢，避免在不同 CPU/IRQ 設定下卡在 `BSP_IntSrcEn()`。
   - 透過 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 切換是否配置中斷，測試預設為關閉。
   - `net_ping_run()` 在關閉 IRQ 時不再要求 `irq_delta > 0`。

5. **測試覆蓋一致化**
   - 新增 `test/test_network_init.c` 循環跑 10 次，只驗證握手結果，確保 `DRIVER_OK` 在 user-mode networking 下穩定。
   - LAN / WAN ping 現在與 handshake 共享完全相同的初始化流程，只差在網路路徑。

6. **診斷訊息**
   - 針對 queue 設定、IRQ 流程與 netif 註冊新增 Step 對應的 log，遇到卡住時可快速定位。

### 最後一輪關鍵調整（已棄用，見下方最終解決方案）

為了釐清「`test-net-init` 穩定但 `test-ping-wan` 偶發卡住」的差異，最後一輪修改聚焦在**驅動初始化結尾**與**測試行為**：

- **IRQ 佈線改成顯式選項**：原本無論是否啟用 IRQ 都會呼叫 `BSP_IntVectSet/BSP_IntSrcEn`。移轉為 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 控制，預設改成僅輪詢，避免在 IRQ 設定尚未穩定時卡在 Step 11 後的流程。
- **`net_ping_run()` 的 IRQ 檢查調整**：過去預期 `irq_delta` 必須大於零，切換成輪詢後這個條件不再成立，因此改成只在啟用 IRQ 的情況下才做檢查。
- **測試一致性確認**：確認 `test-net-init`、LAN/WAN ping 走的是同一段初始化碼，並新增 log 觀察 eth_device/netif 註冊狀態，確保 handshake 成功後不會因為中斷流程差異而提早回傳。

這些調整後，`test-ping-wan` 已能連續執行 10 次以上都 PASS，代表 Step 11 之後的流程也已穩定。

### 最終解決方案：延遲 IRQ 設置 + 強制中斷模式（2025 年修正）

經過深入測試發現，上述透過 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 切換輪詢/中斷模式的做法仍然不夠穩定。真正的問題在於：

**根本原因**：在 VirtIO 設備初始化過程中（`virtio_net_init_device`）立即調用 `BSP_IntVectSet/BSP_IntSrcEn` 設置中斷時，會與 RTOS 調度器或多核環境產生競爭條件，導致系統偶發性掛起在中斷設置流程中。

**最終修正**：

1. **移除所有條件編譯**
   - 完全移除 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 宏定義及相關 `#ifdef/#ifndef` 條件編譯代碼
   - 強制使用中斷模式，移除所有輪詢模式代碼路徑
   - 從 `virtio_net.h`、`virtio_net.c`、`net_ping.c` 清除條件編譯邏輯

2. **延遲 IRQ 設置時機**（關鍵改動）
   - **之前**：在 `virtio_net_init_device()` 中，每個設備初始化完成後立即設置 IRQ
   - **之後**：在 `virtio_net_initialize()` 中，等待**所有**設備都完成初始化後，才統一為所有設備設置 IRQ
   - 這樣可以避免在設備初始化的關鍵路徑上被中斷打斷，減少競爭條件

3. **程式碼變更**
   ```c
   // src/virtio_net.c - virtio_net_init_device()
   // 移除：
   // BSP_IntVectSet(dev->irq, 0u, 0u, BSP_OS_VirtioNetHandler);
   // BSP_IntSrcEn(dev->irq);

   // src/virtio_net.c - virtio_net_initialize()
   // 新增：在所有設備初始化完成後
   for (size_t i = 0; i < virtio_net_device_count; ++i) {
       struct virtio_net_dev *dev = virtio_net_device_list[i];
       if (dev) {
           BSP_IntVectSet(dev->irq, 0u, 0u, BSP_OS_VirtioNetHandler);
           BSP_IntSrcEn(dev->irq);
       }
   }
   ```

4. **穩定性測試結果**
   - 使用 `test_run_stability.sh` 連續執行 **50 次**初始化測試
   - **結果：50/50 全部通過**，無任何掛起或失敗
   - 驗證了延遲 IRQ 設置完全解決了原本的競爭條件問題

## 測試結果

### 最新測試結果（2025 年修正後）

採用延遲 IRQ 設置方案後，所有測試完全穩定：

```bash
./test_run_stability.sh    # 50/50 次都 PASS（測試 make run 初始化穩定性）
make test-net-init         # 10/10 次都 PASS
make test-ping             # 使用中斷模式，延遲約 1-5ms，PASS
make test-ping-wan         # 使用中斷模式，連續測試 50+ 次全部 PASS
```

**重要**：系統現在**強制使用中斷模式**，`CONFIG_VIRTIO_NET_ENABLE_IRQS` 宏已被移除。若需要輪詢模式（不建議），需要重新實作條件編譯邏輯並確保不會在設備初始化期間設置 IRQ。

### 舊版測試結果（僅供參考）

使用條件編譯方案時的測試結果：

```
make test-net-init    # 10/10 次都 PASS
make test-ping        # 走輪詢模式，延遲約 10ms，PASS
make test-ping-wan    # 手動與循環都測過 10 次以上，全部 PASS
```

## 目前差異檔案

### 2025 年最終版本修改

最新修正涉及以下檔案：

- `src/virtio_net.c` - 延遲 IRQ 設置到 `virtio_net_initialize()`，移除條件編譯
- `src/virtio_net.h` - 移除 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 定義
- `src/net_ping.c` - 移除輪詢模式相關條件編譯代碼
- `test/test_network_ping.c`
- `test/test_network_ping_wan.c`
- `test/test_network_init.c`
- `Makefile` - 新增 `make test-net-init` 目標，調整 `make run` timeout
- `test_run_stability.sh` - 新增穩定性測試腳本（50 次循環測試）

### 關鍵變更點

1. **virtio_net.c:virtio_net_init_device()** - 移除 `BSP_IntVectSet/BSP_IntSrcEn` 調用
2. **virtio_net.c:virtio_net_initialize()** - 新增統一的 IRQ 設置迴圈
3. **net_ping.c** - 移除 `#ifndef CONFIG_VIRTIO_NET_ENABLE_IRQS` 的 `dev->recv(dev)` 輪詢調用
4. **net_ping.c** - 移除 `#ifdef CONFIG_VIRTIO_NET_ENABLE_IRQS` 的 IRQ 檢查條件

## 總結

此文檔記錄了 VirtIO 網路驅動從初版到完全穩定的演進過程。最終解決方案透過**延遲 IRQ 設置時機**並**強制使用中斷模式**，徹底解決了初始化過程中的競爭條件問題。

任何新的異常可先檢查上述修改點。若需要調整 IRQ 設置策略或恢復輪詢模式，務必確保不會在設備初始化關鍵路徑上執行 `BSP_IntSrcEn()`，以避免系統掛起。
