# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This directory contains the OmniXtend block device driver and DMA scenario test code for RISC-V platforms. It is part of the FireMarshal_mecamem_hotplug project, which supports memory hotplug for MECA (Memory Extension and Coherence Architecture) memory.

## Building

```bash
# Requires RISC-V cross-compilation toolchain
make                                    # Build with default kernel path
make LINUXSRC=../boards/default/linux   # Build with custom kernel path
make clean                              # Clean build artifacts
```

Output: `omni_blkdev_irq.ko` - Linux kernel module for RISC-V

## Architecture

### Hardware Configuration
- **Target Architecture**: RISC-V 64-bit
- **DMA Controller Base**: `0x9000000`
- **PLIC (Platform-Level Interrupt Controller) Base**: `0xC000000`
- **OmniXtend Remote Memory Base**: `0x200000000` (512MB default)
- **DMA IRQ Number**: 1
- **Hart ID**: 0

### DMA Register Layout (offsets from DMA_BASE)
- `0x00-0x04`: Source address (LO/HI 32-bit split)
- `0x08-0x0C`: Destination address (LO/HI 32-bit split)
- `0x10-0x14`: Transfer length (LO/HI 32-bit split)
- `0x18`: Control register (bit 0: start transfer)
- `0x1C`: Status register (bit 0: transfer complete)

### Key Concepts
- **Cache Coherency**: RISC-V requires explicit cache flushing using custom instruction `.word 0xfc050073` (CFLUSH_D_L1) with 64-byte cache line alignment
- **DMA Flow**: Setup registers → Flush source cache → Start transfer → Wait for interrupt/poll status → Flush destination cache
- **Interrupt Handling**: Uses PLIC for DMA completion interrupts with fallback polling

## Driver Files

- `omni_blkdev_irq.c` - Main block device driver using blk-mq and interrupts
- `omni_blkdev.h` - Device structure definitions
- `omni_blkdev_common.h` - Shared constants and DMA register definitions
- `Makefile` - Build system for cross-compilation

## Usage

```bash
# Load module (on target RISC-V system)
insmod omni_blkdev_irq.ko
insmod omni_blkdev_irq.ko omni_size_mb=1024  # Custom size

# Verify
ls -l /dev/omniblk
dmesg | grep omniblk

# Basic I/O test
dd if=/dev/zero of=/dev/omniblk bs=4k count=100
dd if=/dev/omniblk of=/dev/null bs=4k count=100

# Unload
rmmod omni_blkdev_irq
```

## Related Components

- **meca_chardev/**: Linux character device driver providing `/dev/omnichar` for user-space OmniXtend memory access via DMA
- **omni_scenario_test.c**: Bare-metal DMA test demonstrating CPU and DMA transfers with PLIC interrupt handling
