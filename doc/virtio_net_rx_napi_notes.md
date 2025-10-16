# VirtIO RX NAPI-style Refactor (2024-xx-xx)

## Overview

The VirtIO network driver was reworked so that receive processing behaves similar to NAPI:

- The IRQ handler only acknowledges interrupts, pulls packet descriptors out of the VirtIO RX ring, copies
  the payload into a small software ring buffer, and immediately recycles the descriptor back to the device.
- An RTOS task (`virtio_net_rx_task`) dequeues packets from the software queue and calls
  `net_process_received_packet()`. When the queue is empty the task re-enables RX interrupts.
- If the software queue runs out of space, the number of dropped packets is counted (`rx_queue_drops`)
  and the task is woken so backlog is drained as quickly as possible.

## Key changes

1. **Interrupt handler**
   - Calls `virtio_net_service_rx_ring()` to move descriptors into the software queue and return them to
     the device.
   - Turns off RX interrupts and posts a semaphore when the task needs to run.
   - Keeps TX book-keeping intact; IRQ latency is now dominated by the software queue copy only.

2. **Software RX queue**
   - Each device owns a circular buffer of 64 entries (`virtio_net_rx_sw_packet`), each holding one
     aligned packet copy.
   - Queue head/tail counters are protected with `OS_ENTER_CRITICAL()` and the writer publishes `len`
     with a data memory barrier so the task never consumes partially written data.
   - Statistics (`rx_queue_count`, `rx_queue_drops`) help spot overload without touching the
     VirtIO descriptor state.

3. **RX task**
   - Runs in uC/OS-II context, drains the queue, services the network stack, and re-enables interrupts
     when backlog is gone.
   - Handles the last few outstanding descriptors if an interrupt arrived after the queue was emptied.

4. **Fallback handling**
   - The previous design processed packets inside the ISR when the queue overflowed; that path has been
     removed to keep interrupt latency predictable.

## Impact

- IRQ processing time dropped drastically, helping TCP ACK turnaround and reducing retransmissions.
- LAN→WAN `iperf3` throughput improved from ~70–90 Mbps to ~170–180 Mbps in internal testing.
- RX→TX sequencing no longer corrupts buffers; VirtIO descriptors are recycled immediately after being
  staged in software.

## Operational tips

- Monitor `rx_queue_drops` to spot overload; consider raising the queue length or RX task priority if the
  counter increases frequently.
- A driver reinit (`virtio_net_halt()` / `virtio_net_initialize()`) clears software queues and stats between
  long benchmark sessions.
