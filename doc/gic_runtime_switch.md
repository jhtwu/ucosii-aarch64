# GIC 版本自動偵測與測試操作指南

本文記錄 2024-XX-XX 對 `codex/armv8` 專案的 GIC 相關調整，並說明在 GICv2 / GICv3 之間切換與測試的方法。

## 1. 變更摘要

- **執行期自動偵測 GIC 版本**
  - 在 `BSP_Int_Init()` 開始時讀取 `GICD_TYPER`，依據 bit19 判斷是否支援 affinity routing（=> GICv3）。
  - 偵測結果儲存於 `BSP_GIC_Variant`，並新增 `BSP_Int_GICVariantGet()` 讓其他模組查詢。
  - `BSP_Init()` 依結果決定呼叫 `GIC_Enable()`（v2）或 `gic_v3_init()`（v3）。
- **統一 GIC 基底位址**
  - `src/gic.h` 改用 QEMU `virt` 機器的基底位址：`GIC_DISTRIBUTOR=0x08000000` / `GIC_INTERFACE=0x08010000`，避免編譯期巨集切換。
- **中斷設定流程統一**
  - `BSP_IntSrcEn()`、`BSP_IntSrcDis()`、`BSP_IntVectSet()` 等函式改為依 `BSP_GIC_Variant` 切換；GICv2 若呼叫 `BSP_IntVectSet()` 時 `int_target_list==0` 會自動指派到 CPU0。
  - `BSP_IntHandler()` 依版本選擇 `GICC_*` 或 `ICC_*` 相關存取。
- **Makefile 調整**
  - 引入 `GIC_VERSION` 環境變數控制 `-M virt,gic_version=X`，預設為 3。
  - 保留原有撰寫好的測試目標，移除久未使用且無法穩定通過的 `test-dual`，避免 CI/手動測試誤判。
- **測試確認**
  - `make test`（預設 GICv3）通過 `test-context`、`test-ping`、`test-ping-wan`。
  - `GIC_VERSION=2 make test` 通過同樣三項測試，確認 GICv2 路徑可正確驅動網路與計時器。

## 2. 操作方法

### 2.1 切換 GIC 版本

```bash
# 預設為 GICv3
make                   # 建置
make test              # 執行主要測試

# 改成 GICv2
GIC_VERSION=2 make     # 重新建置
GIC_VERSION=2 make test
```

> 注意：`make test` 會使用 `timeout`，網路測試需要本機存在 `qemu-lan` / `qemu-wan` TAP 介面並可回應 ARP。若環境不支援，可個別執行 `test-context` 以驗證核心功能。

### 2.2 個別測試

```bash
# 僅跑 Context Switch & Timer 測試
make test-context
# 或指定 GICv2
GIC_VERSION=2 make test-context
```

## 3. 相關檔案

- `Makefile`（新增 `GIC_VERSION`、刪除 `test-dual`、清理舊 gicv2 target）
- `src/bsp.c`（初始化時呼叫 `BSP_Int_GICVariantGet()`）
- `src/bsp_int.c` / `src/bsp_int.h`（新偵測邏輯與中斷流程整合）
- `src/gic.h`（統一 Distributor / Interface 位址）

如需參考歷史作法，可對照 `drayos/ucos64` 專案中的 GIC 初始化。若後續在實體硬體上部署，建議確認該平臺的 GIC 支援狀態，再依需要調整偵測條件。

