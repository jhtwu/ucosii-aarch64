# Network Performance Optimization for ARM KVM

## Overview

This document describes the network performance optimizations implemented in the Makefile for improving TCP throughput in ARM KVM environments.

## Performance Tiers

### Tier 1A: KVM + vhost-net + Multi-queue (Best Performance)
**Requirements:**
- ARM64 host with KVM support
- Access to `/dev/kvm` and `/dev/vhost-net`
- User in `kvm` group or running with sudo
- **Multi-queue TAP interfaces** (created with `make setup-mq-tap`)

**Features:**
- **vhost-net acceleration**: Kernel-space packet processing (bypasses userspace)
- **Multi-queue virtio-net**: 4 queues for parallel packet processing
- **Mergeable receive buffers** (`mrg_rxbuf=on`): Reduces memory overhead
- **Packed virtqueues** (`packed=on`): More efficient descriptor layout
- **Event index** (`event_idx=on`): Reduces VM exits
- **Large TX/RX queues**: 1024 descriptors each

**Expected throughput improvement:** 2-4x compared to software emulation

**How to enable:**
```bash
# Step 1: Create multi-queue TAP interfaces (one-time setup)
make setup-mq-tap

# Step 2: Run with multi-queue enabled
make run VIRTIO_QUEUES=4
```

**Configuration:**
```bash
-netdev tap,vhost=on,queues=4
-device virtio-net-device,mq=on,mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
```

### Tier 1B: KVM + vhost-net (Good Performance, Default)
**Requirements:**
- ARM64 host with KVM support
- Access to `/dev/kvm` and `/dev/vhost-net`
- User in `kvm` group or running with sudo

**Features:**
- **vhost-net acceleration**: Kernel-space packet processing
- Single-queue (compatible with existing TAP interfaces)
- Optimized virtio parameters
- **Large TX/RX queues**: 1024 descriptors each

**Expected throughput improvement:** 1.5-2.5x compared to software emulation

**How to enable:**
```bash
# Automatically enabled on ARM64 with KVM access
make run
```

**Configuration:**
```bash
-netdev tap,vhost=on
-device virtio-net-device,mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
```

### Tier 2: KVM without vhost-net (Good Performance)
**Requirements:**
- ARM64 host with KVM support
- Access to `/dev/kvm` only

**Features:**
- No vhost-net acceleration (userspace packet processing)
- Optimized virtio parameters
- Large TX/RX queues: 1024 descriptors

**Expected throughput improvement:** 1.5-2x compared to software emulation

**Configuration:**
```bash
-netdev tap
-device virtio-net-device,mrg_rxbuf=on,packed=on,event_idx=on,tx_queue_size=1024,rx_queue_size=1024
```

### Tier 3: Software Emulation (Baseline)
**Requirements:**
- Any host architecture (x86_64, ARM64 without KVM, etc.)

**Features:**
- Conservative buffer sizes to reduce overhead
- Basic virtio optimizations

**Configuration:**
```bash
-netdev tap
-device virtio-net-device,mrg_rxbuf=on,event_idx=on,tx_queue_size=512,rx_queue_size=512
```

## Choosing Queue Count

Multi-queue virtio-net allows you to specify the number of queue pairs. The optimal number depends on your workload:

- **VIRTIO_QUEUES=1** (default): Best compatibility, works with all TAP interfaces
- **VIRTIO_QUEUES=2**: Good for 2-vCPU guests with moderate traffic
- **VIRTIO_QUEUES=4**: Recommended for 4-vCPU guests (matches current config)
- **VIRTIO_QUEUES=8**: For 8+ vCPU guests with very high connection count

**Guidelines:**
- Queue count should not exceed vCPU count
- More queues = more memory overhead (64KB per queue in host kernel)
- Multi-queue helps most with many concurrent connections
- Single large connection may not benefit much from multi-queue

**Example:**
```bash
# For best performance with 4 vCPUs
make setup-mq-tap
make run VIRTIO_QUEUES=4
```

## Enabling Full Performance (Tier 1A/1B)

### Option 1: Add User to kvm Group (Recommended)
```bash
sudo usermod -aG kvm $USER
# Log out and log back in for changes to take effect
```

### Option 2: Temporary Permission Change
```bash
sudo chmod 666 /dev/kvm
sudo chmod 666 /dev/vhost-net
```

### Option 3: Run with sudo
```bash
sudo make run
```

## Performance Parameters Explained

### vhost-net Acceleration
- **What:** Moves virtio-net data plane from userspace (QEMU) to kernel space
- **Benefit:** Eliminates context switches between QEMU and kernel for each packet
- **Impact:** Can double TCP throughput in high-bandwidth scenarios

### Multi-queue (mq=on, queues=4)
- **What:** Multiple TX/RX queue pairs (4 in this config)
- **Benefit:** Parallel packet processing across multiple vCPUs
- **Impact:** Scales with connection count and traffic volume
- **Note:** Most beneficial with 4+ vCPUs and many concurrent connections

### Mergeable Receive Buffers (mrg_rxbuf=on)
- **What:** Allows receiving large packets across multiple buffers
- **Benefit:** Reduces buffer waste, supports large MTU without large buffers
- **Impact:** 5-15% throughput improvement, especially for large packets

### Packed Virtqueues (packed=on)
- **What:** More efficient descriptor ring layout (virtio 1.1 feature)
- **Benefit:** Better cache utilization, fewer memory accesses
- **Impact:** 10-20% latency reduction, slight throughput improvement

### Event Index (event_idx=on)
- **What:** Suppresses unnecessary notifications between guest and host
- **Benefit:** Reduces VM exits and interrupts
- **Impact:** Up to 30% reduction in CPU overhead for high packet rates

### TX/RX Queue Size
- **What:** Number of descriptors in each virtqueue (512 or 1024)
- **Benefit:** Larger queues reduce queue full conditions
- **Impact:** Smooths bursty traffic, prevents packet drops

## Checking Current Configuration

Run `make run` to see the detected configuration:
```
=== Platform: ARM64 host with KVM acceleration ===
=== GIC Version: 2 ===
=== Network: KVM with vhost-net acceleration and multi-queue ===
```

## Benchmarking

To measure the performance improvement:

1. **Without optimizations** (old Makefile):
   ```bash
   # Baseline throughput
   iperf3 -c <target> -t 30
   ```

2. **With optimizations** (current Makefile):
   ```bash
   # Make sure you're in Tier 1 (check warnings)
   make run
   # In guest, run same iperf3 test
   ```

3. **Expected results:**
   - Software emulation: ~100-200 Mbps
   - KVM without vhost: ~150-400 Mbps
   - KVM with vhost + multi-queue: ~500-1500+ Mbps

## Troubleshooting

### "vhost-net is not available" Warning
- Cause: No read/write access to `/dev/vhost-net`
- Solution: Follow "Enabling Full Performance" section above

### Performance Still Low
1. Check host CPU load: `top` (if at 100%, vCPU count may be too low)
2. Verify vhost-net is actually being used: `lsof /dev/vhost-net` while running
3. Check for packet drops: look for TX/RX drops in guest `ip -s link`
4. Ensure TAP interfaces are up and properly configured on host

### Multi-queue Not Working
- Ensure guest driver supports multi-queue (check `ethtool -l eth0` in guest)
- Your bare-metal OS may need multi-queue support implemented in driver

## References

- [KVM Multi-queue virtio-net](https://www.linux-kvm.org/page/Multiqueue)
- [QEMU Virtio Networking](https://wiki.qemu.org/Documentation/Networking)
- [Red Hat Virtualization Tuning Guide](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/7/html/virtualization_tuning_and_optimization_guide/sect-virtualization_tuning_optimization_guide-networking-techniques)
