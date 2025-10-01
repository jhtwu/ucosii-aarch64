qemu-system-aarch64 -M virt,gic_version=3 -cpu host --enable-kvm  -m 256M -nographic  -serial mon:stdio -kernel  bin/kernel.elf
