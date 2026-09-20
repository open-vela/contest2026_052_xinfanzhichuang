#ifndef __AIC_I2C_H
#define __AIC_I2C_H

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

FAR struct i2c_master_s *aic_i2cbus_initialize(int port);
int aic_i2cbus_uninitialize(FAR struct i2c_master_s *dev);

#endif /* __AIC_I2C_H */
