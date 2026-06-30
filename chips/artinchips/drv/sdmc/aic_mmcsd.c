/****************************************************************************
 * boards/risc-v/artinchip/aic-d12x/src/aic_mmcsd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>

#include <nuttx/mmcsd.h>
#include <nuttx/sdio.h>
#include <nuttx/wqueue.h>

#include "aic_hal_gpio.h"
#include "aic_mmcsd.h"
#include <debug.h>
#include <aic_sdio.h>
#include <sys/mount.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef D12X_SDHC_AUTOMOUNT_BLKDEV
#define D12X_SDHC_AUTOMOUNT_BLKDEV "/dev/mmcsd1"
#endif
#define AIC_SDIO_HOTPLUG_POLL_MS 200

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sdio_dev_s *g_sdio[AIC_SDIO_NSLOTS] = {NULL, NULL};
static bool g_inserted[AIC_SDIO_NSLOTS];
static struct work_s g_hotplug_work[AIC_SDIO_NSLOTS];
static int g_slotno[AIC_SDIO_NSLOTS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

__attribute__((weak)) bool board_aic_sdcard_detect(int slotno) {

#ifdef CONFIG_AIC_CARD_DETECT_BY_GPIO
  static bool det_init = false;
  if (slotno == 1) {
    unsigned int value;
    int pin;

    pin = hal_gpio_name2pin(CONFIG_AIC_SDMC1_HOTPLUG_PIN);
    if (pin < 0) {
      mcerr("ERROR: invalid SDMC1 hotplug pin: %s\n",
            CONFIG_AIC_SDMC1_HOTPLUG_PIN);
      return false;
    }
    if (!det_init) {
      mcinfo("INFO: det_init: %s\n", CONFIG_AIC_SDMC1_HOTPLUG_PIN);
      hal_gpio_set_func(GPIO_GROUP(pin), GPIO_GROUP_PIN(pin), PIN_FUNC_GPIO);
      hal_gpio_direction_input(GPIO_GROUP(pin), GPIO_GROUP_PIN(pin));

      hal_gpio_set_bias_pull(GPIO_GROUP(pin), GPIO_GROUP_PIN(pin), 2);

      det_init = true;
    }
    hal_gpio_get_value(GPIO_GROUP(pin), GPIO_GROUP_PIN(pin), &value);
    return value;
  }
  /*
   * Override this weak symbol if a different slot uses a board-specific
   * card-detect GPIO.
   */
#endif
  UNUSED(slotno);
  return true;
}

void aic_mmcsd_mount(int slotno, bool is_mount) {
  if (slotno != 1) {
    mcerr("ERROR: Invalid slot number: %d\n", slotno);
    return;
  }

  if (is_mount) {
    int ret = mount(D12X_SDHC_AUTOMOUNT_BLKDEV, CONFIG_AUTO_MOUNT_PATH,
                    CONFIG_AUTO_MOUNT_FSTYPE, MS_NOSUID | MS_SYNCHRONOUS, NULL);
    if (ret >= 0) {
      mcinfo("micro_sd %d: mount success: %d\n", __LINE__, ret);
    } else {
      mcinfo("micro_sd %d: mount failed: %d\n", __LINE__, ret);
    }
  } else {
    int ret = umount2(CONFIG_AUTO_MOUNT_PATH, MNT_DETACH);
    if (ret == 0) {
      mcinfo("micro_sd %d: unmount success\n", __LINE__);
    } else {
      mcinfo("micro_sd %d: unmount failed: %d\n", __LINE__, ret);
    }
  }
}

static void aic_sdcard_hotplug_worker(void *arg) {
  int slotno = *(int *)arg;
  bool present = false;
  present = board_aic_sdcard_detect(slotno);
  if (present != g_inserted[slotno]) {
    if (present) {
      sdio_mediachange(g_sdio[slotno], present);
      int minor = slotno; // Assuming minor number is same as slot number
      int ret = mmcsd_slotinitialize(minor, g_sdio[slotno]);
      if (ret == OK) {
        aic_mmcsd_mount(slotno, true);
      }
      mcinfo("SD card %s\n", present ? "inserted" : "removed");
      g_inserted[slotno] = present;
    } else {
      aic_mmcsd_mount(slotno, false);
      sdio_mediachange(g_sdio[slotno], present);
      mcinfo("SD card %s\n", present ? "inserted" : "removed");
      g_inserted[slotno] = present;
    }
  }

  work_queue(HPWORK, &g_hotplug_work[slotno], aic_sdcard_hotplug_worker,
             &g_slotno[slotno], MSEC2TICK(AIC_SDIO_HOTPLUG_POLL_MS));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int aic_mmcsd_initialize(int slotno) {
  int ret;

  g_sdio[slotno] = sdio_initialize(slotno);
  if (g_sdio[slotno] == NULL) {
    mcerr("ERROR: sdio_initialize(%d) failed\n", slotno);
    return -ENODEV;
  }
  g_slotno[slotno] = slotno;
  g_inserted[slotno] = board_aic_sdcard_detect(slotno);

  mcinfo("INFO: slotno=%d, g_inserted(%d)\n", slotno, g_inserted[slotno]);
  if (g_inserted[slotno]) {
    sdio_mediachange(g_sdio[slotno], g_inserted[slotno]);
    ret = mmcsd_slotinitialize(slotno, g_sdio[slotno]);
    if (ret == OK) {
      aic_mmcsd_mount(slotno, true);
    }
  }
  work_queue(HPWORK, &g_hotplug_work[slotno], aic_sdcard_hotplug_worker,
             &g_slotno[slotno], MSEC2TICK(AIC_SDIO_HOTPLUG_POLL_MS));
  return OK;
}
