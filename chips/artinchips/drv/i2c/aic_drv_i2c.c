/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/kmalloc.h>
#include <nuttx/i2c/i2c_master.h>
#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
#include <nuttx/semaphore.h>
#endif

#include <aic_core.h>
#include <hal_i2c.h>
#include <aic_drv_i2c.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_i2c_priv_s {
  struct i2c_master_s dev; /* I2C master device implementation */
  aic_i2c_ctrl aic_bus;    /* AIC_USING HAL I2C control */
  mutex_t lock;            /* Bus access lock */
  int refs;                /* Reference count */
#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
  sem_t waitsem; /* Reserved for future IRQ mode */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int aic_i2c_setup(FAR struct i2c_master_s *dev);
static int aic_i2c_shutdown(FAR struct i2c_master_s *dev);
static int aic_i2c_transfer(FAR struct i2c_master_s *dev,
                            FAR struct i2c_msg_s *msgs, int count);
static int aic_i2c_transfer_poll(FAR struct aic_i2c_priv_s *priv,
                                 FAR struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
static int aic_i2c_transfer_irq(FAR struct aic_i2c_priv_s *priv,
                                FAR struct i2c_msg_s *msgs, int count);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* I2C operations */

static const struct i2c_ops_s g_aic_i2c_ops = {
    .transfer = aic_i2c_transfer,
    .setup = aic_i2c_setup,
    .shutdown = aic_i2c_shutdown,
};

static struct aic_i2c_priv_s *g_aic_i2c_priv[I2C_MAX_NUM];

/* I2C configuration table - populated from Kconfig settings */
static const struct {
  const char *name;
  int         speed_config;
  int         irq_index;
  int         clk_id;
} g_i2c_config[I2C_MAX_NUM] = {
#ifdef CONFIG_AIC_USING_I2C0
  {
    .name = "i2c0",
    .speed_config = CONFIG_AIC_DEV_I2C0_SPEED,
    .irq_index = AIC_IRQ_I2C0,
    .clk_id = CLK_I2C0,
  },
#else
  {NULL, 0, 0, 0},
#endif
#ifdef CONFIG_AIC_USING_I2C1
  {
    .name = "i2c1",
    .speed_config = CONFIG_AIC_DEV_I2C1_SPEED,
    .irq_index = AIC_IRQ_I2C1,
    .clk_id = CLK_I2C1,
  },
#else
  {NULL, 0, 0, 0},
#endif
#ifdef CONFIG_AIC_USING_I2C2
  {
    .name = "i2c2",
    .speed_config = CONFIG_AIC_DEV_I2C2_SPEED,
    .irq_index = AIC_IRQ_I2C2,
    .clk_id = CLK_I2C2,
  },
#else
  {NULL, 0, 0, 0},
#endif
#ifdef CONFIG_AIC_USING_I2C3
  {
    .name = "i2c3",
    .speed_config = CONFIG_AIC_DEV_I2C3_SPEED,
    .irq_index = AIC_IRQ_I2C3,
    .clk_id = CLK_I2C3,
  },
#else
  {NULL, 0, 0, 0},
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int aic_i2c_setup(FAR struct i2c_master_s *dev) {
  FAR struct aic_i2c_priv_s *priv = (FAR struct aic_i2c_priv_s *)dev;
  int ret;

  nxmutex_lock(&priv->lock);

  if (priv->refs == 0) {
    ret = hal_i2c_init(&priv->aic_bus);
    if (ret < 0) {
      nxmutex_unlock(&priv->lock);
      return ret;
    }
  }

  priv->refs++;
  nxmutex_unlock(&priv->lock);

  return OK;
}

static int aic_i2c_shutdown(FAR struct i2c_master_s *dev) {
  FAR struct aic_i2c_priv_s *priv = (FAR struct aic_i2c_priv_s *)dev;

  nxmutex_lock(&priv->lock);

  if (priv->refs > 0) {
    priv->refs--;
    if (priv->refs == 0) {
      hal_i2c_module_disable(&priv->aic_bus);
    }
  }

  nxmutex_unlock(&priv->lock);

  return OK;
}

static int aic_i2c_transfer(FAR struct i2c_master_s *dev,
                            FAR struct i2c_msg_s *msgs, int count) {
  FAR struct aic_i2c_priv_s *priv = (FAR struct aic_i2c_priv_s *)dev;
  int ret;

  if (msgs == NULL || count <= 0) {
    return -EINVAL;
  }

  nxmutex_lock(&priv->lock);

#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
  ret = aic_i2c_transfer_irq(priv, msgs, count);
#else
  ret = aic_i2c_transfer_poll(priv, msgs, count);
#endif

  nxmutex_unlock(&priv->lock);

  return ret;
}

static int aic_i2c_transfer_poll(FAR struct aic_i2c_priv_s *priv,
                                 FAR struct i2c_msg_s *msgs, int count) {
  int ret = OK;
  int i;
  int32_t bytes;
  bool is_last;
  struct aic_i2c_msg aic_msg;

  for (i = 0; i < count; i++) {
    is_last = (i == (count - 1));

    priv->aic_bus.addr_bit =
        (msgs[i].flags & I2C_M_TEN) ? I2C_10BIT_ADDR : I2C_7BIT_ADDR;

    aic_msg.addr = msgs[i].addr;
    aic_msg.flags = 0;
    aic_msg.len = msgs[i].length;
    aic_msg.buf = msgs[i].buffer;

    if ((msgs[i].flags & I2C_M_READ) != 0) {
      bytes =
          hal_i2c_master_receive_msg(&priv->aic_bus, &aic_msg, is_last ? 1 : 0);
    } else {
      bytes =
          hal_i2c_master_send_msg(&priv->aic_bus, &aic_msg, is_last ? 1 : 0);
    }

    if (bytes < 0) {
      i2cerr("I2C HAL transfer error at msg %d: %ld\n", i, (long)bytes);
      ret = (int)bytes;
      break;
    }

    if (bytes != msgs[i].length) {
      i2cerr("I2C transfer incomplete at msg %d: %ld/%d\n", i, (long)bytes,
             msgs[i].length);
      ret = -EIO;
      break;
    }
  }

  return ret;
}

#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
static int aic_i2c_transfer_irq(FAR struct aic_i2c_priv_s *priv,
                                FAR struct i2c_msg_s *msgs, int count) {
  UNUSED(priv);
  UNUSED(msgs);
  UNUSED(count);
  i2cerr(
      "I2C IRQ mode is not ready in NuttX path, fallback to polling config\n");
  return -ENOTSUP;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct i2c_master_s *aic_i2cbus_initialize(int port) {
  FAR struct aic_i2c_priv_s *priv = NULL;
  FAR struct i2c_master_s *dev = NULL;
  int ret;

  if (port < 0 || port >= I2C_MAX_NUM) {
    i2cerr("Invalid I2C port: %d\n", port);
    return NULL;
  }

  if (g_aic_i2c_priv[port] != NULL) {
    i2cinfo("I2C port %d already initialized\n", port);
    return &g_aic_i2c_priv[port]->dev;
  }

  if (g_i2c_config[port].name == NULL) {
    i2cerr("I2C port %d not configured in Kconfig\n", port);
    return NULL;
  }

  priv = (FAR struct aic_i2c_priv_s *)kmm_zalloc(sizeof(struct aic_i2c_priv_s));
  if (priv == NULL) {
    i2cerr("Failed to allocate memory for I2C priv struct\n");
    return NULL;
  }

  priv->dev.ops = &g_aic_i2c_ops;
  priv->aic_bus.index = port;
  priv->aic_bus.reg_base = I2Cx_BASE(port);
  priv->aic_bus.device_name = g_i2c_config[port].name;
  priv->aic_bus.addr_bit = I2C_7BIT_ADDR;
  priv->aic_bus.target_rate = g_i2c_config[port].speed_config;
  priv->aic_bus.bus_mode = I2C_MASTER_MODE;
  priv->aic_bus.irq_index = g_i2c_config[port].irq_index;
  priv->aic_bus.clk_id = g_i2c_config[port].clk_id;
  priv->refs = 0;

  nxmutex_init(&priv->lock);
#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
  nxsem_init(&priv->waitsem, 0, 0);
#endif

  ret = i2c_register(&priv->dev, port);
  if (ret < 0) {
    i2cerr("Failed to register I2C device %d: %d\n", port, ret);
    kmm_free(priv);
    return NULL;
  }

  g_aic_i2c_priv[port] = priv;
  dev = &priv->dev;

  aic_i2c_setup(dev);

  i2cinfo("I2C port %d initialized successfully\n", port);
  return dev;
}

int aic_i2cbus_uninitialize(FAR struct i2c_master_s *dev) {
  FAR struct aic_i2c_priv_s *priv = (FAR struct aic_i2c_priv_s *)dev;
  int port;
  int ret;

  if (priv == NULL) {
    i2cerr("Invalid I2C device pointer\n");
    return -EINVAL;
  }

  port = priv->aic_bus.index;
  if (port < 0 || port >= I2C_MAX_NUM || g_aic_i2c_priv[port] != priv) {
    i2cerr("I2C device not found in registry\n");
    return -EINVAL;
  }

  ret = aic_i2c_shutdown(dev);
  if (ret < 0) {
    i2cerr("Failed to shutdown I2C device %d: %d\n", port, ret);
    return ret;
  }

  i2c_unregister(dev);

  nxmutex_destroy(&priv->lock);
#ifdef CONFIG_AIC_I2C_INTERRUPT_MODE
  nxsem_destroy(&priv->waitsem);
#endif

  kmm_free(priv);
  g_aic_i2c_priv[port] = NULL;

  i2cinfo("I2C port %d uninitialized successfully\n", port);
  return OK;
}