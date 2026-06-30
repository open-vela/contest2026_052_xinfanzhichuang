/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include <nuttx/config.h>
#include <nuttx/timers/pwm.h>

#include "aic_core.h"
#include "aic_hal_clk.h"
#include "hal_pwm.h"

struct aic_pwm_lowerhalf_s {
  struct pwm_lowerhalf_s dev;
  int pwm_id;
#ifdef CONFIG_PWM_MULTICHAN
  struct pwm_chan_s channels[CONFIG_PWM_NCHANNELS];
#else
  ub16_t duty;
  uint8_t cpol;
  uint8_t dcpol;
#endif
  uint32_t frequency;
  bool enabled;
  
};
static struct aic_pwm_lowerhalf_s g_aic_pwm[PWM_ID_MAX_NUM];

static inline struct aic_pwm_lowerhalf_s *to_aic_pwm(FAR struct pwm_lowerhalf_s *dev)
{
  return (struct aic_pwm_lowerhalf_s *)dev;
}



static int pwm_setup(struct pwm_lowerhalf_s *dev)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  struct aic_pwm_action action0 = {
    .CBD = PWM_ACT_NONE,
    .CBU = PWM_ACT_NONE,
    .CAD = PWM_ACT_NONE,
    .CAU = PWM_ACT_LOW,
    .PRD = PWM_ACT_NONE,
    .ZRO = PWM_ACT_HIGH
  };
  struct aic_pwm_action action1 = {
    .CBD = PWM_ACT_NONE,
    .CBU = PWM_ACT_LOW,
    .CAD = PWM_ACT_NONE,
    .CAU = PWM_ACT_NONE,
    .PRD = PWM_ACT_NONE,
    .ZRO = PWM_ACT_HIGH
  };
  hal_log_info("PWM%d setup\n", priv->pwm_id);

  if (priv->enabled) {
    hal_pwm_disable(priv->pwm_id);
    hal_pwm_ch_deinit(priv->pwm_id);
  }
  hal_pwm_ch_init(priv->pwm_id, PWM_MODE_UP_COUNT, 0, &action0, &action1);
  priv->enabled = false;
  return OK;
}



static int pwm_shutdown(struct pwm_lowerhalf_s *dev)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);

   if (!priv->enabled) {
    hal_log_warn("PWM%d is not running, skip stop\n", priv->pwm_id);
    return OK;
  }

  if (priv->enabled) {
    hal_pwm_disable(priv->pwm_id);
  }

  hal_pwm_ch_deinit(priv->pwm_id);
  priv->enabled = false;
  return OK;
}

#ifdef CONFIG_PWM_MULTICHAN
static int pwm_start_multichan(struct aic_pwm_lowerhalf_s *priv, const struct pwm_info_s *info)
{
  int ret;
  uint32_t period_ns;
  uint32_t duty_ns[CONFIG_PWM_NCHANNELS];
  int i;

  if (info->frequency == 0) {
    hal_log_err("PWM frequency cannot be zero\n");
    return -EINVAL;
  }

  if (priv->enabled) {
    ret = hal_pwm_disable(priv->pwm_id);
    if (ret < 0) {
      hal_log_err("Failed to disable PWM%d before restart: %d\n", priv->pwm_id, ret);
      return ret;
    }
    priv->enabled = false;
  }

  period_ns = PWM_10M_Hz_ns / info->frequency;

  for (i = 0; i < CONFIG_PWM_NCHANNELS; i++) {
    if (info->channels[i].channel <= 0) {
      continue;
    }
    duty_ns[i] = info->channels[i].duty * period_ns /0xffff;
    if (info->channels[i].cpol != PWM_CPOL_NDEF) {
      enum pwm_polarity polarity = (info->channels[i].cpol == PWM_CPOL_HIGH) ?
                                   PWM_POLARITY_NORMAL : PWM_POLARITY_INVERSED;
      ret = hal_pwm_set_polarity(priv->pwm_id, polarity);
      if (ret < 0) {
        hal_log_err("Failed to set PWM%d polarity: %d\n", priv->pwm_id, ret);
        return ret;
      }
    }

    priv->channels[i] = info->channels[i];
  }

  ret = hal_pwm_set(priv->pwm_id, duty_ns[0], period_ns, PWM_SET_CMPA);
  if (ret < 0) {
    hal_log_err("Failed to set PWM%d: %d\n", priv->pwm_id, ret);
    return ret;
  }

  if (CONFIG_PWM_NCHANNELS > 1) {
    ret = hal_pwm_set(priv->pwm_id, duty_ns[1], period_ns, PWM_SET_CMPB);
    if (ret < 0) {
      hal_log_err("Failed to set PWM%d channel B: %d\n", priv->pwm_id, ret);
      return ret;
    }
  }

  ret = hal_pwm_enable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to enable PWM%d: %d\n", priv->pwm_id, ret);
    return ret;
  }

  priv->frequency = info->frequency;
  priv->enabled = true;

  return OK;
}
#endif

static int pwm_start(struct pwm_lowerhalf_s *dev, const struct pwm_info_s *info)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  int ret = OK;
  uint32_t duty_ns, period_ns;

  if (!info) {
    hal_log_err("PWM info is NULL\n");
    return -EINVAL;
  }

  if (info->frequency == 0) {
    hal_log_err("PWM frequency cannot be zero\n");
    return -EINVAL;
  }
  if (priv->enabled) {
    ret = hal_pwm_disable(priv->pwm_id);
    if (ret < 0) {
      hal_log_err("Failed to disable PWM%d before restart: %d\n", priv->pwm_id, ret);
      return ret;
    }
    priv->enabled = false;
    hal_log_info("PWM%d was enabled, stopped for restart\n", priv->pwm_id);
  }

#ifdef CONFIG_PWM_MULTICHAN
  return pwm_start_multichan(priv, info);
#else
  period_ns = PWM_10M_Hz_ns / info->frequency;
  duty_ns = info->duty * period_ns / 0xffff;

  if (info->cpol != PWM_CPOL_NDEF) {
    enum pwm_polarity polarity = (info->cpol == PWM_CPOL_HIGH)
                                     ? PWM_POLARITY_NORMAL
                                     : PWM_POLARITY_INVERSED;
    hal_log_info("PWM%d set polarity %d\n", priv->pwm_id, polarity);
    ret = hal_pwm_set_polarity(priv->pwm_id, polarity);
    if (ret < 0) {
      hal_log_err("Failed to set PWM%d polarity: %d\n", priv->pwm_id, ret);
      return ret;
    }
    priv->cpol = info->cpol;
  }
  hal_log_info("PWM%d set duty %lu  period_ns %lu\n", priv->pwm_id, duty_ns, period_ns);

  ret = hal_pwm_set(priv->pwm_id, duty_ns, period_ns, PWM_SET_CMPA_CMPB);
  if (ret < 0) {
    hal_log_err("Failed to set PWM%d: %d\n", priv->pwm_id, ret);
    return ret;
  }

  ret = hal_pwm_enable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to enable PWM%d: %d\n", priv->pwm_id, ret);
    return ret;
  }
  priv->duty = info->duty;
  priv->frequency = info->frequency;
  priv->enabled = true;

  return OK;
#endif
}



static int pwm_stop(struct pwm_lowerhalf_s *dev)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  int ret;
  if (!priv->enabled) {
    hal_log_warn("PWM%d is not running, skip stop\n", priv->pwm_id);
    return OK;
  }

  ret = hal_pwm_disable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to disable PWM%d: %d\n", priv->pwm_id, ret);
    return ret;
  }
  hal_log_info("PWM%d stop\n", priv->pwm_id);
  priv->enabled = false;
  return OK;
}

static int pwm_ioctl(struct pwm_lowerhalf_s *dev, int cmd, unsigned long arg)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  struct pwm_info_s *info;

  hal_log_info("PWM%d ioctl 0x%x\n", priv->pwm_id, cmd);
  switch (cmd) {
    case PWMIOC_GETCHARACTERISTICS:
      info = (struct pwm_info_s *)((uintptr_t)arg);
      if (!info) {
        return -EINVAL;
      }

      info->frequency = priv->frequency;

#ifdef CONFIG_PWM_MULTICHAN
      for (int i = 0; i < CONFIG_PWM_NCHANNELS; i++) {
        info->channels[i] = priv->channels[i];
      }
#else
      info->duty = priv->duty;
      info->cpol = priv->cpol;
      info->dcpol = priv->dcpol;
#endif
      info->arg = NULL;
      return OK;

    default:
      hal_log_warn("Unsupported PWM ioctl command: 0x%x\n", cmd);
      return -ENOTTY;
  }
}

static const struct pwm_ops_s g_pwmops = {
  .setup    = pwm_setup,
  .shutdown = pwm_shutdown,
  .start    = pwm_start,
  .stop     = pwm_stop,
  .ioctl    = pwm_ioctl,
};

int drv_pwm_nuttx_init(void)
{
  int i, ret, err;
  char devname[16];

  ret = hal_pwm_init();
  if (ret < 0) {
    hal_log_err("Failed to initialize PWM HAL: %d\n", ret);
    return ret;
  }
  err = PWM_ID_MAX_NUM;
  for (i = 0; i < PWM_ID_MAX_NUM; i++) {
    g_aic_pwm[i].dev.ops = &g_pwmops;
    g_aic_pwm[i].pwm_id = i;
    g_aic_pwm[i].frequency = 0;
    g_aic_pwm[i].enabled = false;
#ifndef CONFIG_PWM_MULTICHAN
    g_aic_pwm[i].duty = 0;
    g_aic_pwm[i].cpol = PWM_CPOL_NDEF;
    g_aic_pwm[i].dcpol = PWM_DCPOL_NDEF;
#endif

    snprintf(devname, sizeof(devname), "/dev/pwm%d", i);
    ret = pwm_register(devname, &g_aic_pwm[i].dev);
    if (ret < 0) {
      hal_log_err("Failed to register PWM%d: %d\n", i, ret);
      g_aic_pwm[i].dev.ops = NULL;
      continue;
    }
    err--;
    hal_log_info("Registered PWM%d at %s\n", i, devname);
  }
  if(err == PWM_ID_MAX_NUM){
    hal_pwm_deinit();
    return -EFAULT;
  }
  return OK;
}