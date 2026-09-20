/*
 * Copyright (C) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: huahui.mai@artinchip.com
 */

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/semaphore.h>
#include <nuttx/video/fb.h>

#include <aic_core.h>
#include <aic_hal.h>
#include "drv_fb.h"
#include "drv_fb_helper.h"

#include <nuttx/lcd/lcd.h>

#undef pr_debug
#ifdef AIC_LCD_DRV_DEBUG
#define pr_debug pr_info
#else
#define pr_debug(fmt, ...)
#endif

/* LCD device structure - NuttX compatible */
struct aic_lcd_dev_s {
  struct lcd_dev_s dev;
  struct aicfb_info *fbi;
  sem_t sem;
};

static struct aic_lcd_dev_s g_lcddev;

static int aic_lcd_getvideoinfo(FAR struct lcd_dev_s *dev,
                                FAR struct fb_videoinfo_s *vinfo) {
  struct aic_lcd_dev_s *priv = (struct aic_lcd_dev_s *)dev;

  if (!priv || !vinfo || !priv->fbi)
    return -EINVAL;

  vinfo->xres = priv->fbi->width;
  vinfo->yres = priv->fbi->height;
  vinfo->nplanes = 1;
  vinfo->fmt = FB_FMT_RGB32;

  return OK;
}

static int aic_lcd_getplaneinfo(FAR struct lcd_dev_s *dev, unsigned int planeno,
                                FAR struct lcd_planeinfo_s *pinfo) {
  struct aic_lcd_dev_s *priv = (struct aic_lcd_dev_s *)dev;

  if (!priv || !pinfo || planeno != 0)
    return -EINVAL;

  if (!priv->fbi)
    return -EINVAL;

  pinfo->putrun = NULL;
  pinfo->getrun = NULL;
  pinfo->buffer = NULL;
  pinfo->bpp = 32;
  pinfo->dev = dev;

  return OK;
}

static int aic_lcd_getpower(struct lcd_dev_s *dev) { return LCD_FULL_ON; }

static int aic_lcd_setpower(struct lcd_dev_s *dev, int power) {
  if (power > 0)
    aicfb_ioctl(AICFB_POWERON, NULL);
  else
    aicfb_ioctl(AICFB_POWEROFF, NULL);

  return OK;
}

static int aic_lcd_getcontrast(struct lcd_dev_s *dev) { return 0; }

static int aic_lcd_setcontrast(struct lcd_dev_s *dev, unsigned int contrast) {
  return -ENOSYS;
}

static int aic_lcd_setframerate(struct lcd_dev_s *dev, int rate) {
  return -ENOSYS;
}

static int aic_lcd_getframerate(struct lcd_dev_s *dev) { return 60; }

static int aic_lcd_open(FAR struct lcd_dev_s *dev) {
  struct aic_lcd_dev_s *priv = (struct aic_lcd_dev_s *)dev;

  nxsem_wait(&priv->sem);
  pr_debug("LCD device opened\n");
  nxsem_post(&priv->sem);

  return OK;
}

static int aic_lcd_close(FAR struct lcd_dev_s *dev) {
  struct aic_lcd_dev_s *priv = (struct aic_lcd_dev_s *)dev;

  nxsem_wait(&priv->sem);
  pr_debug("LCD device closed\n");
  nxsem_post(&priv->sem);

  return OK;
}

static void aic_lcd_initdev(struct aic_lcd_dev_s *priv) {
  priv->fbi = aicfb_get_drvdata();
  if (!priv->fbi) {
    pr_err("Failed to get framebuffer info\n");
    return;
  }

  priv->dev.getvideoinfo = aic_lcd_getvideoinfo;
  priv->dev.getplaneinfo = aic_lcd_getplaneinfo;
  priv->dev.getpower = aic_lcd_getpower;
  priv->dev.setpower = aic_lcd_setpower;
  priv->dev.getcontrast = aic_lcd_getcontrast;
  priv->dev.setcontrast = aic_lcd_setcontrast;
  priv->dev.setframerate = aic_lcd_setframerate;
  priv->dev.getframerate = aic_lcd_getframerate;
  priv->dev.open = aic_lcd_open;
  priv->dev.close = aic_lcd_close;
  priv->dev.ioctl = NULL;

  nxsem_init(&priv->sem, 0, 1);
}

/* LCD ioctl commands */
#define LCDIOC_POWERON _LCDIOC(1)
#define LCDIOC_POWEROFF _LCDIOC(2)
#define LCDIOC_GETINFO _LCDIOC(3)
#define LCDIOC_SETBRIGHTNESS _LCDIOC(4)

struct lcd_info_s {
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t fmt;
};

static int lcd_open(FAR struct file *filep) {
  struct aic_lcd_dev_s *priv = &g_lcddev;

  nxsem_wait(&priv->sem);

  pr_debug("LCD device opened\n");

  nxsem_post(&priv->sem);
  return OK;
}

static int lcd_close(FAR struct file *filep) {
  struct aic_lcd_dev_s *priv = &g_lcddev;

  nxsem_wait(&priv->sem);

  pr_debug("LCD device closed\n");

  nxsem_post(&priv->sem);
  return OK;
}

static ssize_t lcd_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen) {
  return -ENOSYS;
}

static ssize_t lcd_write(FAR struct file *filep, FAR const char *buffer,
                         size_t buflen) {
  return -ENOSYS;
}

static int lcd_ioctl(FAR struct file *filep, int cmd, unsigned long arg) {
  struct aic_lcd_dev_s *priv = &g_lcddev;
  int ret = OK;

  nxsem_wait(&priv->sem);

  switch (cmd) {
  case LCDIOC_POWERON:
    pr_debug("LCD power on\n");
    aicfb_ioctl(AICFB_POWERON, NULL);
    break;

  case LCDIOC_POWEROFF:
    pr_debug("LCD power off\n");
    aicfb_ioctl(AICFB_POWEROFF, NULL);
    break;

  case LCDIOC_GETINFO: {
    struct lcd_info_s *info = (struct lcd_info_s *)arg;
    if (priv->fbi) {
      info->width = priv->fbi->width;
      info->height = priv->fbi->height;
      info->bpp = priv->fbi->bits_per_pixel;
      info->fmt = FB_FMT_RGB32;
    }
    break;
  }

  case LCDIOC_SETBRIGHTNESS:
    pr_debug("LCD set brightness: %lu\n", arg);
    break;

  default:
    ret = -ENOTTY;
    break;
  }

  nxsem_post(&priv->sem);
  return ret;
}

static const struct file_operations g_lcdops = {
    lcd_open,  /* open */
    lcd_close, /* close */
    lcd_read,  /* read */
    lcd_write, /* write */
    NULL,      /* seek */
    lcd_ioctl, /* ioctl */
    NULL       /* poll */
};

FAR struct lcd_dev_s *aic_lcd_getdev(void) { return &g_lcddev.dev; }

int lcd_register_device(void) {
  struct aic_lcd_dev_s *priv = &g_lcddev;
  int ret;

  aic_lcd_initdev(priv);

  if (!priv->fbi) {
    pr_err("Failed to initialize LCD device\n");
    return -EINVAL;
  }

  ret = register_driver("/dev/lcd0", &g_lcdops, 0666, priv);
  if (ret < 0) {
    pr_err("Failed to register LCD device: %d\n", ret);
    nxsem_destroy(&priv->sem);
    return ret;
  }

  return OK;
}

void lcd_unregister_device(void) {
  struct aic_lcd_dev_s *priv = &g_lcddev;

  unregister_driver("/dev/lcd0");
  nxsem_destroy(&priv->sem);
}
