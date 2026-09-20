/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include <nuttx/config.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/mutex.h>
#include <nuttx/clock.h>

#include "aic_core.h"
#include "aic_hal_clk.h"
#include "hal_pwm.h"

struct aic_pwm_lowerhalf_s {
  struct pwm_lowerhalf_s dev;
  int pwm_id;
  mutex_t lock;
  int refs;
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

  nxmutex_lock(&priv->lock);

  if (priv->refs == 0) {
    hal_log_info("PWM%d setup\n", priv->pwm_id);
    if (priv->enabled) {
      hal_pwm_disable(priv->pwm_id);
      hal_pwm_ch_deinit(priv->pwm_id);
    }
    hal_pwm_ch_init(priv->pwm_id, PWM_MODE_UP_COUNT, 0, &action0, &action1);
    priv->enabled = false;
  }

  priv->refs++;
  nxmutex_unlock(&priv->lock);

  return OK;
}



static int pwm_shutdown(struct pwm_lowerhalf_s *dev)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);

  nxmutex_lock(&priv->lock);

  if (priv->refs > 0) {
    priv->refs--;

    if (priv->refs == 0) {
      if (priv->enabled) {
        hal_pwm_disable(priv->pwm_id);
      }
      hal_pwm_ch_deinit(priv->pwm_id);
      priv->enabled = false;
      hal_log_info("PWM%d shutdown\n", priv->pwm_id);
    }
  } else {
    hal_log_warn("PWM%d is not running, skip stop\n", priv->pwm_id);
  }

  nxmutex_unlock(&priv->lock);

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

  nxmutex_lock(&priv->lock);

  if (priv->enabled) {
    ret = hal_pwm_disable(priv->pwm_id);
    if (ret < 0) {
      hal_log_err("Failed to disable PWM%d before restart: %d\n", priv->pwm_id, ret);
      nxmutex_unlock(&priv->lock);
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
        nxmutex_unlock(&priv->lock);
        return ret;
      }
    }

    priv->channels[i] = info->channels[i];
  }

  ret = hal_pwm_set(priv->pwm_id, duty_ns[0], period_ns, PWM_SET_CMPA);
  if (ret < 0) {
    hal_log_err("Failed to set PWM%d: %d\n", priv->pwm_id, ret);
    nxmutex_unlock(&priv->lock);
    return ret;
  }

  if (CONFIG_PWM_NCHANNELS > 1) {
    ret = hal_pwm_set(priv->pwm_id, duty_ns[1], period_ns, PWM_SET_CMPB);
    if (ret < 0) {
      hal_log_err("Failed to set PWM%d channel B: %d\n", priv->pwm_id, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }
  }

  ret = hal_pwm_enable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to enable PWM%d: %d\n", priv->pwm_id, ret);
    nxmutex_unlock(&priv->lock);
    return ret;
  }

  priv->frequency = info->frequency;
  priv->enabled = true;

  nxmutex_unlock(&priv->lock);
  return OK;
}
#endif

static int pwm_start(struct pwm_lowerhalf_s *dev, const struct pwm_info_s *info)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  int ret = OK;
  uint32_t duty_ns = 0, period_ns = 0;
  float freq;
  uint32_t duty;
  uint32_t frequency;
  uint8_t cpol;

  if (!info || info->frequency == 0) {
    hal_log_err("PWM info is NULL\n");
    return -EINVAL;
  }
  nxmutex_lock(&priv->lock);

  
  if (info) {
    duty = info->duty;
    frequency = info->frequency;
    cpol = info->cpol;
  } 


#ifdef CONFIG_PWM_MULTICHAN
  nxmutex_unlock(&priv->lock);
  return pwm_start_multichan(priv, info);
#else
  period_ns = NSEC_PER_SEC / frequency;
  freq = (float)duty / 0xffff;
  duty_ns = (uint32_t)(period_ns * freq);

  /*
   * PWMIOC_SETCHARACTERISTICS is also used for an already running PWM.
   * Keep the waveform running when only the duty changes.  Stopping and
   * starting the PWM for every slider sample creates a visible backlight
   * dropout.
   */
  if (priv->enabled && frequency == priv->frequency &&
      (cpol == PWM_CPOL_NDEF || cpol == priv->cpol)) {
    ret = hal_pwm_set(priv->pwm_id, duty_ns, period_ns, PWM_SET_CMPA);
    if (ret < 0) {
      hal_log_err("Failed to update PWM%d: %d\n", priv->pwm_id, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

    priv->duty = duty;
    priv->frequency = frequency;
    if (cpol != PWM_CPOL_NDEF) {
      priv->cpol = cpol;
    }

    syslog(LOG_INFO, "PWM%d update duty %d duty_ns %lu period_ns %lu\n",
           priv->pwm_id, duty, duty_ns, period_ns);
    nxmutex_unlock(&priv->lock);
    return OK;
  }

  if (priv->enabled) {
    ret = hal_pwm_disable(priv->pwm_id);
    if (ret < 0) {
      hal_log_err("Failed to disable PWM%d before restart: %d\n", priv->pwm_id, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }
    priv->enabled = false;
    hal_log_info("PWM%d was enabled, stopped for restart\n", priv->pwm_id);
  }

  if (cpol != PWM_CPOL_NDEF) {
    enum pwm_polarity polarity = (cpol == PWM_CPOL_HIGH)
                                     ? PWM_POLARITY_NORMAL
                                     : PWM_POLARITY_INVERSED;
    hal_log_info("PWM%d set polarity %d\n", priv->pwm_id, polarity);
    ret = hal_pwm_set_polarity(priv->pwm_id, polarity);
    if (ret < 0) {
      hal_log_err("Failed to set PWM%d polarity: %d\n", priv->pwm_id, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }
    priv->cpol = cpol;
  }
  syslog(LOG_INFO, "PWM%d set duty %d  duty_ns %lu  period_ns %lu\n",
         priv->pwm_id, duty, duty_ns, period_ns);

  ret = hal_pwm_set(priv->pwm_id, duty_ns, period_ns, PWM_SET_CMPA);
  if (ret < 0) {
    hal_log_err("Failed to set PWM%d: %d\n", priv->pwm_id, ret);
    nxmutex_unlock(&priv->lock);
    return ret;
  }

  ret = hal_pwm_enable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to enable PWM%d: %d\n", priv->pwm_id, ret);
    nxmutex_unlock(&priv->lock);
    return ret;
  }
  priv->duty = duty;
  priv->frequency = frequency;
  priv->cpol = cpol;
  priv->enabled = true;

  nxmutex_unlock(&priv->lock);
  return OK;
#endif
}

static int pwm_stop(struct pwm_lowerhalf_s *dev)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  int ret;

  nxmutex_lock(&priv->lock);

  if (!priv->enabled) {
    hal_log_warn("PWM%d is not running, skip stop\n", priv->pwm_id);
    nxmutex_unlock(&priv->lock);
    return OK;
  }

  ret = hal_pwm_disable(priv->pwm_id);
  if (ret < 0) {
    hal_log_err("Failed to disable PWM%d: %d\n", priv->pwm_id, ret);
    nxmutex_unlock(&priv->lock);
    return ret;
  }
  hal_log_info("PWM%d stop\n", priv->pwm_id);
  priv->enabled = false;
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int pwm_ioctl(struct pwm_lowerhalf_s *dev, int cmd, unsigned long arg)
{
  struct aic_pwm_lowerhalf_s *priv = to_aic_pwm(dev);
  struct pwm_info_s *info;

  hal_log_info("PWM%d ioctl 0x%x\n", priv->pwm_id, cmd);
  switch (cmd) {
    case PWMIOC_GETCHARACTERISTICS:
      nxmutex_lock(&priv->lock);
      info = (struct pwm_info_s *)((uintptr_t)arg);
      if (!info) {
        nxmutex_unlock(&priv->lock);
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
      nxmutex_unlock(&priv->lock);
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
    nxmutex_init(&g_aic_pwm[i].lock);
    g_aic_pwm[i].refs = 0;
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
      nxmutex_destroy(&g_aic_pwm[i].lock);
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
