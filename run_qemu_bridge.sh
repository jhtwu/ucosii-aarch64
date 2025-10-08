#!/bin/bash

# QEMU launch script with bridge networking for real network testing
# Uses existing qemu-lan tap interface on br-lan bridge

# Check if running as root (needed for tap/bridge networking)
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo) for bridge networking"
    exit 1
fi

TAP_INTERFACE="qemu-lan"

# Verify qemu-lan exists
if ! ip link show $TAP_INTERFACE &> /dev/null; then
    echo "ERROR: $TAP_INTERFACE interface not found!"
    echo "Please create it first or check the interface name"
    exit 1
fi

echo "Using existing tap interface: $TAP_INTERFACE"
echo "Bridge status:"
brctl show br-lan | grep -A 1 "bridge name" || brctl show br-lan
echo ""

echo "Starting QEMU with bridge networking..."
exec qemu-system-aarch64 \
  -M virt,gic-version=3 \
  -cpu cortex-a57 \
  -smp 4 \
  -m 2G \
  -nographic \
  -global virtio-mmio.force-legacy=false \
  -netdev tap,id=net0,ifname=$TAP_INTERFACE,script=no,downscript=no \
  -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=52:54:00:12:34:56 \
  -kernel bin/kernel.elf
