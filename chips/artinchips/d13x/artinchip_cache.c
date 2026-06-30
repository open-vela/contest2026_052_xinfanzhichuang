/****************************************************************************
 * arch/risc-v/src/chip/d13x/d13x_cache.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D13X cache interface implementation for NuttX.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <sys/types.h>

#include <core_rv32.h>

#ifndef CONFIG_ARCH_D13X_ICACHE_SIZE
#define CONFIG_ARCH_D13X_ICACHE_SIZE (32 * 1024)
#endif

#ifndef CONFIG_ARCH_D13X_DCACHE_SIZE
#define CONFIG_ARCH_D13X_DCACHE_SIZE (32 * 1024)
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_ICACHE
size_t up_get_icache_linesize(void) { return CACHE_LINE_SIZE; }

size_t up_get_icache_size(void) { return CONFIG_ARCH_D13X_ICACHE_SIZE; }

void up_enable_icache(void) { csi_icache_enable(); }

void up_disable_icache(void) { csi_icache_disable(); }

void up_invalidate_icache(uintptr_t start, uintptr_t end) {
  (void)start;
  (void)end;
  csi_icache_invalid();
}

void up_invalidate_icache_all(void) { csi_icache_invalid(); }



void up_coherent_dcache(uintptr_t addr, size_t len)
{
  if (len > 0)
    {
#ifdef CONFIG_ARCH_DCACHE
      csi_dcache_clean_range((phy_addr_t)addr, (u32)len);
#endif
      csi_icache_invalid();
    }
}

#ifdef CONFIG_ARCH_ICACHE_LOCK
void up_lock_icache(uintptr_t start, uintptr_t end) {
  (void)start;
  (void)end;
}

void up_unlock_icache(uintptr_t start, uintptr_t end) {
  (void)start;
  (void)end;
}

void up_unlock_icache_all(void) {}
#endif
#endif

#ifdef CONFIG_ARCH_DCACHE
size_t up_get_dcache_linesize(void) { return CACHE_LINE_SIZE; }

size_t up_get_dcache_size(void) { return CONFIG_ARCH_D13X_DCACHE_SIZE; }

void up_enable_dcache(void) { csi_dcache_enable(); }

void up_disable_dcache(void) { csi_dcache_disable(); }

void up_invalidate_dcache(uintptr_t start, uintptr_t end) {
  if (end > start) {
    csi_dcache_invalid_range((phy_addr_t)start, (u32)(end - start));
  }
}

void up_invalidate_dcache_all(void) { csi_dcache_invalid(); }

void up_clean_dcache(uintptr_t start, uintptr_t end) {
  if (end > start) {
    csi_dcache_clean_range((phy_addr_t)start, (u32)(end - start));
  }
}

void up_clean_dcache_all(void) { csi_dcache_clean(); }

void up_flush_dcache(uintptr_t start, uintptr_t end) {
  if (end > start) {
    csi_dcache_clean_invalid_range((phy_addr_t)start, (u32)(end - start));
  }
}

void up_flush_dcache_all(void) { csi_dcache_clean_invalid(); }

#ifdef CONFIG_ARCH_DCACHE_LOCK
void up_lock_dcache(uintptr_t start, uintptr_t end) {
  (void)start;
  (void)end;
}

void up_unlock_dcache(uintptr_t start, uintptr_t end) {
  (void)start;
  (void)end;
}

void up_unlock_dcache_all(void) {}
#endif
#endif