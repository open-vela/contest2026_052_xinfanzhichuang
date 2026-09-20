/*
 * WiFi SDIO Interface Driver for NuttX/Vela
 *
 * Replaces RT-Thread sdio_register_driver framework with NuttX
 * native SDIO initialization.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sdio_func.h"
#include "sdio_port.h"
#include "aic_plat_log.h"
#include "rtos_port.h"
#include "plat_port.h"
#include "wifi_port.h"

#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <nuttx/mutex.h>
#include "../../drv/sdmc/aic_sdio.h"

/* Global NuttX SDIO device and bus lock */
struct sdio_dev_s *g_nuttx_sdiodev = NULL;
mutex_t g_sdio_bus_lock = NXMUTEX_INITIALIZER;

struct sdio_func g_wifi_if_sdio_funcs[SDIOM_MAX_FUNCS];

static mutex_t g_wifi_state_lock = NXMUTEX_INITIALIZER;
static bool g_wifi_sdio_initialized;
static bool g_wifi_netif_initialized;

#ifdef CONFIG_BT_SUPPORT
int g_aic8800_bt_patch_status = -EAGAIN;
int g_aic8800_bt_patch_requested;
static bool g_aic8800_bt_active;

struct rwnx_hw;
extern struct rwnx_hw *g_rwnx_hw;
extern int aicbt_init(struct rwnx_hw *rwnx_hw);
#endif

int wifi_if_sdio_deinit(void);

#ifdef CONFIG_BT_SUPPORT
static bool aic8800_bt_patch_auto_request_begin(void)
{
    if (g_aic8800_bt_patch_requested) {
        return false;
    }

    platform_rst_bt_pin_init();
    platform_rst_bt_pin_enable();
    rtos_msleep(100);

    g_aic8800_bt_patch_requested = 1;
    AIC_LOG_PRINTF("AIC8800 BT patch auto-requested for combo init\n");
    return true;
}

static void aic8800_bt_patch_auto_request_end(bool auto_requested)
{
    if (auto_requested) {
        g_aic8800_bt_patch_requested = 0;
    }
}
#endif

static void wifi_power_off_sequence(void)
{
    if (!platform_wifi_power_reset_shared()) {
        platform_rst_wifi_pin_init();
        platform_rst_wifi_pin_disable();
    }

    platform_pwr_wifi_pin_init();
    platform_pwr_wifi_pin_disable();
}

static void wifi_power_on_sequence(void)
{
    const int shared_power_reset = platform_wifi_power_reset_shared();

    platform_pwr_wifi_pin_init();
    if (!shared_power_reset)
        platform_rst_wifi_pin_init();

    AIC_LOG_PRINTF("WiFi power sequence: power/reset %s\n",
                   shared_power_reset ? "shared" : "separate");

    if (!shared_power_reset)
        platform_rst_wifi_pin_disable();

    platform_pwr_wifi_pin_disable();
    rtos_msleep(100);
    platform_pwr_wifi_pin_enable();
    rtos_msleep(300);

    if (!shared_power_reset) {
        platform_rst_wifi_pin_enable();
        rtos_msleep(200);
    } else {
        rtos_msleep(300);
    }
}

int aic8800_reset(void)
{
    wifi_power_off_sequence();
    rtos_msleep(100);
    wifi_power_on_sequence();
    return 0;
}

int aic8800_power_on(void)
{
    wifi_power_on_sequence();
    return 0;
}

int aic8800_power_off(void)
{
    wifi_power_off_sequence();
    return 0;
}

/*
 * wifi_if_sdio_init - Initialize WiFi SDIO interface
 *
 * For NuttX: initialize the SDIO host controller, then call
 * the AIC8800 SDIO probe function directly.
 */
int wifi_if_sdio_init(void)
{
    int idx;
    int ret;
#ifdef CONFIG_BT_SUPPORT
    bool bt_patch_auto_requested = false;
#endif

    nxmutex_lock(&g_wifi_state_lock);

    if (g_wifi_sdio_initialized) {
        nxmutex_unlock(&g_wifi_state_lock);
        AIC_LOG_PRINTF("%s: WiFi SDIO already initialized\n", __func__);
        return 0;
    }

    AIC_LOG_PRINTF("%s: initializing WiFi SDIO interface\n", __func__);

#ifdef CONFIG_BT_SUPPORT
    g_aic8800_bt_patch_status = -EAGAIN;
    bt_patch_auto_requested = aic8800_bt_patch_auto_request_begin();
#endif

    aic8800_power_on();

    /* Initialize SDIO host controller (slot 0 = SDC0) */
    g_nuttx_sdiodev = sdio_initialize(0);
    if (!g_nuttx_sdiodev) {
        AIC_LOG_PRINTF("%s: sdio_initialize(0) failed\n", __func__);
        aic8800_power_off();
#ifdef CONFIG_BT_SUPPORT
        aic8800_bt_patch_auto_request_end(bt_patch_auto_requested);
#endif
        nxmutex_unlock(&g_wifi_state_lock);
        return -1;
    }

    SDIO_ATTACH(g_nuttx_sdiodev);
    SDIO_RESET(g_nuttx_sdiodev);
    SDIO_CLOCK(g_nuttx_sdiodev, CLOCK_IDMODE);

    ret = sdio_probe(g_nuttx_sdiodev);
    if (ret < 0) {
        AIC_LOG_PRINTF("%s: sdio_probe failed: %d\n", __func__, ret);
        g_nuttx_sdiodev = NULL;
        aic8800_power_off();
#ifdef CONFIG_BT_SUPPORT
        aic8800_bt_patch_auto_request_end(bt_patch_auto_requested);
#endif
        nxmutex_unlock(&g_wifi_state_lock);
        return ret;
    }

    /* Initialize sdio_func structures */
    for (idx = 0; idx < SDIOM_MAX_FUNCS; idx++) {
        memset(&g_wifi_if_sdio_funcs[idx], 0, sizeof(struct sdio_func));
        g_wifi_if_sdio_funcs[idx].num = idx;
        g_wifi_if_sdio_funcs[idx].drv_priv = (void *)g_nuttx_sdiodev;
    }

    /* Call the AIC8800 SDIO probe */
    ret = aicwf_sdio_probe(&g_wifi_if_sdio_funcs[0]);
    if (ret < 0) {
        AIC_LOG_PRINTF("%s: aicwf_sdio_probe failed: %d\n", __func__, ret);
        memset(g_wifi_if_sdio_funcs, 0, sizeof(g_wifi_if_sdio_funcs));
        g_nuttx_sdiodev = NULL;
        aic8800_power_off();
#ifdef CONFIG_BT_SUPPORT
        g_aic8800_bt_patch_status = -EAGAIN;
        aic8800_bt_patch_auto_request_end(bt_patch_auto_requested);
#endif
        nxmutex_unlock(&g_wifi_state_lock);
        return ret;
    }

    g_wifi_sdio_initialized = true;
    AIC_LOG_PRINTF("%s: WiFi SDIO init done\n", __func__);
#ifdef CONFIG_BT_SUPPORT
    aic8800_bt_patch_auto_request_end(bt_patch_auto_requested);
#endif
    nxmutex_unlock(&g_wifi_state_lock);
    return 0;
}

int aic8800_bt_patch_prepare(void)
{
#ifdef CONFIG_BT_SUPPORT
    int ret;

    if (g_aic8800_bt_patch_status == 0) {
        g_aic8800_bt_active = true;
        AIC_LOG_PRINTF("AIC8800 BT patch already prepared\n");
        return 0;
    }

    AIC_LOG_PRINTF("AIC8800 BT patch prepare: assert BT_ON and init SDIO\n");
    platform_rst_bt_pin_init();
    platform_rst_bt_pin_enable();
    rtos_msleep(100);

    g_aic8800_bt_patch_requested = 1;
    ret = wifi_if_sdio_init();
    if (ret < 0) {
        g_aic8800_bt_patch_requested = 0;
        AIC_LOG_PRINTF("AIC8800 BT patch prepare failed: %d\n", ret);
        return ret;
    }

    if (g_aic8800_bt_patch_status != 0 && g_rwnx_hw != NULL) {
        AIC_LOG_PRINTF("AIC8800 BT patch prepare: load patch on active SDIO\n");
        ret = aicbt_init(g_rwnx_hw);
        g_aic8800_bt_patch_status = ret;
        AIC_LOG_PRINTF("AIC8800 BT patch late init %s: %d\n",
                       ret == 0 ? "done" : "failed", ret);
    }

    g_aic8800_bt_patch_requested = 0;

    AIC_LOG_PRINTF("AIC8800 BT patch prepare done\n");
    if (g_aic8800_bt_patch_status != 0) {
        AIC_LOG_PRINTF("AIC8800 BT patch not ready: %d\n",
                       g_aic8800_bt_patch_status);
        return g_aic8800_bt_patch_status < 0 ?
               g_aic8800_bt_patch_status : -EIO;
    }

    g_aic8800_bt_active = true;
    return 0;
#else
    AIC_LOG_PRINTF("AIC8800 BT patch support disabled\n");
    return -ENOTSUP;
#endif
}

void aic8800_bt_patch_release(void)
{
#ifdef CONFIG_BT_SUPPORT
    g_aic8800_bt_active = false;
    g_aic8800_bt_patch_requested = 0;

    if (!g_wifi_netif_initialized) {
        AIC_LOG_PRINTF("AIC8800 BT patch release: WiFi stopped, deinit SDIO\n");
        wifi_if_sdio_deinit();
    } else {
        AIC_LOG_PRINTF("AIC8800 BT patch release: keep SDIO for WiFi\n");
    }
#endif
}

int wifi_if_sdio_deinit(void)
{
    int ret = 0;

    nxmutex_lock(&g_wifi_state_lock);

    if (!g_wifi_sdio_initialized) {
        nxmutex_unlock(&g_wifi_state_lock);
        AIC_LOG_PRINTF("%s: WiFi SDIO already deinitialized\n", __func__);
        return 0;
    }

    AIC_LOG_PRINTF("%s: deinitializing WiFi SDIO\n", __func__);
#ifdef CONFIG_BT_SUPPORT
    if (g_aic8800_bt_active) {
        AIC_LOG_PRINTF("%s: skip SDIO deinit, BT is active\n", __func__);
        nxmutex_unlock(&g_wifi_state_lock);
        return 0;
    }
#endif

    ret = aicwf_sdio_remove(&g_wifi_if_sdio_funcs[0]);
    if (ret < 0) {
        AIC_LOG_PRINTF("%s: aicwf_sdio_remove failed: %d\n", __func__, ret);
    }

    memset(g_wifi_if_sdio_funcs, 0, sizeof(g_wifi_if_sdio_funcs));
    g_nuttx_sdiodev = NULL;
    g_wifi_sdio_initialized = false;
#ifdef CONFIG_BT_SUPPORT
    g_aic8800_bt_patch_status = -EAGAIN;
#endif
    aic8800_power_off();

    nxmutex_unlock(&g_wifi_state_lock);
    return ret;
}

/*
 * riscv_netinitialize - NuttX network device initialization hook
 *
 * Called by riscv_initialize.c during boot when CONFIG_NET is enabled.
 * Initializes the AIC8800 WiFi SDIO interface and registers wlan0.
 */

/*
 * riscv_netinitialize - NuttX network device initialization hook
 *
 * WiFi SDIO init is deferred to wifistart command to avoid
 * crashing the kernel scheduler during early boot.
 */
extern int wifi_netdev_init(void);
extern int wifi_netdev_deinit(void);
extern int net_init(void);

void riscv_netinitialize(void)
{
    /* WiFi initialization is deferred to wifistart NSH command.
     * Do NOT call wifi_if_sdio_init() or wifi_netdev_init() here
     * because the kernel scheduler is not fully initialized yet
     * at this point, and aicwf_sdio_probe() from the .a library
     * corrupts the ready-to-run task list.
     */
}

/*
 * wifi_start - Initialize WiFi (called from wifistart NSH command)
 */
int wifi_start(void)
{
    int ret;

    AIC_LOG_PRINTF("WiFi starting...\n");

    if (g_wifi_netif_initialized) {
        AIC_LOG_PRINTF("WiFi already started\n");
        return 0;
    }

    ret = wifi_if_sdio_init();
    if (ret < 0)
    {
        AIC_LOG_PRINTF("wifi_if_sdio_init failed: %d\n", ret);
        return ret;
    }

    ret = net_init();
    if (ret < 0)
    {
        AIC_LOG_PRINTF("net_init failed: %d\n", ret);
        wifi_if_sdio_deinit();
        return ret;
    }

    ret = wifi_netdev_init();
    if (ret < 0)
    {
        AIC_LOG_PRINTF("wifi_netdev_init failed: %d\n", ret);
        wifi_if_sdio_deinit();
        g_wifi_netif_initialized = false;
        return ret;
    }

    g_wifi_netif_initialized = true;
    AIC_LOG_PRINTF("AIC8800 netif initialized\n");
    AIC_LOG_PRINTF("WiFi started successfully\n");
    return 0;
}

int wifi_stop(void)
{
    int ret;

    AIC_LOG_PRINTF("WiFi stop\n");

    if (!g_wifi_netif_initialized) {
        AIC_LOG_PRINTF("WiFi already stopped\n");
        return 0;
    }

    ret = wifi_netdev_deinit();
    if (ret < 0) {
        AIC_LOG_PRINTF("wifi_netdev_deinit failed: %d\n", ret);
    }

    ret = wifi_if_sdio_deinit();
    if (ret < 0) {
        AIC_LOG_PRINTF("wifi_if_sdio_deinit failed: %d\n", ret);
    }

    g_wifi_netif_initialized = false;
    AIC_LOG_PRINTF("AIC8800 netif deinitialized\n");

    return ret;
}
