/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ARTINCHIP_AIC_OSAL_H_
#define _ARTINCHIP_AIC_OSAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/irq.h>
#ifndef irq_handler_t
typedef xcpt_t irq_handler_t;
#endif
#ifndef irqreturn_t
typedef int irqreturn_t;
#endif
#ifndef pin_irq_handler_t
typedef irqreturn_t (*pin_irq_handler_t)(void *);
#endif
#undef KERNEL_BAREMETAL

#include <aic_common.h>
#include "core_rv32.h"

/* aicos_memcpy */
extern void *aicos_memcpy(void *, const void *, size_t);

/* Common malloc align */
typedef void *(*aicos_malloc1_t)(size_t);
typedef void (*aicos_free1_t)(void *);
typedef void *(*aicos_malloc2_t)(unsigned int, size_t);
typedef void (*aicos_free2_t)(unsigned int, void *);

void *_aicos_malloc_align_(size_t size, size_t align, unsigned int type,
                           void *func);
void _aicos_free_align_(void *ptr, unsigned int type, void *func);

/* Irq */
void aicos_irq_enter(void);
void aicos_irq_exit(void);

/* New define */
typedef void *aicos_thread_t;
typedef void (*aic_thread_entry_t)(void *argument);
typedef void *aicos_sem_t;
typedef void *aicos_mutex_t;
typedef void *aicos_event_t;
typedef void *aicos_queue_t;
typedef void *aicos_wqueue_t;

//--------------------------------------------------------------------+
// Irq API
//--------------------------------------------------------------------+
static inline void aicos_local_irq_save(unsigned long *state) {
  *state = up_irq_save();
}
static inline void aicos_local_irq_restore(unsigned long state) {
  up_irq_restore(state);
}

static inline void aicos_local_irq_disable(void) {}
static inline void aicos_local_irq_enable(void) {}
static inline int aicos_request_irq(unsigned int irq, xcpt_t handler,
                                    unsigned int flags, const char *name,
                                    void *dev) {
  return -1;
}
static inline void aicos_irq_enable(unsigned int irq) {}
static inline void aicos_irq_disable(unsigned int irq) {}

static inline bool aicos_in_irq(void) { return false; }

//--------------------------------------------------------------------+
// Cache API
//--------------------------------------------------------------------+
static inline void aicos_dma_sync(void) { up_clean_dcache_all(); }
static inline void aicos_dcache_invalid_range(unsigned long *addr,
                                              unsigned long size) {
  up_invalidate_dcache((uintptr_t)addr, (uintptr_t)addr + size);
}
static inline void aicos_dcache_clean_range(unsigned long *addr,
                                            unsigned long size) {
  up_clean_dcache((uintptr_t)addr, (uintptr_t)addr + size);
}
static inline void aicos_dcache_clean_invalid_range(unsigned long *addr,
                                                    unsigned long size) {
  up_flush_dcache((uintptr_t)addr, (uintptr_t)addr + size);
}
static inline void aicos_icache_enable(void) { csi_icache_enable(); }
static inline void aicos_icache_disable(void) { csi_icache_disable(); }
static inline void aicos_icache_invalid(void) { csi_icache_invalid(); }
static inline void aicos_dcache_enable(void) { csi_dcache_enable(); }
static inline void aicos_dcache_disable(void) { csi_dcache_disable(); }
static inline void aicos_dcache_invalid(void) { csi_dcache_invalid(); }
static inline void aicos_dcache_clean(void) { csi_dcache_clean(); }
static inline void aicos_dcache_clean_invalid(void) {
  csi_dcache_clean_invalid();
}

//--------------------------------------------------------------------+
// Delay API
//--------------------------------------------------------------------+
static inline void aicos_mdelay(unsigned long msecs) {}
static inline void aicos_udelay(unsigned long usecs) {}

// memory
void *aicos_malloc_try_cma(size_t size);
void *aicos_malloc_align_try_cma(size_t size, size_t align);

#ifdef __cplusplus
}
#endif
#endif
