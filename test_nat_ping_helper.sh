#!/bin/bash
#
# NAT Ping Test Helper Script
#
# This script sets up the host-side networking for NAT ping tests and
# optionally monitors traffic to help verify NAT functionality.
#
# Usage:
#   ./test_nat_ping_helper.sh setup      - Setup TAP interfaces and routing
#   ./test_nat_ping_helper.sh monitor    - Monitor ICMP traffic on interfaces
#   ./test_nat_ping_helper.sh respond    - Respond to pings from NAT gateway
#   ./test_nat_ping_helper.sh cleanup    - Remove TAP interfaces
#   ./test_nat_ping_helper.sh test       - Run full test with monitoring
#

set -e

# Network configuration (matching test_network_config.h)
LAN_TAP="qemu-lan"
WAN_TAP="qemu-wan"
LAN_HOST_IP="192.168.1.103"
WAN_HOST_IP="10.3.5.103"
LAN_SUBNET="192.168.1.0/24"
WAN_SUBNET="10.3.5.0/24"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root (needed for TAP setup)
check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "This operation requires root privileges"
        log_info "Please run with sudo: sudo $0 $*"
        exit 1
    fi
}

# Setup TAP interfaces
setup_tap_interfaces() {
    log_info "Setting up TAP interfaces for NAT testing..."

    # LAN TAP interface
    if ip link show "$LAN_TAP" &>/dev/null; then
        log_warn "TAP interface $LAN_TAP already exists, removing..."
        ip link delete "$LAN_TAP"
    fi

    log_info "Creating LAN TAP interface: $LAN_TAP"
    ip tuntap add dev "$LAN_TAP" mode tap user "$SUDO_USER"
    ip addr add "$LAN_HOST_IP/24" dev "$LAN_TAP"
    ip link set "$LAN_TAP" up

    # WAN TAP interface
    if ip link show "$WAN_TAP" &>/dev/null; then
        log_warn "TAP interface $WAN_TAP already exists, removing..."
        ip link delete "$WAN_TAP"
    fi

    log_info "Creating WAN TAP interface: $WAN_TAP"
    ip tuntap add dev "$WAN_TAP" mode tap user "$SUDO_USER"
    ip addr add "$WAN_HOST_IP/24" dev "$WAN_TAP"
    ip link set "$WAN_TAP" up

    # Enable IP forwarding (needed for NAT testing)
    log_info "Enabling IP forwarding..."
    sysctl -w net.ipv4.ip_forward=1 > /dev/null

    # Verify setup
    log_success "TAP interfaces configured:"
    ip addr show "$LAN_TAP" | grep "inet "
    ip addr show "$WAN_TAP" | grep "inet "

    log_info ""
    log_info "Network Topology:"
    log_info "  LAN: $LAN_HOST_IP (host) <-> 192.168.1.1 (NAT gateway)"
    log_info "  WAN: $WAN_HOST_IP (host) <-> 10.3.5.99 (NAT gateway)"
}

# Monitor ICMP traffic
monitor_traffic() {
    log_info "Monitoring ICMP traffic on both interfaces..."
    log_info "Press Ctrl+C to stop monitoring"
    log_info ""

    # Start tcpdump on both interfaces
    tcpdump -i "$LAN_TAP" -n icmp or arp &
    TCPDUMP_LAN_PID=$!

    tcpdump -i "$WAN_TAP" -n icmp or arp &
    TCPDUMP_WAN_PID=$!

    # Wait for user interrupt
    trap "kill $TCPDUMP_LAN_PID $TCPDUMP_WAN_PID 2>/dev/null; exit 0" INT TERM

    wait
}

# Respond to pings from NAT gateway
respond_to_pings() {
    log_info "Listening for pings from NAT gateway (10.3.5.99)..."
    log_info "This will automatically respond to ICMP echo requests"
    log_info "Press Ctrl+C to stop"
    log_info ""

    # The kernel will automatically respond to pings if:
    # 1. IP forwarding is enabled (done in setup)
    # 2. No firewall rules blocking ICMP

    # Disable any firewall rules that might block ICMP
    iptables -I INPUT -i "$WAN_TAP" -p icmp -j ACCEPT 2>/dev/null || true
    iptables -I OUTPUT -o "$WAN_TAP" -p icmp -j ACCEPT 2>/dev/null || true

    log_success "Ready to respond to pings from 10.3.5.99"
    log_info "Monitoring WAN interface for incoming pings..."

    # Monitor for ICMP traffic
    tcpdump -i "$WAN_TAP" -n icmp and "icmp[icmptype] = icmp-echo"
}

# Cleanup TAP interfaces
cleanup() {
    log_info "Cleaning up TAP interfaces..."

    if ip link show "$LAN_TAP" &>/dev/null; then
        ip link delete "$LAN_TAP"
        log_success "Removed $LAN_TAP"
    fi

    if ip link show "$WAN_TAP" &>/dev/null; then
        ip link delete "$WAN_TAP"
        log_success "Removed $WAN_TAP"
    fi

    log_success "Cleanup complete"
}

# Run full test with monitoring
run_test() {
    log_info "========================================="
    log_info "NAT Ping Test - Full Test Suite"
    log_info "========================================="

    # Setup
    check_root
    setup_tap_interfaces

    log_info ""
    log_info "TAP interfaces are ready. Now run the test binary:"
    log_info "  make test-nat-ping"
    log_info ""
    log_info "Or manually with QEMU:"
    log_info "  make run (this will run the main firmware with NAT enabled)"
    log_info ""
    log_info "To monitor traffic, run in another terminal:"
    log_info "  sudo $0 monitor"
    log_info ""

    read -p "Press Enter to start monitoring, or Ctrl+C to exit..."
    monitor_traffic
}

# Display usage
usage() {
    echo "NAT Ping Test Helper Script"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  setup      - Setup TAP interfaces and routing (requires sudo)"
    echo "  monitor    - Monitor ICMP traffic on interfaces (requires sudo)"
    echo "  respond    - Respond to pings from NAT gateway (requires sudo)"
    echo "  cleanup    - Remove TAP interfaces (requires sudo)"
    echo "  test       - Run full test with monitoring (requires sudo)"
    echo ""
    echo "Examples:"
    echo "  sudo $0 setup          # Setup network interfaces"
    echo "  sudo $0 monitor        # Monitor traffic"
    echo "  sudo $0 cleanup        # Clean up when done"
    echo ""
}

# Main script logic
case "${1:-}" in
    setup)
        check_root
        setup_tap_interfaces
        ;;
    monitor)
        check_root
        if ! ip link show "$LAN_TAP" &>/dev/null || ! ip link show "$WAN_TAP" &>/dev/null; then
            log_error "TAP interfaces not found. Run 'sudo $0 setup' first"
            exit 1
        fi
        monitor_traffic
        ;;
    respond)
        check_root
        if ! ip link show "$WAN_TAP" &>/dev/null; then
            log_error "WAN TAP interface not found. Run 'sudo $0 setup' first"
            exit 1
        fi
        respond_to_pings
        ;;
    cleanup)
        check_root
        cleanup
        ;;
    test)
        run_test
        ;;
    *)
        usage
        exit 1
        ;;
esac

exit 0
