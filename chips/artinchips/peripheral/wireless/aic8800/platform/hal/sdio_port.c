/*
 * Copyright (C) 2018-2024 AICSemi Ltd.
 *
 * All Rights Reserved
 */

//-------------------------------------------------------------------
// Driver Header Files
//-------------------------------------------------------------------
#include "sys_al.h"
#include "wifi_al.h"
#include "sdio_al.h"
#include "sdio_def.h"
#include "sdio_port.h"
#include "wifi_port.h"
#include "rtos_port.h"
#include "rtos_errno.h"
#include "aic_plat_log.h"
#include "aic_plat_mem.h"
#include "aic_plat_hal.h"
#ifdef CONFIG_OOB
#include "aic_plat_gpio.h"
#endif /* CONFIG_OOB */
#include <nuttx/config.h>
#include <errno.h>
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <nuttx/kmalloc.h>
#include "aic_sdio.h"

uint32_t  DEVICEID = 0xc18d;	// 8800DC/DW/DL  // wait for using.
#define DRIVER_CHIP_ID  PRODUCT_ID_AIC8800D80

#define CONFIG_SDIO_HOST_MUTEX  0 // enabled only if the platform not support sdio claim/release host api
#define AIC8800_SDIO_MAX_CLOCK_HZ 20000000
#define AIC_SDIO_CCCR_REV       0x00
#define AIC_SDIO_CCCR_IOEN      0x02
#define AIC_SDIO_CCCR_IORDY     0x03
#define AIC_SDIO_CCCR_INTEN     0x04
#define AIC_SDIO_CCCR_BUS_IF    0x07
#define AIC_SDIO_CCCR_CIS_PTR   0x09
#define AIC_SDIO_FBR_BASE(fn)   ((fn) * 0x100)
#define AIC_SDIO_FBR_CIS_PTR    0x09
#define AIC_SDIO_CISTPL_NULL    0x00
#define AIC_SDIO_CISTPL_MANFID  0x20
#define AIC_SDIO_CISTPL_END     0xff
#define AIC_SDIO_CIS_SCAN_LIMIT 256

//-------------------------------------------------------------------
// Driver Variables define
//-------------------------------------------------------------------
/* sdio base */
bool_l func_flag_tx = true;
bool_l func_flag_rx = true;

int msgcfm_poll_en = 1;
static uint32_t sdio_block_size;
uint8_t sdio_unexcept_test = 0;
struct aic_sdio_dev sdio_dev = {NULL,};
static uint32_t g_sdio_cmd53_log_count;

struct sdio_func *sdio_function[SDIOM_MAX_FUNCS];
static void (*sdio_irq_cb)(void*);
#if CONFIG_SDIO_HOST_MUTEX
rtos_mutex sdio_host_mutex = NULL;
#endif

/* NuttX SDIO device pointer - set during probe/init */
static struct sdio_dev_s *g_nuttx_sdio_dev = NULL;

//-------------------------------------------------------------------
// Driver Import functions
//-------------------------------------------------------------------


//-------------------------------------------------------------------
// Driver Macros Functions
//-------------------------------------------------------------------
#define	SDIO_PRE_OP() {\
	if(!(wifi_chip_status_get()))\
		return -1;\
}

#define	SDIO_PRE_OP_B() {\
	if(!(wifi_chip_status_get()))\
		return false;\
}

#define	SDIO_PRE_OPV(v) {\
	if(!(wifi_chip_status_get()))\
	{\
		*v=-1;\
		return;\
	}\
}

#define	SDIO_PRE_OPVR(v) {\
	if(!(wifi_chip_status_get()))\
	{\
		*v=-1;\
		return -1;\
	}\
}

#define	SDIO_WR_FAIL(ret){\
	if(ret)\
		sdio_dev.wr_fail++;\
	else\
		sdio_dev.wr_fail = 0;\
	if(sdio_dev.wr_fail > 15)\
	{\
		aic_driver_unexcepted(0x04);\
	}\
}


#define SDIO_ANY_ID (~0)

//-------------------------------------------------------------------
// Driver functions define
//-------------------------------------------------------------------

/* Helper: get NuttX sdio_dev_s from a sdio_func */
static inline struct sdio_dev_s *get_nuttx_dev(struct sdio_func *func)
{
    if (func && func->drv_priv) {
        return (struct sdio_dev_s *)func->drv_priv;
    }
    return g_nuttx_sdio_dev;
}

static void aic_sdio_log_cmd53(const char *op, struct sdio_func *func,
                               unsigned int addr, int count, int ret,
                               const uint8_t *buf)
{
    int i;
    int dump_len;

    if (g_sdio_cmd53_log_count >= 40) {
        return;
    }

    g_sdio_cmd53_log_count++;
    dump_len = count > 8 ? 8 : count;
    AIC_LOG_PRINTF("sdio %s[%lu]: fn=%d addr=0x%x count=%d ret=%d blk=%lu data:",
                   op, (unsigned long)g_sdio_cmd53_log_count,
                   func ? func->num : -1, addr, count, ret,
                   (unsigned long)sdio_block_size);
    if (buf && dump_len > 0) {
        for (i = 0; i < dump_len; i++) {
            AIC_LOG_PRINTF(" %02x", buf[i]);
        }
    }
    AIC_LOG_PRINTF("\n");
}

static int aic_sdio_read_f0(struct sdio_dev_s *dev, uint32_t addr, uint8_t *val)
{
    if (dev == NULL || val == NULL) {
        return -EINVAL;
    }

    return sdio_io_rw_direct(dev, false, 0, addr, 0, val);
}

static int aic_sdio_read_cis_ptr(struct sdio_dev_s *dev, uint32_t base,
                                 uint32_t *ptr)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    int ret;

    ret = aic_sdio_read_f0(dev, base + AIC_SDIO_CCCR_CIS_PTR, &b0);
    if (ret < 0) {
        return ret;
    }

    ret = aic_sdio_read_f0(dev, base + AIC_SDIO_CCCR_CIS_PTR + 1, &b1);
    if (ret < 0) {
        return ret;
    }

    ret = aic_sdio_read_f0(dev, base + AIC_SDIO_CCCR_CIS_PTR + 2, &b2);
    if (ret < 0) {
        return ret;
    }

    *ptr = ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;
    return 0;
}

static int aic_sdio_parse_manfid(struct sdio_dev_s *dev, uint32_t cis_ptr,
                                 uint16_t *vendor, uint16_t *device)
{
    uint32_t addr = cis_ptr;
    uint32_t scanned = 0;
    uint8_t tuple;
    uint8_t link;
    uint8_t data[4];
    int ret;
    int i;

    if (cis_ptr == 0 || cis_ptr == 0x00ffffff) {
        return -EINVAL;
    }

    while (scanned < AIC_SDIO_CIS_SCAN_LIMIT) {
        ret = aic_sdio_read_f0(dev, addr, &tuple);
        if (ret < 0) {
            return ret;
        }

        if (tuple == AIC_SDIO_CISTPL_NULL) {
            addr++;
            scanned++;
            continue;
        }

        if (tuple == AIC_SDIO_CISTPL_END) {
            return -ENOENT;
        }

        ret = aic_sdio_read_f0(dev, addr + 1, &link);
        if (ret < 0) {
            return ret;
        }

        if (link == 0) {
            return -ENOENT;
        }

        if (tuple == AIC_SDIO_CISTPL_MANFID && link >= sizeof(data)) {
            for (i = 0; i < (int)sizeof(data); i++) {
                ret = aic_sdio_read_f0(dev, addr + 2 + i, &data[i]);
                if (ret < 0) {
                    return ret;
                }
            }

            *vendor = ((uint16_t)data[1] << 8) | data[0];
            *device = ((uint16_t)data[3] << 8) | data[2];
            return 0;
        }

        addr += 2 + link;
        scanned += 2 + link;
    }

    return -ENOENT;
}

static void aic_sdio_dump_card_info(struct sdio_func *func)
{
    struct sdio_dev_s *dev = get_nuttx_dev(&func[FUNC_0]);
    uint16_t common_vendor = 0;
    uint16_t common_device = 0;
    uint8_t rev = 0;
    uint8_t ioen = 0;
    uint8_t iordy = 0;
    uint8_t inten = 0;
    uint8_t busif = 0;
    uint32_t cis_ptr = 0;
    int ret;
    int i;

    if (dev == NULL) {
        AIC_LOG_PRINTF("sdio: no NuttX SDIO device for CIS dump\n");
        return;
    }

    aic_sdio_read_f0(dev, AIC_SDIO_CCCR_REV, &rev);
    aic_sdio_read_f0(dev, AIC_SDIO_CCCR_IOEN, &ioen);
    aic_sdio_read_f0(dev, AIC_SDIO_CCCR_IORDY, &iordy);
    aic_sdio_read_f0(dev, AIC_SDIO_CCCR_INTEN, &inten);
    aic_sdio_read_f0(dev, AIC_SDIO_CCCR_BUS_IF, &busif);
    AIC_LOG_PRINTF("sdio CCCR rev=0x%02x ioen=0x%02x iordy=0x%02x inten=0x%02x busif=0x%02x\n",
                   rev, ioen, iordy, inten, busif);

    ret = aic_sdio_read_cis_ptr(dev, 0, &cis_ptr);
    if (ret == 0) {
        ret = aic_sdio_parse_manfid(dev, cis_ptr, &common_vendor,
                                    &common_device);
    }

    if (ret == 0) {
        AIC_LOG_PRINTF("sdio common CIS ptr=0x%06lx manfid=%04x:%04x\n",
                       (unsigned long)cis_ptr, common_vendor, common_device);
    } else {
        AIC_LOG_PRINTF("sdio common CIS ptr=0x%06lx manfid read failed: %d\n",
                       (unsigned long)cis_ptr, ret);
    }

    for (i = 0; i < SDIOM_MAX_FUNCS; i++) {
        uint32_t ptr_base = i == FUNC_0 ? 0 : AIC_SDIO_FBR_BASE(i);
        uint16_t vendor = common_vendor;
        uint16_t device = common_device;

        cis_ptr = 0;
        ret = aic_sdio_read_cis_ptr(dev, ptr_base, &cis_ptr);
        if (ret == 0) {
            ret = aic_sdio_parse_manfid(dev, cis_ptr, &vendor, &device);
        }

        if (ret == 0 || (common_vendor != 0 && common_device != 0)) {
            func[i].vendor = vendor;
            func[i].device = device;
        }

        AIC_LOG_PRINTF("sdio func%d CIS ptr=0x%06lx manfid=%04x:%04x ret=%d\n",
                       i, (unsigned long)cis_ptr, func[i].vendor,
                       func[i].device, ret);
    }
}

/* AIC driver sdio base functions */
void aic_sdio_set_clock(uint32_t clk)
{
    struct sdio_dev_s *dev = get_nuttx_dev(sdio_function[FUNC_0]);
    if (dev == NULL) {
        return;
    }
    if (clk == 0 || clk > AIC8800_SDIO_MAX_CLOCK_HZ) {
        clk = AIC8800_SDIO_MAX_CLOCK_HZ;
    }

    DBG_SDIO_INF("host set clock %d Hz\n", clk);

    if (clk <= 400000) {
        SDIO_CLOCK(dev, CLOCK_IDMODE);
    } else if (clk <= 25000000) {
        SDIO_CLOCK(dev, CLOCK_SD_TRANSFER_1BIT);
    } else {
        SDIO_CLOCK(dev, CLOCK_SD_TRANSFER_4BIT);
    }
}

int aic_sdio_enable_func(struct sdio_func *func)
{
    int func_num = func->num;
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    if (dev && func_num <= SDIOM_MAX_FUNCS) {
        return sdio_enable_function(dev, (uint8_t)func_num);
    }
    return -1;
}

int xhci_sdio_disable_func(struct sdio_func *func)
{
    /* NuttX does not provide sdio_disable_function in the standard API.
     * Stub: the host driver handles this. */
    (void)func;
    return 0;
}

void sdio_claim_host(struct sdio_func *func)
{
    /* NuttX SDIO framework handles locking internally via
     * sdio_takelock/sdio_givelock (dev->mutex). No-op here to avoid
     * recursive lock with nxmutex_lock. */
    (void)func;
}

void sdio_release_host(struct sdio_func *func)
{
    /* No-op - see sdio_claim_host comment */
    (void)func;
}

unsigned char sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
    uint8_t val = 0;
    int ret;
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    if (dev) {
        ret = sdio_io_rw_direct(dev, false, func->num, addr, 0, &val);
    } else {
        ret = -ENODEV;
    }
    if (err_ret) {
        *err_ret = ret;
    }
    return val;
}

void sdio_writeb(struct sdio_func *func, unsigned char b, unsigned int addr, int *err_ret)
{
    int ret;
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    if (dev) {
        ret = sdio_io_rw_direct(dev, true, func->num, addr, b, NULL);
    } else {
        ret = -ENODEV;
    }
    if (err_ret) {
        *err_ret = ret;
    }
}

int sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr, int count)
{
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    uint8_t *buf = dst;
    int ret;

    if (!dev) return -ENODEV;
    if (count <= 0) return 0;

    if (sdio_block_size > 0 && count >= (int)sdio_block_size) {
        unsigned int nblocks = (unsigned int)count / sdio_block_size;
        unsigned int len = nblocks * sdio_block_size;

        ret = sdio_io_rw_extended(dev, false, func->num, addr, false,
                                  buf, sdio_block_size, nblocks);
        aic_sdio_log_cmd53("read-blk", func, addr, (int)len, ret, buf);
        if (ret) return ret;

        count -= (int)len;
        buf += len;
    }

    if (count > 0) {
        ret = sdio_io_rw_extended(dev, false, func->num, addr, false,
                                  buf, (unsigned int)count, 0);
        aic_sdio_log_cmd53("read-byte", func, addr, count, ret, buf);
        if (ret) return ret;
    }

    return 0;
}

int sdio_writesb(struct sdio_func *func, unsigned int addr, void *src, int count)
{
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    uint8_t *buf = src;
    int ret;

    if (!dev) return -ENODEV;
    if (count <= 0) return 0;

    if (sdio_block_size > 0 && count >= (int)sdio_block_size) {
        unsigned int nblocks = (unsigned int)count / sdio_block_size;
        unsigned int len = nblocks * sdio_block_size;

        ret = sdio_io_rw_extended(dev, true, func->num, addr, false,
                                  buf, sdio_block_size, nblocks);
        aic_sdio_log_cmd53("write-blk", func, addr, (int)len, ret, buf);
        if (ret) return ret;

        count -= (int)len;
        buf += len;
    }

    if (count > 0) {
        ret = sdio_io_rw_extended(dev, true, func->num, addr, false,
                                  buf, (unsigned int)count, 0);
        aic_sdio_log_cmd53("write-byte", func, addr, count, ret, buf);
        if (ret) return ret;
    }

    return 0;
}

static uint8_t sdio_f0_readb(unsigned int addr, int *err_ret)
{
    uint8_t val = 0;
    struct sdio_dev_s *dev = get_nuttx_dev(sdio_function[FUNC_0]);
    if (dev) {
        int ret = sdio_io_rw_direct(dev, false, 0, addr, 0, &val);
        if (err_ret) *err_ret = ret;
    } else {
        if (err_ret) *err_ret = -ENODEV;
    }
    return val;
}

static void sdio_f0_writeb(unsigned char b, unsigned int addr, int *err_ret)
{
    struct sdio_dev_s *dev = get_nuttx_dev(sdio_function[FUNC_0]);
    if (dev) {
        int ret = sdio_io_rw_direct(dev, true, 0, addr, b, NULL);
        if (err_ret) *err_ret = ret;
    } else {
        if (err_ret) *err_ret = -ENODEV;
    }
}

#if 0
bool sdio_readb_cmd52(uint32_t addr, uint8_t *data)
{
    int err;

	SDIO_PRE_OP_B();
    sdio_claim_host(sdio_function[FUNC_1]);

    *data = sdio_readb(sdio_function[FUNC_1], addr, &err);
	SDIO_WR_FAIL(err);

    if(err) {
        AIC_LOG_PRINTF("sdio_readb_cmd52 fail %d!\n", err);
        return FALSE;
    }

    sdio_release_host(sdio_function[FUNC_1]);
    return TRUE;
}

bool sdio_readb_cmd52_func2(uint32_t addr, uint8_t *data)
{
    int err;
	SDIO_PRE_OP_B();
    sdio_claim_host(sdio_function[FUNC_2]);
    *data = sdio_readb(sdio_function[FUNC_2], addr, &err);
	SDIO_WR_FAIL(err);
    if(err) {
        AIC_LOG_PRINTF("sdio_readb_cmd52_func2 fail %d!\n", err);
        return FALSE;
    }

    sdio_release_host(sdio_function[FUNC_2]);
    return TRUE;
}

bool sdio_writeb_cmd52(uint32_t addr, uint8_t data)
{
    int err;

	SDIO_PRE_OP_B();
    sdio_claim_host(sdio_function[FUNC_1]);
    sdio_writeb(sdio_function[FUNC_1], data, addr, &err);
	SDIO_WR_FAIL(err);
    if(err) {
        AIC_LOG_PRINTF("sdio_writeb_cmd52 fail %d!\n", err);
        return FALSE;
    }

    sdio_release_host(sdio_function[FUNC_1]);
    return TRUE;
}

static bool sdio_writeb_cmd52_func2(uint32_t addr, uint8_t data)
{
    int err;

	SDIO_PRE_OP_B();
    sdio_claim_host(sdio_function[FUNC_2]);
    sdio_writeb(sdio_function[FUNC_2], data, addr, &err);
	SDIO_WR_FAIL(err);
    if(err) {
        AIC_LOG_PRINTF("sdio_writeb_cmd52_func2 fail %d!\n", err);
        return FALSE;
    }

    sdio_release_host(sdio_function[FUNC_2]);
    return TRUE;
}
#endif

#ifdef CONFIG_OOB
static void sdio_oobirq_task(void *argv)
{
    int ret = 0;
    uint32_t wait_time = 1000;
    while(1) {
        ret = rtos_semaphore_wait(sdio_dev.sdio_oobirq_sema, wait_time);
        if (ret)
            AIC_LOG_PRINTF("sdio_oobirq_sema timeout: %d\n", ret);
        aic_sdio_rx_task(0);
    }
}

static void aicwf_sdio_oob_irq_hdl(void)
{
    int count = 0;
    count = rtos_semaphore_get_count(sdio_dev.sdio_oobirq_sema);
    if(count == 0)
        rtos_semaphore_signal(sdio_dev.sdio_oobirq_sema, true);
    else
         AIC_LOG_PRINTF("sdio oob isr: %d\n",count);
}

void aicwf_sdio_oob_enable(void)
{
    int ret = 0;
    GPIOConfiguration config;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    uint32_t oob_gpio_num = sdiodev->sdio_gpio_num;

    config.pinDir = GPIO_IN_PIN;
    #if 1
    config.pinEd = GPIO_RISE_EDGE;
    config.pinPull = GPIO_PULLDN_ENABLE;
    #else
    config.pinEd = GPIO_FALL_EDGE;
    config.pinPull = GPIO_PULLUP_ENABLE;
    #endif

    config.isr = aicwf_sdio_oob_irq_hdl;
    ret = GpioInitConfiguration(oob_gpio_num, config);
    AIC_LOG_PRINTF("sdio-oob: gpio=%d, ret=%d, LVL=%d", oob_gpio_num, ret, GpioGetLevel(oob_gpio_num));

    AIC_LOG_PRINTF("%s: close host sdio-irq", __func__);
    sdio_set_irq_handler(NULL);

    //disable sdio interrupt
    AIC_LOG_PRINTF("%s: SDIOWIFI_INTR_CONFIG_REG Disable\n", __func__);
    ret = aicwf_sdio_writeb_func2(sdiodev, sdiodev->sdio_reg.intr_config_reg, 0x0);
    if (ret == false) {
        AIC_LOG_PRINTF("ERR: reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
    }

    ret = rtos_task_create(sdio_oobirq_task, "oobirq_task", SDIO_OOBIRQ_TASK,
                           sdio_oobirq_stack_size, NULL, sdio_oobirq_priority, &sdiodev->sdio_oobirq_hdl);
    if (ret) {
        AIC_LOG_PRINTF("ERR: oobirq_task create failed\n");
    } else {
        sdiodev->oob_enable = true;
    }
}
#endif /* CONFIG_OOB */

/* NuttX SDIO IRQ callback - called from the SDIO host driver's work queue */
static void nuttx_sdio_irq_cb(void *arg)
{
    int func_num = (int)(uintptr_t)arg;
    if (func_num < SDIOM_MAX_FUNCS) {
        struct sdio_func *func = sdio_function[func_num];
        if (func && func->irq_handler) {
            func->irq_handler(func);
        }
    }
}

int aic_sdio_claim_irq(struct sdio_func *func, aicwf_sdio_irq_handler_t handler)
{
    int ret = 0;
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    uint8_t inten = 0;
    int read_ret = 0;

    if (func->irq_handler == NULL) {
        func->irq_handler = handler;
    }

    if (dev) {
        ret = aic_sdio_register_sdio_irq(dev, func->num,
                                         nuttx_sdio_irq_cb,
                                         (void *)(uintptr_t)func->num);
        if (ret == 0) {
            ret = sdio_enable_interrupt(dev, FUNC_0);
        }
        if (ret == 0) {
            ret = sdio_enable_interrupt(dev, func->num);
        }
        read_ret = aic_sdio_read_f0(dev, AIC_SDIO_CCCR_INTEN, &inten);
        AIC_LOG_PRINTF("sdio claim irq func%d ret=%d cccr_inten=0x%02x read=%d\n",
                       func->num, ret, inten, read_ret);
    }
    return ret;
}

int aic_sdio_release_irq(struct sdio_func *func)
{
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    func->irq_handler = NULL;
    if (dev) {
        aic_sdio_unregister_sdio_irq(dev, func->num);
    }
    return 0;
}

void sdio_set_irq_handler(void (*cb)(void))
{
	sdio_irq_cb = cb;
}

bool sdio_host_enable_isr(bool enable)
{
    return TRUE;
}

static uint32_t sdio_get_block_size(void)
{
    return sdio_block_size;
}

int aic_sdio_set_block_size(struct sdio_func *func, unsigned int blk_sz)
{
    struct sdio_dev_s *dev = get_nuttx_dev(func);
    if (dev) {
        int ret = sdio_set_blocksize(dev, func->num, (uint16_t)blk_sz);
        if (ret == 0) {
            sdio_block_size = blk_sz;
        }
        return ret;
    }
    return -ENODEV;
}

void sdio_release_func2(void)
{
    int ret = 0;
    AIC_LOG_PRINTF("%s\n", __func__);
    sdio_claim_host(sdio_function[FUNC_2]);
    ret = aicwf_sdio_writeb_func2(&sdio_dev, sdio_dev.sdio_reg.intr_config_reg, 0x0);
    if (ret < 0) {
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdio_dev.sdio_reg.intr_config_reg);
    }
    sdio_release_host(sdio_function[FUNC_2]);
}

static void aicwf_sdio_irq_hdl(void *arg)
{
    #ifdef CONFIG_OOB
    if (sdio_dev.oob_enable == false)
    #endif
    {
        #if !CONFIG_RXTASK_INSDIO
        int count;
        count = 0;
        if(count == 0)
            rtos_semaphore_signal(sdio_dev.sdio_rx_sema, true);
        else
            AIC_LOG_PRINTF("sdio isr->> %d\n",count);
        #else
        aic_sdio_rx_task(0);
        #endif
    }
}

int sdio_interrupt_init(struct aic_sdio_dev *sdiodev)
{
    int ret;
    int read_ret;
    uint8_t host_inten = 0;
    uint8_t card_inten = 0;

    AIC_LOG_PRINTF("%s, chipid=%d\n", __func__, sdiodev->chipid);
    // func1
    sdio_claim_host(sdio_function[FUNC_1]);
    ret = aic_sdio_claim_irq(sdio_function[FUNC_1], aicwf_sdio_irq_hdl);
    if (ret) {
        sdio_release_host(sdio_function[FUNC_1]);
        AIC_LOG_PRINTF("claim func1 irq failed: %d\n", ret);
        return ret;
    }

    //enable sdio interrupt
    sdio_writeb(sdio_function[FUNC_1], 0x7, sdiodev->sdio_reg.intr_config_reg, &ret);
    card_inten = sdio_readb(sdio_function[FUNC_1], sdiodev->sdio_reg.intr_config_reg, &read_ret);
    aic_sdio_read_f0(get_nuttx_dev(sdio_function[FUNC_1]), AIC_SDIO_CCCR_INTEN, &host_inten);
    sdio_release_host(sdio_function[FUNC_1]);
    if (ret) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
        return ret;
    }
    AIC_LOG_PRINTF("sdio func1 irq enabled: host=0x%02x card=0x%02x read=%d\n",
                   host_inten, card_inten, read_ret);

    if ((sdiodev->chipid == PRODUCT_ID_AIC8800DC) || (sdiodev->chipid == PRODUCT_ID_AIC8800DW)) {
        // func2
        sdio_claim_host(sdio_function[FUNC_2]);
        ret = aic_sdio_claim_irq(sdio_function[FUNC_2], aicwf_sdio_irq_hdl);
        if (ret) {
            sdio_release_host(sdio_function[FUNC_2]);
            AIC_LOG_PRINTF("claim func2 irq failed: %d\n", ret);
            return ret;
        }

        //enable sdio interrupt
        sdio_writeb(sdio_function[FUNC_2], 0x7, sdiodev->sdio_reg.intr_config_reg, &ret);
        card_inten = sdio_readb(sdio_function[FUNC_2], sdiodev->sdio_reg.intr_config_reg, &read_ret);
        aic_sdio_read_f0(get_nuttx_dev(sdio_function[FUNC_2]), AIC_SDIO_CCCR_INTEN, &host_inten);
        sdio_release_host(sdio_function[FUNC_2]);
        if (ret) {
            ret = EREMOTEIO;
            AIC_LOG_PRINTF("func2 reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
            return ret;
        }
        AIC_LOG_PRINTF("sdio func2 irq enabled: host=0x%02x card=0x%02x read=%d\n",
                       host_inten, card_inten, read_ret);
    }
    msgcfm_poll_en = 0;
    return 0;
}

int sdio_interrupt_deinit(struct aic_sdio_dev *sdiodev)
{
    int ret;
    AIC_LOG_PRINTF("%s, chipid=%d\n", __func__, sdiodev->chipid);
    // func1
    sdio_claim_host(sdio_function[FUNC_1]);
    aic_sdio_release_irq(sdio_function[FUNC_1]);
    //disable sdio interrupt
    sdio_writeb(sdio_function[FUNC_1], 0x0, sdiodev->sdio_reg.intr_config_reg, &ret);
    sdio_release_host(sdio_function[FUNC_1]);
    if (ret) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
        return ret;
    }
    if ((sdiodev->chipid == PRODUCT_ID_AIC8800DC) || (sdiodev->chipid == PRODUCT_ID_AIC8800DW)) {
        // func2
        sdio_claim_host(sdio_function[FUNC_2]);
        aic_sdio_release_irq(sdio_function[FUNC_2]);
        //disable sdio interrupt
        sdio_writeb(sdio_function[FUNC_2], 0x0, sdiodev->sdio_reg.intr_config_reg, &ret);
        sdio_release_host(sdio_function[FUNC_2]);
        if (ret) {
            ret = EREMOTEIO;
            AIC_LOG_PRINTF("func2 reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
            return ret;
        }
    }
    msgcfm_poll_en = 1;
    return 0;
}

int aicwf_sdio_func_init(uint16_t chipid, struct aic_sdio_dev *sdiodev)
{
    int32_t ret = 0;
    uint8_t block_bit0 = 0x1;
    uint8_t byte_mode_disable = 0x1;//1: no byte mode

    AIC_LOG_PRINTF("%s: chipid=%d\n", __func__, chipid);
    /* SDIO Function 1 */
    sdio_claim_host(sdio_function[FUNC_1]);

    ret = aic_sdio_set_block_size(sdio_function[FUNC_1], SDIOWIFI_FUNC_BLOCKSIZE);
    if (ret) {
        AIC_LOG_PRINTF("func1 blksize set failed, ret=%d\n", ret);
    }
    AIC_LOG_PRINTF("sdio_host_init:sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
    if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("sdio_host_init: blksize set failed\n");
        return ret;
    }

    ret = aic_sdio_enable_func(sdio_function[FUNC_1]);
    if (ret) {
        AIC_LOG_PRINTF("sdio func1 enable failed, ret=%d\n", ret);
        return ret;
    }

    sdio_writeb(sdio_function[FUNC_1], block_bit0, sdiodev->sdio_reg.register_block, &ret);
    if (ret < 0) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
        return ret;
    }

    //1: no byte mode
    sdio_writeb(sdio_function[FUNC_1], byte_mode_disable, sdiodev->sdio_reg.bytemode_enable_reg, &ret);
    if (ret < 0) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
        return ret;
    }

    #if 0
    //enable sdio interrupt
    sdio_writeb(sdio_function[FUNC_1], 0x7, sdiodev->sdio_reg.intr_config_reg, &ret);
    if (ret < 0) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
        return ret;
    }
    #endif

    sdio_release_host(sdio_function[1]);
    AIC_LOG_PRINTF("sdio_host_init:enable fun1 ok!\n");


    if (chipid == PRODUCT_ID_AIC8800DC || chipid == PRODUCT_ID_AIC8800DW) {
        /* SDIO Function 2 */
        sdio_claim_host(sdio_function[FUNC_2]);

        ret = aic_sdio_set_block_size(sdio_function[FUNC_2], SDIOWIFI_FUNC_BLOCKSIZE);
        if (ret) {
            AIC_LOG_PRINTF("func2 blksize set failed, ret=%d\n", ret);
        }
        AIC_LOG_PRINTF("sdio_host_init:func2:sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
        if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
            AIC_LOG_PRINTF("sdio_host_init:func2: blksize set failed\n");
        }

        ret = aic_sdio_enable_func(sdio_function[FUNC_2]);
        if (ret) {
            AIC_LOG_PRINTF("sdio func2 enable failed, ret=%d\n", ret);
            return ret;
        }

        sdio_writeb(sdio_function[FUNC_2], block_bit0, sdiodev->sdio_reg.register_block, &ret);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
            return ret;
        }

        //1: no byte mode
        sdio_writeb(sdio_function[FUNC_2], byte_mode_disable, sdiodev->sdio_reg.bytemode_enable_reg, &ret);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
            return ret;
        }

        #if 0
        //enable sdio interrupt
        sdio_writeb(sdio_function[FUNC_2], 0x7, sdiodev->sdio_reg.intr_config_reg, &ret);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
            return ret;
        }
        #endif

        sdio_release_host(sdio_function[FUNC_2]);
        AIC_LOG_PRINTF("sdio_host_init:enable fun2 ok!\n");
    }

    return 0;
}

int aicwf_sdiov3_func_init(uint16_t chipid, struct aic_sdio_dev *sdiodev)
{
    int32_t ret = 0;
    uint8_t byte_mode_disable = 0x1;//1: no byte mode

    AIC_LOG_PRINTF("%s: chipid=%d\n", __func__, chipid);

    /* SDIO Function 1 */
    sdio_claim_host(sdio_function[FUNC_1]);

    ret = aic_sdio_set_block_size(sdio_function[FUNC_1], SDIOWIFI_FUNC_BLOCKSIZE);
    if (ret) {
        AIC_LOG_PRINTF("func1 blksize set failed, ret=%d\n", ret);
    }
    AIC_LOG_PRINTF("sdio_host_init:sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
    if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
        ret = EREMOTEIO;
        AIC_LOG_PRINTF("sdio_host_init: blksize set failed\n");
        return ret;
    }

    ret = aic_sdio_enable_func(sdio_function[FUNC_1]);
    if (ret) AIC_LOG_PRINTF("sdio func1 enable failed, ret=%d\n", ret);

    sdio_f0_writeb(0x7F, 0xF2, &ret);
    if (ret) {
        AIC_LOG_PRINTF("set fn0 0xF2 fail %d\n", ret);
        return ret;
    }

    aic_sdio_set_clock(0);

    //1: no byte mode
    sdio_writeb(sdio_function[FUNC_1], byte_mode_disable, sdiodev->sdio_reg.bytemode_enable_reg, &ret);
    if (ret < 0) {
        AIC_LOG_PRINTF("reg:%d write failed! ret=%d\n", sdiodev->sdio_reg.bytemode_enable_reg, ret);
        return ret;
    }

    sdio_release_host(sdio_function[FUNC_1]);
    AIC_LOG_PRINTF("sdio_host_init:enable fun1 ok!\n");

    return 0;
}

/**
 * aicwf_sdio_probe - called when the platform SDIO controller enumerates the
 *                    AIC8800 functions.
 * @func: pointer to an array of 3 sdio_func structures (func0, func1, func2)
 *        whose drv_priv fields have been set to the NuttX struct sdio_dev_s *.
 */
int aicwf_sdio_probe(struct sdio_func *func)
{
    int ret = 0;
#if CONFIG_SDIO_HOST_MUTEX
    ret = rtos_mutex_create(&sdio_host_mutex, "sdio_host_mutex");
    if (ret) {
        AIC_LOG_PRINTF("Alloc sdio_host_mutex failed, ret=%d\n", ret);
        return ret;
    }
#endif
    /* Store the NuttX sdio_dev_s pointer from the func's drv_priv */
    g_nuttx_sdio_dev = (struct sdio_dev_s *)func[0].drv_priv;

    sdio_function[FUNC_0] = &func[0];
    sdio_function[FUNC_1] = &func[1];
    sdio_function[FUNC_2] = &func[2];
    aic_sdio_dump_card_info(func);
#if 1
    ret = aic_wifi_init(CONFIG_WIFIMODE_SELECT, CONFIG_CHIPID_SELECT, NULL);
#else
    ret = wifi_driver_init();
#endif
    return ret;
}

int aicwf_sdio_remove(struct sdio_func *func)
{
    int ret = 0;
    aic_wifi_deinit(WIFI_MODE_UNKNOWN);
#if CONFIG_SDIO_HOST_MUTEX
    if (sdio_host_mutex) {
        rtos_mutex_delete(sdio_host_mutex);
        sdio_host_mutex = NULL;
    }
#endif
    g_nuttx_sdio_dev = NULL;
    return ret;
}

#if 0
/* Registration stubs - in NuttX the platform init code calls
 * sdio_initialize() + mmcsd_slotinitialize() or aicwf_sdio_probe()
 * directly from board-level init. */
int aicw_sdio_register_init(void)
{
    return 0;
}

int aicw_sdio_register_deinit(void)
{
    return 0;
}
#endif
