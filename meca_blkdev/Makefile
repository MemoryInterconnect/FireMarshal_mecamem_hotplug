# OmniXtend Block Device Driver Makefile

# Module name
MODULE_NAME := omni_blkdev_irq

# Kernel module objects
obj-m := $(MODULE_NAME).o

# Kernel source directory
LINUXSRC ?= ../boards/default/linux
ARCH := riscv
CROSS_COMPILE := riscv64-unknown-linux-gnu-

# Current directory
PWD := $(shell pwd)

# Kernel build command
KMAKE := $(MAKE) -C $(LINUXSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) CONFIG_MODVERSIONS=

.PHONY: all clean help

# Default target - build module
all:
	@echo "Building OmniXtend block device driver..."
	$(KMAKE) modules
	@echo "Removing modversions from vermagic..."
	@python3 -c "import sys; \
	data = bytearray(open('$(MODULE_NAME).ko', 'rb').read()); \
	old = b'vermagic=5.7.0-rc3-00006-ga68fa9704ec2 SMP mod_unload modversions riscv\x00'; \
	new = b'vermagic=5.7.0-rc3-00006-ga68fa9704ec2 SMP mod_unload riscv\x00'; \
	new = new[:-1] + b'\x00' * (len(old) - len(new) + 1); \
	idx = data.find(old); \
	data[idx:idx+len(old)] = new if idx != -1 else data[idx:idx+len(old)]; \
	open('$(MODULE_NAME).ko', 'wb').write(data) if idx != -1 else None"
	@echo "Build complete: $(MODULE_NAME).ko"

clean:
	@echo "Cleaning build artifacts..."
	$(KMAKE) clean
	rm -f *.o *.ko *.mod.* *.symvers *.order .*.cmd
	rm -rf .tmp_versions
	@echo "Clean complete"

help:
	@echo "OmniXtend Block Device Driver Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build module (default)"
	@echo "  clean    - Remove all build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  LINUXSRC       - Path to Linux kernel source (default: $(LINUXSRC))"
	@echo "  ARCH           - Target architecture (default: $(ARCH))"
	@echo "  CROSS_COMPILE  - Cross-compiler prefix (default: $(CROSS_COMPILE))"
	@echo ""
	@echo "Examples:"
	@echo "  make                                    # Build with default LINUXSRC"
	@echo "  make LINUXSRC=../boards/default/linux   # Build with custom kernel path"
