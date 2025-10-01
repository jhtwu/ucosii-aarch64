qemu-system-aarch64 -M virt,gic_version=3 -cpu cortex-a57  -m 384M -nographic  -serial mon:stdio -kernel  bin/kernel.elf
