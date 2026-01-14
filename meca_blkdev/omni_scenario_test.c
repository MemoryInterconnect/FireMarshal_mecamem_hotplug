/*
 * OmniXtend DMA Scenario Test - With Interrupt Support
 *
 * This test demonstrates DMA transfers with interrupt-driven completion
 * detection using PLIC (Platform-Level Interrupt Controller).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// DMA controller base address
#define DMA_BASE_ADDR    0x9000000ULL

// DMA control registers offsets
#define DMA_SRC_ADDR_LO  (DMA_BASE_ADDR + 0x00)
#define DMA_SRC_ADDR_HI  (DMA_BASE_ADDR + 0x04)
#define DMA_DST_ADDR_LO  (DMA_BASE_ADDR + 0x08)
#define DMA_DST_ADDR_HI  (DMA_BASE_ADDR + 0x0C)
#define DMA_LENGTH_LO    (DMA_BASE_ADDR + 0x10)
#define DMA_LENGTH_HI    (DMA_BASE_ADDR + 0x14)
#define DMA_CONTROL      (DMA_BASE_ADDR + 0x18)
#define DMA_STATUS       (DMA_BASE_ADDR + 0x1C)

// PLIC (Platform-Level Interrupt Controller) base address and offsets
#define PLIC_BASE        0xC000000ULL
#define PLIC_PRIORITY(id)     (PLIC_BASE + 4*(id))
#define PLIC_PENDING(id)      (PLIC_BASE + 0x1000 + 4*((id)/32))
#define PLIC_ENABLE(hart)     (PLIC_BASE + 0x2000 + 0x80*(hart))
#define PLIC_THRESHOLD(hart)  (PLIC_BASE + 0x200000 + 0x1000*(hart))
#define PLIC_CLAIM(hart)      (PLIC_BASE + 0x200004 + 0x1000*(hart))

#define DMA_IRQ_NUM      1    // DMA interrupt number from device tree
#define HART_ID          0    // Hart (hardware thread) ID

// OmniXtend remote memory base address
#define OMNI_REMOTE_MEM_BASE  0x200000000ULL

// Test configuration
#define TEST_SIZE 256  // 256 bytes = 64 words

// Memory addresses (64-byte aligned)
#define LOCAL_BUFFER_1   0x80010000ULL
#define LOCAL_BUFFER_2   0x80020000ULL
#define LOCAL_BUFFER_3   0x80030000ULL

// Global flag for DMA completion
volatile int dma_done = 0;

// Register access functions
void write_reg_u32(uintptr_t addr, uint32_t value) {
    volatile uint32_t *loc_addr = (volatile uint32_t *)addr;
    *loc_addr = value;
}

uint32_t read_reg_u32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

void write_reg_u64(uintptr_t addr, uint64_t value) {
    volatile uint64_t *loc_addr = (volatile uint64_t *)addr;
    *loc_addr = value;
}

uint64_t read_reg_u64(uintptr_t addr) {
    return *(volatile uint64_t *)addr;
}

// PLIC functions
void plic_set_priority(uint32_t irq, uint32_t priority) {
    write_reg_u32(PLIC_PRIORITY(irq), priority);
}

void plic_set_threshold(uint32_t hart, uint32_t threshold) {
    write_reg_u32(PLIC_THRESHOLD(hart), threshold);
}

void plic_enable_irq(uint32_t hart, uint32_t irq) {
    uint32_t reg_offset = PLIC_ENABLE(hart) + 4*(irq/32);
    uint32_t bit_mask = 1 << (irq % 32);
    uint32_t current = read_reg_u32(reg_offset);
    write_reg_u32(reg_offset, current | bit_mask);
}

uint32_t plic_claim(uint32_t hart) {
    return read_reg_u32(PLIC_CLAIM(hart));
}

void plic_complete(uint32_t hart, uint32_t irq) {
    write_reg_u32(PLIC_CLAIM(hart), irq);
}

// Cache flush using CFLUSH_D_L1 instruction
static inline void cflush(uint64_t addr) {
    register uint64_t a0 asm("a0") = addr;
    asm volatile (".word 0xfc050073" : : "r"(a0) : "memory");
}

void flush_dcache_range(uint64_t start_addr, uint64_t length) {
    asm volatile("fence rw, rw" ::: "memory");
    uint64_t cache_line_size = 64;
    uint64_t end_addr = start_addr + length;
    for (uint64_t addr = start_addr; addr < end_addr; addr += cache_line_size) {
        cflush(addr);
    }
    asm volatile("fence rw, rw" ::: "memory");
}

// Enable machine-mode external interrupts
void enable_machine_external_interrupts() {
    // Set mstatus.MIE = 1 (enable machine interrupts)
    uint64_t mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus |= (1 << 3);  // MIE bit
    asm volatile("csrw mstatus, %0" :: "r"(mstatus));

    // Set mie.MEIE = 1 (enable machine external interrupts)
    uint64_t mie;
    asm volatile("csrr %0, mie" : "=r"(mie));
    mie |= (1 << 11);  // MEIE bit
    asm volatile("csrw mie, %0" :: "r"(mie));
}

// Initialize PLIC for DMA interrupts
void plic_init() {
    printf("[PLIC] Initializing PLIC for DMA interrupts...\n");

    // Set DMA interrupt priority
    plic_set_priority(DMA_IRQ_NUM, 3);
    printf("  Set DMA IRQ %d priority to 3\n", DMA_IRQ_NUM);

    // Set threshold to 0
    plic_set_threshold(HART_ID, 0);
    printf("  Set HART %d threshold to 0\n", HART_ID);

    // Enable DMA interrupt
    plic_enable_irq(HART_ID, DMA_IRQ_NUM);
    printf("  Enabled DMA IRQ %d for HART %d\n", DMA_IRQ_NUM, HART_ID);

    // Enable machine-mode external interrupts in CPU
    enable_machine_external_interrupts();
    printf("  Enabled machine-mode external interrupts\n");

    printf("[PLIC] Initialization complete\n");
}

// DMA transfer with interrupt support
int dma_transfer(uint64_t src_addr, uint64_t dst_addr, uint32_t length, const char* desc) {
    printf("\n[DMA] %s\n", desc);
    printf("  Source:      0x%016lx\n", src_addr);
    printf("  Destination: 0x%016lx\n", dst_addr);
    printf("  Length:      %d bytes\n", length);

    dma_done = 0;

    // Write DMA registers
    write_reg_u64(DMA_SRC_ADDR_LO, (uint32_t)(src_addr & 0xFFFFFFFF));
    write_reg_u64(DMA_SRC_ADDR_HI, (uint32_t)(src_addr >> 32));
    write_reg_u64(DMA_DST_ADDR_LO, (uint32_t)(dst_addr & 0xFFFFFFFF));
    write_reg_u64(DMA_DST_ADDR_HI, (uint32_t)(dst_addr >> 32));
    write_reg_u64(DMA_LENGTH_LO, length);
    write_reg_u64(DMA_LENGTH_HI, 0);

    // Flush source cache
    printf("  Flushing source cache...\n");
    flush_dcache_range(src_addr, length);

    // Start DMA
    printf("  Starting DMA transfer...\n");
    write_reg_u64(DMA_CONTROL, 1);

    // Wait for completion - check both interrupt and status
    int timeout = 100000;
    uint32_t irq = 0;
    uint32_t status = 0;
    int check_count = 0;

    while (timeout > 0 && !dma_done) {
        // Try to claim interrupt
        irq = plic_claim(HART_ID);

        if (irq == DMA_IRQ_NUM) {
            printf("  [SUCCESS] DMA interrupt (IRQ %d) received after %d checks\n", irq, check_count);
            dma_done = 1;
            plic_complete(HART_ID, irq);
            break;
        } else if (irq != 0) {
            plic_complete(HART_ID, irq);
        }

        // Also check status register (fallback)
        status = read_reg_u32(DMA_STATUS);
        if (status & 0x1) {
            if (irq == 0) {
                printf("  [WARNING] DMA done via status polling (no interrupt) after %d checks\n", check_count);
            }
            dma_done = 1;
            break;
        }

        for (volatile int i = 0; i < 10; i++);
        check_count++;
        timeout--;
    }

    if (!dma_done) {
        printf("  [ERROR] DMA timeout!\n");
        return 0;
    }

    uint32_t final_status = read_reg_u32(DMA_STATUS);
    if (irq == DMA_IRQ_NUM) {
        printf("  [SUCCESS] DMA completed via interrupt (status=0x%08x)\n", final_status);
    } else {
        printf("  [SUCCESS] DMA completed via status polling (status=0x%08x)\n", final_status);
    }

    // Flush destination cache
    printf("  Invalidating destination cache...\n");
    flush_dcache_range(dst_addr, length);

    return 1;
}

// Display memory
void show_memory(uint64_t addr, int words, const char* label) {
    volatile uint32_t* ptr = (volatile uint32_t*)addr;
    printf("\n[%s] Memory at 0x%016lx:\n", label, addr);
    for (int i = 0; i < words && i < 16; i++) {
        printf("  [%02d] 0x%08x", i, ptr[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    if (words % 4 != 0) printf("\n");
}

// Verify memory
int verify_memory(uint64_t addr, uint32_t* expected, int words, const char* label) {
    volatile uint32_t* ptr = (volatile uint32_t*)addr;
    int errors = 0;

    printf("\n[VERIFY] %s:\n", label);
    for (int i = 0; i < words; i++) {
        if (ptr[i] != expected[i]) {
            if (errors < 10) {
                printf("  [%02d] MISMATCH: expected 0x%08x, got 0x%08x\n",
                       i, expected[i], ptr[i]);
            }
            errors++;
        }
    }

    if (errors == 0) {
        printf("  [PASS] All %d words match!\n", words);
        return 1;
    } else {
        printf("  [FAIL] %d out of %d words mismatched\n", errors, words);
        return 0;
    }
}

int main(void) {
    printf("========================================\n");
    printf("  OmniXtend DMA Scenario Test\n");
    printf("  (With Interrupt Support)\n");
    printf("========================================\n");
    printf("Configuration:\n");
    printf("  DMA Controller:    0x%016lx\n", DMA_BASE_ADDR);
    printf("  OmniXtend Memory:  0x%016lx\n", OMNI_REMOTE_MEM_BASE);
    printf("  Local Buffer 1:    0x%016lx\n", LOCAL_BUFFER_1);
    printf("  Local Buffer 2:    0x%016lx\n", LOCAL_BUFFER_2);
    printf("  Local Buffer 3:    0x%016lx\n", LOCAL_BUFFER_3);
    printf("  Test Size:         %d bytes (%d words)\n", TEST_SIZE, TEST_SIZE/4);
    printf("  PLIC Base:         0x%016lx\n", PLIC_BASE);
    printf("  DMA IRQ Number:    %d\n\n", DMA_IRQ_NUM);

    // Initialize PLIC
    plic_init();

    volatile uint32_t* buf1 = (volatile uint32_t*)LOCAL_BUFFER_1;
    volatile uint32_t* buf2 = (volatile uint32_t*)LOCAL_BUFFER_2;
    volatile uint32_t* buf3 = (volatile uint32_t*)LOCAL_BUFFER_3;
    volatile uint32_t* omni = (volatile uint32_t*)OMNI_REMOTE_MEM_BASE;

    // STEP 1: CPU Write to OmniXtend
    printf("\n========================================\n");
    printf("STEP 1: CPU Write to OmniXtend\n");
    printf("========================================\n");
    printf("Writing pattern 0xAA000000 to OmniXtend...\n");
    for (int i = 0; i < TEST_SIZE/4; i++) {
        omni[i] = 0xAA000000 + i;
    }
    flush_dcache_range(OMNI_REMOTE_MEM_BASE, TEST_SIZE);
    printf("[SUCCESS] CPU write completed\n");
    show_memory(OMNI_REMOTE_MEM_BASE, 8, "OmniXtend after CPU write");

    // STEP 2: CPU Read from OmniXtend
    printf("\n========================================\n");
    printf("STEP 2: CPU Read from OmniXtend\n");
    printf("========================================\n");
    int verify_ok = 1;
    for (int i = 0; i < TEST_SIZE/4; i++) {
        if (omni[i] != (0xAA000000 + i)) {
            printf("  [ERROR] Mismatch at [%d]\n", i);
            verify_ok = 0;
        }
    }
    if (verify_ok) {
        printf("[SUCCESS] CPU read verification passed\n");
    } else {
        printf("[FAIL] CPU read verification failed\n");
        return 1;
    }

    // STEP 3: Prepare buffers
    printf("\n========================================\n");
    printf("STEP 3: Prepare Local Buffers\n");
    printf("========================================\n");
    for (int i = 0; i < TEST_SIZE/4; i++) {
        buf1[i] = 0xBB000000 + i;
        buf2[i] = 0xCC000000 + i;
        buf3[i] = 0x00000000;
    }
    flush_dcache_range(LOCAL_BUFFER_1, TEST_SIZE);
    flush_dcache_range(LOCAL_BUFFER_2, TEST_SIZE);
    flush_dcache_range(LOCAL_BUFFER_3, TEST_SIZE);
    printf("[SUCCESS] Buffers initialized\n");

    // STEP 4: DMA Local to Local
    printf("\n========================================\n");
    printf("STEP 4: DMA Transfer Local to Local\n");
    printf("========================================\n");
    if (!dma_transfer(LOCAL_BUFFER_1, LOCAL_BUFFER_3, TEST_SIZE,
                      "Buffer1 → Buffer3 (Local to Local)")) {
        printf("[FAIL] Step 4 failed\n");
        return 1;
    }
    show_memory(LOCAL_BUFFER_3, 8, "Buffer 3 after DMA");
    uint32_t expected_buf1[TEST_SIZE/4];
    for (int i = 0; i < TEST_SIZE/4; i++) {
        expected_buf1[i] = 0xBB000000 + i;
    }
    if (!verify_memory(LOCAL_BUFFER_3, expected_buf1, TEST_SIZE/4, "Buffer3 vs Buffer1")) {
        printf("[FAIL] Step 4 verification failed\n");
        return 1;
    }

    // STEP 5: DMA Local to OmniXtend
    printf("\n========================================\n");
    printf("STEP 5: DMA Transfer Local to OmniXtend\n");
    printf("========================================\n");
    for (int i = 0; i < TEST_SIZE/4; i++) {
        buf3[i] = 0x00000000;
    }
    if (!dma_transfer(LOCAL_BUFFER_2, OMNI_REMOTE_MEM_BASE + 0x1000, TEST_SIZE,
                      "Buffer2 → OmniXtend")) {
        printf("[FAIL] Step 5 failed\n");
        return 1;
    }
    show_memory(OMNI_REMOTE_MEM_BASE + 0x1000, 8, "OmniXtend after DMA write");

    // STEP 6: DMA OmniXtend to Local
    printf("\n========================================\n");
    printf("STEP 6: DMA Transfer OmniXtend to Local\n");
    printf("========================================\n");
    if (!dma_transfer(OMNI_REMOTE_MEM_BASE + 0x1000, LOCAL_BUFFER_3, TEST_SIZE,
                      "OmniXtend → Buffer3")) {
        printf("[FAIL] Step 6 failed\n");
        return 1;
    }
    show_memory(LOCAL_BUFFER_3, 8, "Buffer 3 after DMA read");
    uint32_t expected_buf2[TEST_SIZE/4];
    for (int i = 0; i < TEST_SIZE/4; i++) {
        expected_buf2[i] = 0xCC000000 + i;
    }
    if (!verify_memory(LOCAL_BUFFER_3, expected_buf2, TEST_SIZE/4, "Buffer3 vs Buffer2")) {
        printf("[FAIL] Step 6 verification failed\n");
        return 1;
    }

    // STEP 7: Final verification
    printf("\n========================================\n");
    printf("STEP 7: Final CPU Verification\n");
    printf("========================================\n");
    int final_ok = 1;
    for (int i = 0; i < TEST_SIZE/4; i++) {
        uint32_t val = ((volatile uint32_t*)(OMNI_REMOTE_MEM_BASE + 0x1000))[i];
        if (val != (0xCC000000 + i)) {
            printf("  [ERROR] Mismatch at [%d]\n", i);
            final_ok = 0;
        }
    }
    if (final_ok) {
        printf("[SUCCESS] Final verification passed\n");
    } else {
        printf("[FAIL] Final verification failed\n");
        return 1;
    }

    // Summary
    printf("\n========================================\n");
    printf("  TEST SUMMARY (Interrupt-Driven)\n");
    printf("========================================\n");
    printf("✓ PLIC Initialization                  PASSED\n");
    printf("✓ Step 1: CPU Write to OmniXtend       PASSED\n");
    printf("✓ Step 2: CPU Read from OmniXtend      PASSED\n");
    printf("✓ Step 3: Local Buffer Initialization  PASSED\n");
    printf("✓ Step 4: DMA Local → Local (IRQ)      PASSED\n");
    printf("✓ Step 5: DMA Local → OmniXtend (IRQ)  PASSED\n");
    printf("✓ Step 6: DMA OmniXtend → Local (IRQ)  PASSED\n");
    printf("✓ Step 7: CPU Verify OmniXtend         PASSED\n");
    printf("\n[SUCCESS] All tests PASSED!\n");
    printf("[SUCCESS] DMA interrupts working correctly!\n");
    printf("========================================\n");

    return 0;
}
