/**
 * @file   sdio_func.h
 * @brief  SDIO function device abstraction for AIC8800
 *
 * Adapted for NuttX/Vela from original RT-Thread version.
 * Stores a pointer to NuttX's sdio_dev_s for bus operations.
 */

#ifndef _SDIO_FUNC_H_
#define _SDIO_FUNC_H_

#include <nuttx/config.h>
#include <nuttx/sdio.h>
#include <nuttx/semaphore.h>
#include <semaphore.h>

struct sdio_func {
    void (*irq_handler)(struct sdio_func *); /* IRQ callback */
    unsigned int    num;        /* function number */
    unsigned short  vendor;     /* vendor id */
    unsigned short  device;     /* device id */
    void *drv_priv;             /* driver private data */
};

/* Global NuttX SDIO device reference (set during init) */
extern struct sdio_dev_s *g_nuttx_sdiodev;

/* SDIO bus mutex for exclusive access */
extern mutex_t g_sdio_bus_lock;

#endif /* _SDIO_FUNC_H_ */
