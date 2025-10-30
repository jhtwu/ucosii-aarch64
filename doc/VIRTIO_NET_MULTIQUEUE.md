# VirtIO-Net Multi-Queue Implementation

## Overview

This document describes the multi-queue (MQ) implementation for the VirtIO-Net driver in uC/OS-II AArch64.

Multi-queue support enables parallel packet processing across multiple CPU cores, improving network throughput and reducing latency under high load.

## Implementation Status

### ✅ Completed Features

1. **Feature Negotiation**
   - `VIRTIO_NET_F_MQ` (bit 22): Multi-queue support
   - `VIRTIO_NET_F_CTRL_VQ` (bit 17): Control virtqueue for MQ configuration
   - Runtime detection of device capabilities

2. **Queue Initialization**
   - Support for 1-4 queue pairs (configurable at runtime)
   - Each queue pair consists of:
     - RX queue (even numbers: 0, 2, 4, 6)
     - TX queue (odd numbers: 1, 3, 5, 7)
   - Control queue (queue 8) for MQ commands

3. **TX Path**
   - **Flow-based queue selection** using packet hash
   - Preserves per-flow packet ordering (critical for TCP)
   - Automatic load balancing across queues

4. **RX Path**
   - Independent RX processing task for each queue pair
   - Zero-copy packet processing
   - ISR enqueues packets, tasks process at task level

5. **Control Commands**
   - `VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET` sent after DRIVER_OK
   - Properly timed according to VirtIO specification

## Architecture

### Data Structures

```c
struct virtio_net_queue_pair {
    struct virtio_net_dev *dev;       // Pointer to parent device

    // RX queue
    struct vring_desc *rx_desc;
    struct vring_avail *rx_avail;
    struct vring_used *rx_used;
    u8 *rx_buffers[VIRTIO_NET_QUEUE_SIZE];

    // TX queue
    struct vring_desc *tx_desc;
    struct vring_avail *tx_avail;
    struct vring_used *tx_used;
    u8 *tx_buffers[VIRTIO_NET_QUEUE_SIZE];

    // RX task synchronization
    struct virtio_net_rx_pkt rx_pkt_queue[VIRTIO_NET_RX_PKT_QUEUE_SIZE];
    OS_EVENT *rx_sem;
    OS_STK *rx_task_stack;
    u8 rx_task_prio;

    u16 queue_pair_index;
};

struct virtio_net_dev {
    // Multi-queue configuration
    u16 num_queue_pairs;         // Active queue pairs (1-4)
    u16 max_queue_pairs;         // Device maximum
    struct virtio_net_queue_pair queue_pairs[VIRTIO_NET_MAX_QUEUE_PAIRS];

    // Control queue
    struct vring_desc *ctrl_desc;
    struct vring_avail *ctrl_avail;
    struct vring_used *ctrl_used;
    u8 *ctrl_buffer;

    // ... other fields
};
```

### TX Queue Selection Algorithm

**Problem**: Round-robin queue selection causes TCP out-of-order delivery, severely degrading throughput.

**Solution**: Flow-based hashing ensures packets from the same flow always use the same queue.

```c
// Simple but effective hash function
u32 hash = 0;
for (int i = 0; i < 16; i += 4) {
    hash ^= *(u32 *)(&packet[i]);  // XOR header bytes
}
queue_idx = hash % num_queue_pairs;
```

This preserves per-flow ordering while distributing different flows across queues.

### RX Processing

1. **ISR (fast path)**:
   - Reads used ring from all queue pairs
   - Enqueues packet descriptors to per-queue-pair packet queues
   - Wakes up corresponding RX task via semaphore

2. **RX Task (task level)**:
   - Processes packets from packet queue
   - Calls `net_process_received_packet()` for each packet
   - Recycles buffers back to device
   - Batch notification to reduce VM exits

## Configuration

### Compile-time Configuration

```c
#define VIRTIO_NET_MAX_QUEUE_PAIRS  4   // Maximum supported queue pairs
#define VIRTIO_NET_QUEUE_SIZE       64  // Descriptors per queue
```

### Runtime Detection

The driver automatically detects the device's multi-queue capability:

```c
if (device_features & VIRTIO_NET_F_MQ) {
    dev->max_queue_pairs = config->max_virtqueue_pairs;
    dev->num_queue_pairs = min(max_queue_pairs, VIRTIO_NET_MAX_QUEUE_PAIRS);
} else {
    dev->num_queue_pairs = 1;  // Fall back to single queue
}
```

**No recompilation needed** - the same binary works with both single-queue and multi-queue devices.

### QEMU Configuration

**Single Queue (default)**:
```bash
make run
```

**Multi-Queue (4 queues)**:
```bash
make run VIRTIO_QUEUES=4
```

This configures QEMU with:
```
-netdev tap,id=net0,ifname=qemu-lan,vhost=on,queues=4
-device virtio-net-device,netdev=net0,mq=on,...
```

**Note**: Multi-queue requires TAP interfaces created with `multi_queue` flag:
```bash
make setup-mq-tap  # One-time setup
```

## Performance Characteristics

### Expected Performance Gains

| Scenario | Queue Config | Relative Performance |
|----------|--------------|---------------------|
| Single TCP connection | 1 queue | 1.0x (baseline) |
| Single TCP connection | 4 queues | 1.0-1.2x |
| Multiple TCP connections | 1 queue | 1.0x |
| Multiple TCP connections | 4 queues | 1.5-2.5x |
| High packet rate (small packets) | 1 queue | 1.0x |
| High packet rate (small packets) | 4 queues | 2.0-3.0x |

### Why Multi-Queue Helps

1. **Parallel Processing**: Multiple RX tasks can process packets concurrently on different CPU cores
2. **Reduced Contention**: Less lock contention on queue structures
3. **Better CPU Utilization**: Workload distributed across cores
4. **Lower Latency**: Packets processed faster under load

### Limitations

**RX Traffic Distribution**:
- By default, QEMU/vhost-net may send most RX traffic to queue 0
- Full multi-queue RX requires RSS (Receive-Side Scaling) configuration
- RSS/flow steering is not yet implemented in this driver

**Current Status**:
- TX: Fully utilizes all queues ✓
- RX: Basic support, full utilization requires RSS (future work)

## Testing

### Basic Connectivity Test

```bash
# Start with multi-queue
make run VIRTIO_QUEUES=4

# From host, ping guest
ping 192.168.1.1
```

### Throughput Test

```bash
# On host
iperf3 -s

# In guest (via serial console or network)
iperf3 -c <host-ip> -t 30

# Multiple connections to test multi-queue
iperf3 -c <host-ip> -t 30 -P 4
```

### Verify Multi-Queue Active

Check boot messages:
```
virtio-net: max_virtqueue_pairs=4, using 4 queue pair(s)
virtio-net: RX task created for queue pair 0 (priority 10)
virtio-net: RX task created for queue pair 1 (priority 11)
virtio-net: RX task created for queue pair 2 (priority 12)
virtio-net: RX task created for queue pair 3 (priority 13)
virtio-net: Step 12 - MQ configured for 4 queue pairs
```

## Troubleshooting

### Issue: Network not working with multi-queue

**Symptom**: `could not configure /dev/net/tun: Invalid argument`

**Cause**: TAP interfaces are single-queue, but QEMU configured for multi-queue

**Solution**:
```bash
make setup-mq-tap  # Create multi-queue TAP interfaces
make run VIRTIO_QUEUES=4
```

### Issue: TCP throughput degraded with multi-queue

**Symptom**: Lower throughput with VIRTIO_QUEUES=4 than VIRTIO_QUEUES=1

**Cause**: Out-of-order packet delivery (should be fixed in current implementation)

**Verification**: This should NOT happen with the flow-based hash implementation. If it does, check that the hash function is working correctly.

### Issue: Only queue 0 receiving traffic

**Symptom**: All RX interrupts only for queue pair 0

**Status**: Expected behavior without RSS configuration. TX still benefits from multi-queue.

**Future**: Implement RSS to distribute RX traffic.

## Key Implementation Details

### Control Command Timing

**Critical**: MQ control command must be sent **after** DRIVER_OK status is set:

```c
// 1. Initialize all queues
// 2. Create RX tasks
// 3. Set DRIVER_OK status
virtio_mmio_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_DRIVER_OK);

// 4. NOW send MQ command (device is ready to receive commands)
if (need_mq_cmd) {
    virtio_net_send_ctrl_cmd(dev, VIRTIO_NET_CTRL_MQ,
                            VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET, ...);
}
```

### Task Priorities

RX tasks use unique priorities to avoid conflicts:

```c
qp->rx_task_prio = 10 + device_index * 10 + queue_pair_index;
```

- Device 0, Queue 0: Priority 10
- Device 0, Queue 1: Priority 11
- Device 0, Queue 2: Priority 12
- Device 0, Queue 3: Priority 13
- Device 1, Queue 0: Priority 20
- ...

### Memory Barriers

Proper memory barriers ensure correct operation:

```c
// Before updating avail->idx (visible to device)
__asm__ volatile("dmb sy" ::: "memory");
qp->rx_avail->idx++;

// Before reading used->idx (written by device)
__asm__ volatile("dmb sy" ::: "memory");
last_used = qp->rx_used->idx;
```

## Future Enhancements

### 1. RSS (Receive-Side Scaling)

Implement RSS to distribute RX traffic across queues:

- Configure RSS hash function and key
- Set up indirection table
- Enable `VIRTIO_NET_F_RSS` feature

**Expected gain**: 1.5-2x improvement in multi-connection RX throughput

### 2. Adaptive Queue Selection

Dynamically adjust number of active queues based on load:

- Monitor queue depth and latency
- Activate more queues under high load
- Save power by using fewer queues under low load

### 3. Per-Queue Statistics

Add counters for debugging and optimization:

```c
struct queue_stats {
    u64 tx_packets;
    u64 rx_packets;
    u64 tx_bytes;
    u64 rx_bytes;
    u64 tx_queue_full;
    u64 rx_queue_full;
};
```

## References

- [VirtIO Specification v1.1](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html)
- [Linux KVM Multi-queue](https://www.linux-kvm.org/page/Multiqueue)
- [VirtIO Network Device](https://docs.oasis-open.org/virtio/virtio/v1.1/cs01/virtio-v1.1-cs01.html#x1-2000004)

## Related Documentation

- [VIRTIO_NET_DRIVER.md](VIRTIO_NET_DRIVER.md) - Driver implementation overview
- [NETWORK_PERFORMANCE.md](NETWORK_PERFORMANCE.md) - Performance optimization guide
- [TCP_THROUGHPUT_OPTIMIZATION_GUIDE.md](TCP_THROUGHPUT_OPTIMIZATION_GUIDE.md) - TCP tuning guide

---

**Last Updated**: 2025-10-30
**Status**: Production Ready
**Tested with**: QEMU 8.0+, vhost-net, ARM64 KVM
