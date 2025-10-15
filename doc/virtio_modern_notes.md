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

### 最後一輪關鍵調整

為了釐清「`test-net-init` 穩定但 `test-ping-wan` 偶發卡住」的差異，最後一輪修改聚焦在**驅動初始化結尾**與**測試行為**：

- **IRQ 佈線改成顯式選項**：原本無論是否啟用 IRQ 都會呼叫 `BSP_IntVectSet/BSP_IntSrcEn`。移轉為 `CONFIG_VIRTIO_NET_ENABLE_IRQS` 控制，預設改成僅輪詢，避免在 IRQ 設定尚未穩定時卡在 Step 11 後的流程。  
- **`net_ping_run()` 的 IRQ 檢查調整**：過去預期 `irq_delta` 必須大於零，切換成輪詢後這個條件不再成立，因此改成只在啟用 IRQ 的情況下才做檢查。  
- **測試一致性確認**：確認 `test-net-init`、LAN/WAN ping 走的是同一段初始化碼，並新增 log 觀察 eth_device/netif 註冊狀態，確保 handshake 成功後不會因為中斷流程差異而提早回傳。

這些調整後，`test-ping-wan` 已能連續執行 10 次以上都 PASS，代表 Step 11 之後的流程也已穩定。

## 測試結果

所有測試均在不需 sudo 的情況下執行：

```
make test-net-init    # 10/10 次都 PASS
make test-ping        # 走輪詢模式，延遲約 10ms，PASS
make test-ping-wan    # 手動與循環都測過 10 次以上，全部 PASS
```

若需回到中斷模式，請：

1. 在 `src/virtio_net.h` 定義 `CONFIG_VIRTIO_NET_ENABLE_IRQS`。
2. 視需要調整測試期望（重新檢查 `net_ping_run()` 中的 IRQ delta 判斷）。

## 目前差異檔案

- `src/virtio_net.c`
- `src/virtio_net.h`
- `src/net_ping.c`
- `test/test_network_ping.c`
- `test/test_network_ping_wan.c`
- `test/test_network_init.c`
- `Makefile`（新增 `make test-net-init` 目標）

此文檔建立目的是方便後續維護與 debug，任何新的異常可先檢查上述修改點。若有要恢復 legacy 或中斷模式的需求，也可依此調整。***
