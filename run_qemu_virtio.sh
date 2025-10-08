#!/bin/bash
# QEMU launch script for VirtIO-Net testing with modern VirtIO (non-legacy)
# Based on NuttX configuration

exec qemu-system-aarch64 \
  -M virt,gic-version=3 \
  -cpu cortex-a57 \
  -smp 4 \
  -m 2G \
  -nographic \
  -global virtio-mmio.force-legacy=false \
  -netdev user,id=net0,hostfwd=tcp::5555-10.0.2.15:23 \
  -device virtio-net-device,netdev=net0 \
  -kernel bin/kernel.elf
