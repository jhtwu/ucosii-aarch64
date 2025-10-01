TOOLCHAIN = aarch64-thunderx-elf
# TOOLCHAIN = /home/dylan/desktop/kvm/buildroot/output/host/bin/aarch64-buildroot-linux-uclibc
TARGET = kernel.elf
ARMARCH = armv8-a
CORE = cortex-a57
CC = $(TOOLCHAIN)-gcc
AS = $(TOOLCHAIN)-as
SIZE = $(TOOLCHAIN)-size
DUMP = $(TOOLCHAIN)-objdump
OBJCOPY = $(TOOLCHAIN)-objcopy

SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

LFILE = $(SRCDIR)/linker.ld
# Compile flags here
CFLAGS   = -std=gnu11 -w -nostartfiles -fno-exceptions -mcpu=$(CORE) -static -g -I$(SRCDIR) -fstack-usage -mabi=ilp32
AFLAGS   =  -g  -mabi=ilp32 -I$(SRCDIR)
LINKER   = $(CC) -o
# linking flags here
LFLAGS   = -w -T $(LFILE) -nostartfiles -fno-exceptions  -mcpu=$(CORE) -static -g -lc  -mabi=ilp32 


GDB = $(TOOLCHAIN)-gdb
QEMU = qemu-system-arm
QEMU_OPTS = -M virt -cpu cortex-a15  -m 256M -nographic  -serial mon:stdio -kernel 



C_FILES := $(wildcard $(SRCDIR)/*.c)
AS_FILES := $(wildcard $(SRCDIR)/*.S)
OBJECTS_C := $(addprefix $(OBJDIR)/,$(notdir $(C_FILES:.c=.o)))
OBJECTS_S := $(addprefix $(OBJDIR)/,$(notdir $(AS_FILES:.S=.o)))
OBJECTS_ALL := $(OBJECTS_S) $(OBJECTS_C)
rm = rm -f


$(BINDIR)/$(TARGET): remove $(OBJECTS_ALL)
	@mkdir -p $(@D)
	@$(LINKER) $@ $(LFLAGS) $(OBJECTS_ALL)
	@echo "Linking complete!"
	@$(SIZE) $@
	$(DUMP) -S $(BINDIR)/$(TARGET) > os.list
	@if [ -d "/srv/tftpboot/" ]; then cp bin/kernel.elf /srv/tftpboot/;echo "cp bin/kernel.elf /srv/tftpboot/"; fi

$(OBJECTS_ALL) : | obj

$(OBJDIR):
	@mkdir -p $@

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled "$<" successfully!"

$(OBJDIR)/%.o : $(SRCDIR)/%.S
	@$(AS) $(AFLAGS) $< -o $@
	@echo "Assembled "$<" successfully!"

qemu:$(OBJECTS_ALL) $(BINDIR)/$(TARGET)
	$(QEMU) $(QEMU_OPTS) $(BINDIR)/$(TARGET) 

qemu_gdb:$(OBJECTS_ALL) $(BINDIR)/$(TARGET)
	$(QEMU) $(QEMU_OPTS) $(BINDIR)/$(TARGET)  -gdb tcp::2222 -S 

gdb:$(OBJECTS_ALL)
	$(GDB) $(BINDIR)/$(TARGET)

dqemu: all
	$(QEMU) -s -S $(QEMU_OPTS) $(BINDIR)/$(TARGET)

.PHONY: clean
clean:
	@$(rm) $(OBJECTS_ALL)
	@$(rm) -rf $(BINDIR)/*
	@$(rm) -rf obj
	@$(rm) os.list
	@echo "Cleanup complete!"

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(TARGET) 
	@echo "Executable removed!"
