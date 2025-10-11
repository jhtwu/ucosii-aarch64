# VirtIO-Net Legacy Interrupt Notes / VirtIO-Net 中斷模式說明

## Overview / 概覽

The current driver (`src/virtio_net.c`) uses the VirtIO-MMIO device exposed by `qemu-system-aarch64` in **legacy interrupt mode**. All queues (TX and RX) share a single interrupt line (default SPI 48 in our `virt` machine). When the device finishes using descriptors in either queue, it sets bit 0 of `VIRTIO_MMIO_INTERRUPT_STATUS` and raises the interrupt.

目前驅動 (`src/virtio_net.c`) 透過 `qemu-system-aarch64` 的 VirtIO-MMIO 裝置運作，並沿用 **legacy 中斷模式**：TX 與 RX 佇列共用單一中斷線（在 `virt` 平台預設為 SPI 48）。只要任一佇列的 descriptor 被裝置消耗，就會在 `VIRTIO_MMIO_INTERRUPT_STATUS` 的 bit 0 置 1 並觸發中斷。

### Interrupt Flow / 中斷流程

1. `BSP_OS_VirtioNetHandler()` 透過 `virtio_mmio_read()` 讀取 `VIRTIO_MMIO_INTERRUPT_STATUS`，並立即寫回 `VIRTIO_MMIO_INTERRUPT_ACK` 清除狀態。  
   `BSP_OS_VirtioNetHandler()` 會先讀取 `VIRTIO_MMIO_INTERRUPT_STATUS` 並立刻寫回 `VIRTIO_MMIO_INTERRUPT_ACK` 清除中斷來源。
2. If bit 0 (`VIRTIO_INT_USED_RING`) is set, the handler updates `tx_last_used` to match `tx_used->idx`. This releases any TX descriptors the device has finished with, so subsequent calls to `virtio_net_send()` see the available space correctly.  
   如果 bit 0 (`VIRTIO_INT_USED_RING`) 被設，處理函式就會把 `tx_last_used` 更新為 `tx_used->idx`，回收裝置已消耗的 TX descriptor，讓 `virtio_net_send()` 計算可用空間時能反映最新狀態。
3. After the TX bookkeeping, the handler calls `virtio_net_rx()` to drain RX completions.  
   完成 TX 狀態同步後，處理函式會呼叫 `virtio_net_rx()` 處理所有 RX 佇列的已完成封包。

Because both queues share the same interrupt status bit, the driver always executes TX clean-up and RX polling together. If either queue produces work, the handler will cover both.  
由於兩個佇列共用同一個狀態位元，中斷處理常式每次都會同時執行 TX 清理與 RX 佇列巡檢；只要任一佇列有完成的工作，這個流程就能涵蓋兩邊。

### Why “fire-and-forget” TX works / 為什麼 TX 可「火力全開」送出

`virtio_net_send()` only enqueues descriptors and notifies the device. It does **not** wait for completion. The completion happens asynchronously, and the interrupt handler updates `tx_last_used`. Under normal circumstances (interrupt enabled and handler installed), `tx_last_used` keeps pace with `tx_used->idx`, so the available-slot calculation stays accurate. If the interrupt path is broken, the queue eventually fills up and `virtio_net_send()` returns `-1`.  
`virtio_net_send()` 只負責將 descriptor 放入佇列並通知裝置，並不等待完成。真正的完成訊號由裝置在背景觸發中斷，處理常式再同步 `tx_last_used`。只要中斷路徑正常，`tx_last_used` 會跟 `tx_used->idx` 保持一致，佇列容量計算就會正確；若中斷未啟用，佇列最終會塞滿並讓 `virtio_net_send()` 回傳 `-1`。

## QEMU Command Line / QEMU 指令列說明

The Makefile (see `Makefile`, variable `QEMU_SOFT_FLAGS`) adds `-global virtio-mmio.force-legacy=false`. This flag tells QEMU to expose the modern VirtIO-MMIO registers (version 2+), while still keeping the legacy interrupt behaviour. We currently negotiate zero features in the driver, so we remain compatible with both legacy and transitional devices, but we still rely on the single legacy interrupt bit described above.  
Makefile (`QEMU_SOFT_FLAGS`) 帶入 `-global virtio-mmio.force-legacy=false`，表示 QEMU 會提供 modern 的 VirtIO-MMIO 版本（v2+），但中斷仍保留 legacy 行為。我們的驅動初始化時協商的 features 為 0，因此同時相容於 legacy/transitional 裝置，但仍依賴上述的單一中斷線設計。

To migrate to per-queue MSI-X interrupts in the future, we would need to:  
若未來要切換到每佇列各自的 MSI-X 中斷，需要：

1. Enable MSI-X or the `VIRTIO_F_NOTIFICATION_DATA` feature in the driver.  
   在驅動中啟用 MSI-X 或 `VIRTIO_F_NOTIFICATION_DATA`。
2. Configure QEMU with `-device virtio-net-device,...,vector=...` or use virtio PCI.  
   在 QEMU 端加上 `-device virtio-net-device,...,vector=...` 或改用 virtio PCI。
3. Register separate interrupt vectors for each queue.  
   在驅動中替每個佇列註冊獨立的中斷向量。

目前的工作流程尚未做到這一步，因此務必確保 legacy interrupt handler 正常工作。  
現階段尚未進行上述調整，請確保 legacy 中斷處理流程正常運作，以免 TX 佇列不會回收。
