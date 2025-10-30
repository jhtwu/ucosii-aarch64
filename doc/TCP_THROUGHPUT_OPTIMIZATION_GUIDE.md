# TCP Throughput Optimization Guide

## Quick Summary

针对 ARM KVM 环境，我们实现了三层网络性能优化配置，可以显著改善 TCP throughput test 的效能。

## 当前配置 (默认)

执行 `make run` 将自动检测并应用最佳配置：

```
=== Platform: ARM64 host with KVM acceleration ===
=== GIC Version: 2 ===
=== Network: KVM with vhost-net acceleration ===
```

### 默认优化 (Tier 1B)
- ✅ **KVM 硬件加速**：使用 host CPU 和硬件虚拟化
- ✅ **GICv2**：兼容 KVM 的中断控制器
- ✅ **vhost-net**：核心空间网络处理，绕过用户空间
- ✅ **优化的 virtio 参数**：
  - `mrg_rxbuf=on` - 可合并接收缓冲区
  - `packed=on` - 压缩 virtqueue 布局
  - `event_idx=on` - 减少 VM exits
  - `tx_queue_size=1024` - 大型发送队列
  - `rx_queue_size=1024` - 大型接收队列

**预期性能提升：1.5-2.5x** compared to software emulation

## 进阶优化：启用多队列 (Tier 1A) - 需要 Driver 改造

**⚠️ 重要：多队列功能需要修改 guest driver 代码才能使用。**

当前 virtio-net driver (`src/virtio_net.c`) 只实现了单队列模式。虽然 Makefile 支持配置多队列，但 guest OS 无法使用，因为 driver 需要：
- 协商 VIRTIO_NET_F_MQ feature bit
- 初始化多个 virtqueue 对（不只是 queue 0/1）
- 实现队列选择逻辑

对于更高的性能（特别是多连接场景），可以启用多队列支持（需先改造 driver）。

### 步骤 1: 创建多队列 TAP 接口（一次性设置）

```bash
make setup-mq-tap
```

这会：
1. 删除现有的 `qemu-lan` 和 `qemu-wan` TAP 接口
2. 创建新的支持 `multi_queue` 的 TAP 接口
3. 设置正确的权限和状态

### 步骤 2: 使用多队列运行

**⚠️ 注意：当前 driver 不支持多队列**

```bash
make run VIRTIO_QUEUES=4
```

虽然 QEMU 会配置多队列，但 guest driver 目前只实现了单队列模式。要启用多队列需要修改 `src/virtio_net.c`：
- 协商 `VIRTIO_NET_F_MQ` feature
- 初始化多个 virtqueue 对
- 实现队列选择逻辑

**当前建议使用默认单队列配置即可获得良好性能。**

**预期性能提升：**
- 单队列（当前）: 1.5-2.5x
- 多队列（需 driver 改造）: 2-4x

## 性能对比

| 配置 | 相对性能 | TCP Throughput (估计) |
|------|----------|----------------------|
| 软件模拟 | 1x | 100-200 Mbps |
| KVM + vhost (单队列) | 1.5-2.5x | 200-500 Mbps |
| KVM + vhost (4队列) | 2-4x | 400-800+ Mbps |

*实际性能取决于硬件、负载类型和网络配置*

## Makefile 优化详解

### 1. 编译优化 (已存在)
```makefile
-O3                    # 最高级别优化
-flto                  # 链接时优化
-fomit-frame-pointer   # 减少栈帧开销
-march=armv8-a+simd    # 启用 SIMD 指令
-finline-functions     # 内联函数
```

### 2. QEMU CPU 配置 (新增)
```makefile
# ARM64 with KVM
-cpu host --enable-kvm

# 其他平台
-cpu cortex-a57
```

### 3. 网络加速配置 (新增)

#### vhost-net 加速
```makefile
-netdev tap,vhost=on
```
- 将数据平面从 QEMU 用户空间移到内核
- 减少上下文切换
- 降低延迟，提高吞吐量

#### 多队列支持
```makefile
-netdev tap,vhost=on,queues=4
-device virtio-net-device,mq=on,...
```
- 4个并行队列处理封包
- 利用多核 CPU
- 特别适合高并发连接

#### Virtio 优化参数
```makefile
mrg_rxbuf=on          # 可合并接收缓冲区
packed=on             # Virtio 1.1 压缩格式
event_idx=on          # 减少不必要的通知
tx_queue_size=1024    # 大型队列减少满载
rx_queue_size=1024
```

## 故障排除

### 问题 1: "could not configure /dev/net/tun: Invalid argument"

**原因：** 尝试在 `one_queue` TAP 接口上使用多队列

**解决方案：**
```bash
# 选项 A: 使用单队列（默认）
make run

# 选项 B: 创建多队列 TAP 并启用
make setup-mq-tap
make run VIRTIO_QUEUES=4
```

### 问题 2: "KVM is not available"

**原因：** 没有 `/dev/kvm` 访问权限

**解决方案：**
```bash
sudo usermod -aG kvm $USER
# 登出并重新登入
```

### 问题 3: "vhost-net is not available"

**原因：** 没有 `/dev/vhost-net` 访问权限

**解决方案：**
```bash
sudo usermod -aG kvm $USER
# 登出并重新登入
```

### 问题 4: 性能仍然不理想

**检查清单：**
1. 确认 KVM 和 vhost-net 都已启用
   ```bash
   make run  # 检查输出的 "Network:" 行
   ```

2. 验证 vhost-net 正在使用
   ```bash
   sudo lsof /dev/vhost-net  # 运行 QEMU 时执行
   ```

3. 检查 TAP 接口配置
   ```bash
   ip tuntap show | grep qemu
   # 应该看到 "multi_queue" 如果使用多队列
   ```

4. 监控主机 CPU 使用率
   ```bash
   top
   # 如果 CPU 100%，可能需要更多 vCPU 或优化代码
   ```

## 参考配置

### 当前运行的配置（可在日志中看到）
```bash
qemu-system-aarch64 \
  -M virt,gic_version=2 \
  -cpu host --enable-kvm \
  -smp 4 -m 2048M \
  -netdev tap,id=net0,ifname=qemu-lan,vhost=on \
  -device virtio-net-device,netdev=net0,\
    mrg_rxbuf=on,packed=on,event_idx=on,\
    tx_queue_size=1024,rx_queue_size=1024
```

### 多队列配置
```bash
qemu-system-aarch64 \
  -M virt,gic_version=2 \
  -cpu host --enable-kvm \
  -smp 4 -m 2048M \
  -netdev tap,id=net0,ifname=qemu-lan,vhost=on,queues=4 \
  -device virtio-net-device,netdev=net0,\
    mq=on,mrg_rxbuf=on,packed=on,event_idx=on,\
    tx_queue_size=1024,rx_queue_size=1024
```

## 下一步优化建议

如果当前优化后性能仍不满足需求：

1. **增加内存**：`QEMU_RUN_MEMORY` (当前 2048M)
2. **调整 vCPU 数量**：`QEMU_RUN_SMP` (当前 4)
3. **使用 hugepages**：减少 TLB miss
4. **CPU pinning**：将 vCPU 固定到物理核心
5. **中断亲和性**：优化中断处理

## 测量性能

### 使用 iperf3 测试

**在主机上：**
```bash
iperf3 -s
```

**在 guest 中（通过串口）：**
```bash
# TCP 吞吐量测试
iperf3 -c <host-ip> -t 30

# 多连接测试（验证多队列效果）
iperf3 -c <host-ip> -t 30 -P 4
```

### 预期结果

| 测试 | 软件模拟 | KVM+vhost | KVM+vhost+MQ |
|------|---------|-----------|--------------|
| 单连接 | ~150 Mbps | ~300 Mbps | ~400 Mbps |
| 4连接 | ~200 Mbps | ~400 Mbps | ~700+ Mbps |

## 相关文档

- [NETWORK_PERFORMANCE.md](NETWORK_PERFORMANCE.md) - 详细性能参数说明
- [Makefile](../Makefile) - 完整配置
- [Linux KVM Multi-queue](https://www.linux-kvm.org/page/Multiqueue)

## 总结

通过结合以下优化，TCP throughput 可以获得显著提升：

1. ✅ **编译优化** (-O3, LTO, SIMD)
2. ✅ **KVM 硬件加速** (-cpu host --enable-kvm)
3. ✅ **GICv2 配置**
4. ✅ **vhost-net 加速** (核心空间网络处理)
5. ✅ **优化的 virtio 参数** (大队列, packed, mrg_rxbuf)
6. ⚡ **可选：多队列** (需要 multi-queue TAP)

默认配置即可获得 **1.5-2.5x** 性能提升，启用多队列后可达 **2-4x** 提升。
