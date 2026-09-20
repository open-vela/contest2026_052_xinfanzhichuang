#ifndef __AIC_DRV_PINCTRL_H__
#define __AIC_DRV_PINCTRL_H__

#include <nuttx/pinctrl/pinctrl.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int aic_pinctrl_initialize(void);
FAR struct pinctrl_dev_s *aic_get_pinctrl_dev(void);

#endif /* __AIC_DRV_PINCTRL_H__ */