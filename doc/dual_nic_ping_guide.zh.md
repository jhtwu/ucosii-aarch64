# 雙介面網路測試流程說明

本文整理最新的雙介面網路測試方法，協助在本專案中驗證 LAN（`192.168.1.1 ↔ 192.168.1.103`）與 WAN（`10.3.5.99 ↔ 10.3.5.103`）兩條虛擬鏈路的封包與中斷狀態。所有指令皆以一般使用者身份執行，僅在建立 TAP/Bridge 時需要 `sudo`。

---

## 1. 建立與設定 TAP / Bridge

```bash
sudo ip tuntap del dev qemu-lan mode tap 2>/dev/null
sudo ip tuntap del dev qemu-wan mode tap 2>/dev/null

sudo ip tuntap add dev qemu-lan mode tap user $USER
sudo ip tuntap add dev qemu-wan mode tap user $USER

sudo ip link set qemu-lan up
sudo ip link set qemu-wan up

sudo brctl addif br-lan qemu-lan
sudo brctl addif br-wan qemu-wan
```

> 若主機未建立 `br-lan` / `br-wan`，可透過 `sudo make setup-network` 先行建立，再依需求調整介面名稱與 IP。

檢查橋接狀態：

```bash
brctl show
```

應可看到 `qemu-lan` 掛載於 `br-lan`、`qemu-wan` 掛載於 `br-wan`。

---

## 2. 執行整體測試

```bash
make test-dual
```

啟動後韌體會依序執行：

1. `eth_init()` 掃描兩個 VirtIO 介面（IRQ 48 與 IRQ 49）。
2. 針對 LAN 與 WAN 依序送出 ARP + 4 次 ICMP Echo。
3. 回報測試統計與中斷資訊。

成功範例：

```
[RESULT] arp_us=1270 ping_us[min=102 max=956 avg=387] count=4
[PASS] Ping response statistics captured
[INFO] IRQ delta=5 RX packets=6
[RESULT] arp_us=108 ping_us[min=107 max=148 avg=128] count=4
[PASS] Ping response statistics captured
[INFO] IRQ delta=5 RX packets=5
```

`IRQ delta` 與 `RX packets` 代表該介面自測試開始後新增的中斷次數與接收封包數；只要兩者皆非零，即可確認驅動有偵測到封包並觸發中斷。

測試結束後系統進入閒置迴圈，外層 `timeout`（預設 60 秒）會中斷 QEMU，`make` 目標隨之結束。

若僅需啟動 QEMU 而不進行雙介面測試，可使用 `make run`（預設為 bridge、需 `qemu-lan` TAP；若要改用 user-mode，可在指令前加 `NET_MODE=user`）。

---

## 3. 單一測試案例

仍可透過既有目標分別測試：

```bash
make test-ping       # 只測 LAN，需 qemu-lan TAP
make test-ping-wan   # 只測 WAN，需 qemu-wan TAP
```

兩者共用同一個 Ping 驗證程式，輸出與 `make run` 完全一致。

---

## 4. 常見問題

| 症狀 | 排查方式 |
| ---- | -------- |
| `Device or resource busy` | 前一個 QEMU 尚未釋放 TAP，或 TAP 已被其他程序佔用。 |
| `[FAIL] ARP response timeout` | 檢查 host 是否設定了 `192.168.1.103` / `10.3.5.103`，並確認橋接是否連通。 |
| `[FAIL] No interrupt activity detected` | 多半代表沒有封包進入：橋接未加入、介面未 `up`、或主機防火牆擋掉 ARP/ICMP。 |
| `Property 'virtio-net-device.addr' not found` | 請更新 Makefile，確保測試目標未使用舊版的 `addr=` 參數。 |

---

## 5. 參考

- `src/net_ping.c`：共用的 Ping 驗證邏輯與中斷計數。
- `src/app.c`：在作業系統啟動後自動執行 LAN/WAN 驗證。
- `Makefile`：`make run` 及 `make test-*` 的 QEMU 參數配置。
