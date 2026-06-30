/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#ifndef _ARTINCHIP_HAL_PWM_H_
#define _ARTINCHIP_HAL_PWM_H_

#include "aic_common.h"

#ifdef CONFIG_ARCH_CHIP_D12X
#include "../d12x/include/hal_pwm.h"
#elif defined(CONFIG_ARCH_CHIP_D13X)
#include "../d13x/include/hal_pwm.h"
#endif
#define PWM_10M_Hz_ns  1000000000U

#define PWM_PWMx 0x300

#define PWM_CTL 0x000
#define PWM_MCTL 0x004
#define PWM_CKCTL 0x008
#define PWM_INTCTL 0x00C
#define PWM_INTSTS 0x010
#define PWM_TBCTL(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x000)
#define PWM_TBSTS(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x004)
#define PWM_TBPHS(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x008)
#define PWM_TBCTR(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x010)
#define PWM_TBPRD(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x014)
#define PWM_CMPCTL(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x018)
#define PWM_CMPAHR(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x01C)
#define PWM_CMPA(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x020)
#define PWM_CMPB(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x024)
#define PWM_AQCTLA(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x028)
#define PWM_AQCTLB(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x02C)
#define PWM_AQSFRC(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x030)
#define PWM_AQCSFRC(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x034)
#define PWM_DBCTL(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x038)
#define PWM_DBRED(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x03C)
#define PWM_DBFED(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x040)
#define PWM_ETSEL(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x044)
#define PWM_ETPS(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x048)
#define PWM_ETFLG(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x04C)
#define PWM_ETCLR(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x050)
#define PWM_ETFRC(n) (PWM_PWMx + (((n)&0x7) << 8) + 0x054)
#define PWM_VERSION 0xFFC

#define PWM_DEFAULT_TB_CLK_RATE 24000000
#define PWM_DEFAULT_DB_RED 20
#define PWM_DEFAULT_DB_FED 20

#define PWM_ACTION_CFG_NUM 6

#define PWM_CTL_EN BIT(0)
#define PWM_MCTL_PWM0_EN BIT(0)
#define PWM_MCTL_PWM_EN(n) (PWM_MCTL_PWM0_EN << (n))
#define PWM_CKCTL_PWM0_ON BIT(0)
#define PWM_CKCTL_PWM_ON(n) (PWM_CKCTL_PWM0_ON << (n))
#define PWM_INTCTL_PWM0_ON BIT(0)
#define PWM_INTCTL_PWM_ON(n) (PWM_INTCTL_PWM0_ON << (n))
#define PWM_TBCTL_CLKDIV_MAX 0xFFF
#define PWM_TBCTL_CLKDIV_SHIFT 16
#define PWM_TBCTL_CLKDIV_MASK GENMASK(27, 16)
#define PWM_TBCTL_CTR_MODE_SHITF 0
#define PWM_TBCTL_CTR_MODE_MASK GENMASK(1, 0)
#define PWM_TBPRD_MAX 0xFFFF
#define PWM_AQCTL_DEF_LEVEL BIT(16)
#define PWM_AQCTL_CBD_SHIFT 10
#define PWM_AQCTL_CBU_SHIFT 8
#define PWM_AQCTL_CAD_SHIFT 6
#define PWM_AQCTL_CAU_SHIFT 4
#define PWM_AQCTL_PRD_SHIFT 2
#define PWM_AQCTL_MASK 0x3
#define PWM_ETSEL_INTEN_SHIFT 3
#define PWM_ETSEL_INTSEL_SHIFT 0
#define PWM_SHADOW_SEL_ZRQ_PRD 0xa

enum pwm_polarity {
  PWM_POLARITY_NORMAL,
  PWM_POLARITY_INVERSED,
};

enum aic_pwm_irq_event {
  PWM_RESERVE_EVENT1 = 0,
  PWM_ZERO_EVENT,
  PWM_PRD_EVENT,
  PWM_RESERVE_EVENT2,
  PWM_CMPA_UP_EVENT,
  PWM_CMPA_DOWN_EVENT,
  PWM_CMPB_UP_EVENT,
  PWM_CMPB_DOWN_EVENT
};

enum aic_pwm_mode {
  PWM_MODE_UP_COUNT = 0,
  PWM_MODE_DOWN_COUNT,
  PWM_MODE_UP_DOWN_COUNT,
  PWM_MODE_NUM
};

enum aic_pwm_action_type {
  PWM_ACT_NONE = 0,
  PWM_ACT_LOW,
  PWM_ACT_HIGH,
  PWM_ACT_INVERSE,
  PWM_ACT_NUM
};

enum aic_pwm_cmp_write_type {
  PWM_SET_CMPA = 0,
  PWM_SET_CMPB,
  PWM_SET_CMPA_CMPB
};

struct aic_pwm_action {
  enum aic_pwm_action_type CBD;
  enum aic_pwm_action_type CBU;
  enum aic_pwm_action_type CAD;
  enum aic_pwm_action_type CAU;
  enum aic_pwm_action_type PRD;
  enum aic_pwm_action_type ZRO;
};

struct aic_pwm_arg {
  u16 available;
  u16 id;
  enum aic_pwm_mode mode;
  u32 clk_rate;
  u32 tb_clk_rate;
  float freq;
  struct aic_pwm_action action0;
  struct aic_pwm_action action1;
  u32 period;
  u32 duty;
  s32 def_level;
  enum pwm_polarity polarity;
};

enum aic_pwm_int_event {
  PWM_CMPA_UP = 0,
  PWM_CMPA_DOWN,
  PWM_CMPB_UP,
  PWM_CMPB_DOWN
};

struct aic_pwm_pulse_para {
  u32 prd_ns;
  u32 duty_ns;
  u32 pulse_cnt;
};

void hal_pwm_ch_init(u32 ch, enum aic_pwm_mode mode, u32 default_level,
                     struct aic_pwm_action *a0, struct aic_pwm_action *a1);
void hal_pwm_ch_deinit(u32 ch);
int hal_pwm_set_prd(u32 ch, u32 cnt);
int hal_pwm_set(u32 ch, u32 duty_ns, u32 period_ns, u32 output);
int hal_pwm_get(u32 ch, u32 *duty_ns, u32 *period_ns);
int hal_pwm_set_polarity(u32 ch, enum pwm_polarity polarity);
int hal_pwm_enable(u32 ch);
int hal_pwm_disable(u32 ch);
u32 hal_pwm_int_sts(void);
void hal_pwm_clr_int(u32 stat);
void hal_pwm_int_config(u32 ch, enum aic_pwm_irq_event irq_mode, u8 enable);

int hal_pwm_init(void);
int hal_pwm_deinit(void);

void hal_pwm_status_show(void);
int hal_pwm_set_tb(u32 ch, int freq);
void hal_show_pwm_info(int ch);

#endif // end of _ARTINCHIP_HAL_PWM_H_
