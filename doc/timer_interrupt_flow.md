# Timer Interrupt Flow / 計時器中斷流程

## Overview / 概覽
This project uses the ARMv8 virtual timer to generate the periodic OS tick for µC/OS-II. The firmware programs the EL1 generic timer, routes the interrupt through the GIC, and services it in `BSP_OS_TmrTickHandler()` to drive `OSTimeTick()`.

本專案利用 ARMv8 虛擬計時器產生 µC/OS-II 的週期性系統時脈。韌體會設定 EL1 Generic Timer，透過 GIC 導向中斷，並在 `BSP_OS_TmrTickHandler()` 內呼叫 `OSTimeTick()`，帶動作業系統排程。

## Related Source Files / 相關程式檔案
- `src/app.c` – Calls `BSP_OS_TmrTickInit()` once multitasking is ready. / 在多工啟動後呼叫 `BSP_OS_TmrTickInit()`。
- `src/bsp_os.c` – Implements timer programming (`cp15_virt_timer_init`), the tick handler, and interrupt registration. / 實作計時器設定、Tick 中斷服務程式與 GIC 註冊。
- `src/bsp_os.h` – Declares the timer BSP APIs used by the OS. / 宣告提供給作業系統的計時器介面。
- `src/bsp_int.c` / `src/bsp_int.h` – Generic interrupt controller glue that stores ISR vectors and dispatches them. / GIC 膠合層，負責儲存並分派 ISR。
- `src/exception.c` – Routes the EL1 IRQ vector to `BSP_IntHandler()`. / 將 EL1 IRQ 例外導向 `BSP_IntHandler()`。
- `src/os_cpu_a_vfp-none_a57.S`, `src/vector_cortex-a57.S` – Assembly vectors that branch into the shared C IRQ handler. / 組語向量程式，跳入共用的 C 語言 IRQ 處理流程。
- `src/interrupt.c` – Legacy bare-metal IRQ path (kept for reference). / 保留供參考的舊式裸機 IRQ 處理程式。

## Execution Flow / 執行流程
1. **Application boot / 應用程式啟動**  
   `AppTaskStart()` calls `BSP_OS_TmrTickInit(1000)` after it has created the application tasks, ensuring that the OS tick starts only after µC/OS-II is running.  
   `AppTaskStart()` 於建構完成各任務後才呼叫 `BSP_OS_TmrTickInit(1000)`，確保系統時脈在 µC/OS-II 運作時才啟用。

2. **Timer setup / 計時器設定**  
   `BSP_OS_TmrTickInit()` programs EL1 timer access (`cntkctl_el1`), calls `cp15_virt_timer_init()` to set the virtual timer period, registers `BSP_OS_TmrTickHandler()` as interrupt vector 27 (virtual timer IRQ), and enables the IRQ line.  
   `BSP_OS_TmrTickInit()` 會設定 `cntkctl_el1` 讓 EL1 可使用計時器，呼叫 `cp15_virt_timer_init()` 寫入虛擬計時器週期，將 `BSP_OS_TmrTickHandler()` 掛到 GIC 中的向量 27（虛擬計時器 IRQ），並開啟該中斷。

3. **GIC dispatch / GIC 分派**  
   When the timer expires, the GIC signals the CPU. The exception vectors in `vector_cortex-a57.S`/`os_cpu_a_vfp-none_a57.S` branch to `common_irq_trap_handler()` (C), which immediately calls `BSP_IntHandler()` to acknowledge and dispatch the interrupt.  
   計時器逾時後，GIC 對 CPU 發出 IRQ。`vector_cortex-a57.S` 與 `os_cpu_a_vfp-none_a57.S` 的向量程式會跳到 `common_irq_trap_handler()`，該函式隨即呼叫 `BSP_IntHandler()` 以確認並分派中斷。

4. **Handler execution / 中斷服務程式**  
`BSP_IntHandler()` looks up the registered ISR (`BSP_OS_TmrTickHandler()`), calls it, then writes EOIR to complete the IRQ. `BSP_OS_TmrTickHandler()` invokes `OSTimeTick()`, manages the virtual timer control bits, and reprograms the next deadline via `cp15_virt_timer_init()`.  
`BSP_IntHandler()` 查表取得先前註冊的 ISR（即 `BSP_OS_TmrTickHandler()`），執行完後寫入 EOIR 結束 IRQ；`BSP_OS_TmrTickHandler()` 會呼叫 `OSTimeTick()` 更新系統節拍、處理虛擬計時器控制位元，並透過 `cp15_virt_timer_init()` 安排下一次 Tick。

5. **OS scheduling / 作業系統排程**  
   `OSTimeTick()` updates the µC/OS-II kernel tick count and wakes any time-delayed tasks, enabling standard scheduling.  
   `OSTimeTick()` 會更新 µC/OS-II 的時脈計數並喚醒所有等待逾時的任務，驅動正常的排程行為。

## Legacy Components / 傳統元件
- The previous SP804 helper module has been removed; virtual timer is now the sole tick source.  
  過去的 SP804 輔助模組已移除，目前僅保留虛擬計時器做為系統時脈。

- `interrupt.c` provides an attribute-based bare-metal IRQ handler used before the µC/OS-II port. The µC/OS-II path now goes through `BSP_IntHandler()` instead.  
  `interrupt.c` 為早期裸機架構所使用的 IRQ 處理器，現行 µC/OS-II 流程改由 `BSP_IntHandler()` 管理。

## Notes / 備註
- The virtual timer interval (`cntv_tval_el0`) is currently fixed at `0x96000` (614 000 decimal) in `cp15_virt_timer_init()`. Adjust the value to change the tick frequency. / `cp15_virt_timer_init()` 目前將虛擬計時器間隔設為 `0x96000` (十進位 614 000)，可依需求調整以修改 Tick 頻率。
- Remember to keep the timer initialization after the OS start, otherwise the handler may fire before tasks exist. / 請務必在作業系統啟動後再初始化計時器，避免中斷在任務尚未建立時就觸發。
- If you replace the timer source (e.g., use SP804), replicate the registration flow: initialize hardware, register ISR with `BSP_IntVectSet()`, enable the IRQ, and call `OSTimeTick()` inside the handler. / 若改用其他計時器（如 SP804），請複製同樣的註冊流程：初始化硬體、以 `BSP_IntVectSet()` 註冊 ISR、開啟 IRQ，並在中斷服務程式內呼叫 `OSTimeTick()`。
