# LAN Ping Test Documentation

## Overview
This test verifies that ucOS-II can ping a host on the LAN network (192.168.1.1) using TAP bridge networking.

## Test Configuration

### Network Setup
- **Guest IP**: 192.168.1.103
- **Target IP**: 192.168.1.1 (host TAP interface)
- **TAP Interface**: qemu-lan
- **Network**: 192.168.1.0/24

### Configurable IPs
You can override the IPs at compile time:

```c
// In test/test_network_ping_lan.c
#ifndef LAN_TARGET_IP
#define LAN_TARGET_IP {192u, 168u, 1u, 1u}  // Default: 192.168.1.1
#endif

#ifndef LAN_GUEST_IP
#define LAN_GUEST_IP {192u, 168u, 1u, 103u}  // Default: 192.168.1.103
#endif
```

## Local Testing

### Method 1: Using the test script
```bash
./test_ping_locally.sh
```

### Method 2: Manual setup
```bash
# 1. Setup TAP interface (requires sudo)
sudo ip tuntap add dev qemu-lan mode tap user $USER
sudo ip addr add 192.168.1.1/24 dev qemu-lan
sudo ip link set qemu-lan up

# 2. Verify TAP setup
ip addr show qemu-lan
ping -c 2 192.168.1.1

# 3. Build and run test
make test-ping-lan

# 4. Cleanup
sudo ip link delete qemu-lan
```

## CI/CD Testing

### GitHub Actions Workflow
The test runs automatically on:
- Push to main or claude/** branches
- Pull requests to main

### Workflow Steps
1. Install cross-compilation toolchain
2. Build firmware and test binaries
3. Setup TAP network interface
4. Run LAN ping test
5. Report results

### Expected Behavior
- ✅ **PASS**: Guest successfully pings 192.168.1.1
- ❌ **FAIL**: Guest cannot reach 192.168.1.1
- ⚠️ **SKIP**: TAP interface unavailable (non-fatal)

## Troubleshooting

### Test fails with "TAP interface not available"
This is expected if:
- Running without sudo
- TAP interface not created
- Permissions issue with /dev/net/tun

**Solution**: Run with sudo or setup TAP interface manually

### Test fails with "192.168.1.1 is unreachable"
Possible causes:
1. TAP interface not configured properly
2. No route to 192.168.1.0/24
3. Firewall blocking ICMP
4. Network initialization failed in guest

**Debug steps**:
```bash
# Check TAP interface
ip addr show qemu-lan
ip link show qemu-lan

# Check routing
ip route | grep 192.168.1

# Test host connectivity
ping -c 2 192.168.1.1

# Check firewall (if applicable)
sudo iptables -L -n | grep ICMP
```

### GitHub Actions environment issues
In CI/CD, additional considerations:
- TAP interface creation requires sudo (✓ available in GitHub Actions)
- Network isolation in containers
- Limited permissions in some environments

## Verification Checklist

Before considering the test stable:
- [ ] Test passes locally with `./test_ping_locally.sh`
- [ ] Test passes in GitHub Actions CI/CD
- [ ] Test passes consistently (run 3+ times)
- [ ] Error messages are clear and actionable
- [ ] Test completes within timeout (10 seconds)

## Known Limitations

1. **Requires TAP support**: Test cannot run in environments without TAP/TUN
2. **Needs network permissions**: Requires ability to create network interfaces
3. **Fixed timeout**: Test times out after 10 seconds
4. **Single interface**: Only tests one NIC, not dual-NIC NAT setup

## Future Improvements

- [ ] Add environment variable support for IP configuration
- [ ] Support alternative network backends (user-mode fallback)
- [ ] Add latency and packet loss measurements
- [ ] Test with different network topologies
- [ ] Add IPv6 support
