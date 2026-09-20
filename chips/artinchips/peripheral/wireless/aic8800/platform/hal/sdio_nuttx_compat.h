/*
 * NuttX SDIO Compatibility Layer for AIC8800
 *
 * Maps RT-Thread MMCSD API to NuttX SDIO API
 */

#ifndef _SDIO_NUTTX_COMPAT_H_
#define _SDIO_NUTTX_COMPAT_H_

#include <nuttx/config.h>
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <nuttx/semaphore.h>
#include <semaphore.h>

/* Forward declarations for NuttX SDIO structures */
struct rt_mmcsd_card {
    struct sdio_dev_s *sdio_dev;
    void *drv_priv;
    uint16_t vendor;
    uint16_t device;
    uint8_t func_num;
};

struct rt_mmcsd_host {
    struct sdio_dev_s *sdio_dev;
    sem_t excl_sem;
    uint32_t freq;
    uint32_t clock;
    uint16_t clk_div;
};

struct rt_sdio_device_id {
    uint16_t vendor;
    uint16_t device;
};

struct rt_sdio_func {
    struct rt_mmcsd_card *card;
    uint8_t num;
    void (*irq_handler)(struct sdio_func *);
    void *drv_priv;
};

/* SDIO request structure */
struct rt_mmcsd_req {
    struct rt_mmcsd_cmd *cmd;
    struct rt_mmcsd_data *data;
};

struct rt_mmcsd_cmd {
    uint32_t opcode;
    uint32_t arg;
    uint32_t resp[4];
    int err;
};

struct rt_mmcsd_data {
    uint32_t *buf;
    uint32_t blocks;
    uint32_t blksize;
    int flags;
};

/* Compatibility function prototypes */
int rt_sdio_io_rw_direct(struct rt_mmcsd_card *card, int write,
                          unsigned fn, unsigned addr, uint8_t in, uint8_t *out);
int rt_sdio_io_rw_extended(struct rt_mmcsd_card *card, int write,
                            unsigned fn, unsigned addr, int incr_addr,
                            uint8_t *buf, unsigned blocks, unsigned blksz);
int rt_mmcsd_change_para(struct rt_mmcsd_host *host, int freq);

#endif /* _SDIO_NUTTX_COMPAT_H_ */
