#!/bin/bash
# Local test script to verify LAN ping test works before CI/CD

set -e

echo "========================================="
echo "Local LAN Ping Test Verification"
echo "========================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script needs sudo to setup TAP interface"
    echo "Running with sudo..."
    exec sudo bash "$0" "$@"
fi

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    ip link delete qemu-lan 2>/dev/null || true
}

trap cleanup EXIT

# Setup TAP interface
echo "1. Setting up TAP network interface..."
ip tuntap add dev qemu-lan mode tap user ${SUDO_USER:-$USER}
ip addr add 192.168.1.1/24 dev qemu-lan
ip link set qemu-lan up

echo "2. Verifying TAP interface..."
ip addr show qemu-lan

echo "3. Testing host connectivity..."
ping -c 2 -W 1 192.168.1.1 || echo "Warning: Cannot ping self, but TAP is up"

echo "4. Building test binary..."
if [ -z "$SUDO_USER" ]; then
    make test_bin/test_network_ping_lan.elf
else
    sudo -u $SUDO_USER make test_bin/test_network_ping_lan.elf
fi

echo "5. Running QEMU test..."
timeout 15s qemu-system-aarch64 \
    -M virt,gic_version=2 \
    -nographic \
    -serial mon:stdio \
    -cpu cortex-a57 \
    -smp 1 \
    -m 2048M \
    -global virtio-mmio.force-legacy=false \
    -netdev tap,id=net0,ifname=qemu-lan,script=no,downscript=no \
    -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=52:54:00:12:34:56 \
    -kernel test_bin/test_network_ping_lan.elf

echo ""
echo "Test completed!"
