/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/arch.h>
#include <nuttx/config.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "aic_core.h"
#include "aic_drv_gpio.h"
#include "aic_hal_gpio.h"
#include <nuttx/spinlock.h>
#if defined(CONFIG_DEV_GPIO)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_gpio_dev_s {
  struct gpio_dev_s gpio;
  uint32_t pin; /* GPIO pin number/ID */
  pin_interrupt_t callback;
};
#endif
/* GPIO中断条目 */
struct gpio_irq_entry_s {
  uint32_t pin;               /* GPIO引脚号 */
  uint32_t intr_type;         /* 中断触发类型 */
  gpio_irq_handler_t handler; /* 中断处理函数 */
  void *arg;                  /* 传递给处理函数的参数 */
  bool enabled;               /* 是否启用 */
  bool work_scheduled;        /* 是否已计划工作 */
  struct work_s work; /* 工作结构体，用于中断处理的下半部 */
};

/* GPIO中断链表头 */
static struct gpio_irq_entry_s *g_gpio_irq_list[GPIO_MAX_PIN] = {};

/* 自旋锁保护中断链表 */
static spinlock_t g_gpio_irq_lock;

/* GPIO中断是否已初始化 */
static bool g_gpio_irq_initialized = false;

#if defined(CONFIG_DEV_GPIO)
/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int aic_gpio_read(FAR struct gpio_dev_s *dev, FAR bool *value);
static int aic_gpio_write(FAR struct gpio_dev_s *dev, bool value);
static int aic_gpio_attach(FAR struct gpio_dev_s *dev,
                           pin_interrupt_t callback);
static int aic_gpio_enable(FAR struct gpio_dev_s *dev, bool enable);
static int aic_gpio_setpintype(FAR struct gpio_dev_s *dev,
                               enum gpio_pintype_e pintype);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct gpio_operations_s g_aic_gpio_ops = {
    .go_read = aic_gpio_read,
    .go_write = aic_gpio_write,
    .go_attach = aic_gpio_attach,
    .go_enable = aic_gpio_enable,
    .go_setpintype = aic_gpio_setpintype,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int aic_gpio_read(FAR struct gpio_dev_s *dev, FAR bool *value) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)dev;
  unsigned int pin = aic_dev->pin;
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);
  unsigned int val = 0;

  hal_gpio_get_value(g, p, &val);
  *value = (val != 0);

  return OK;
}

static int aic_gpio_write(FAR struct gpio_dev_s *dev, bool value) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)dev;
  unsigned int pin = aic_dev->pin;
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  if (value) {
    hal_gpio_set_output(g, p);
  } else {
    hal_gpio_clr_output(g, p);
  }

  return OK;
}

static int aic_gpio_attach(FAR struct gpio_dev_s *dev,
                           pin_interrupt_t callback) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)dev;
  aic_dev->callback = callback;
  return OK;
}

static int aic_gpio_isr(int irq, FAR void *context, FAR void *arg) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)arg;

  if (aic_dev->callback != NULL) {
    aic_dev->callback(&aic_dev->gpio, aic_dev->pin);
  }

  return OK;
}

static int aic_gpio_enable(FAR struct gpio_dev_s *dev, bool enable) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)dev;
  unsigned int pin = aic_dev->pin;
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);
  int irq = AIC_GPIO_TO_IRQ(pin);
  int ret;

  if (enable) {
    if (aic_dev->callback != NULL) {
      ret = irq_attach(irq, aic_gpio_isr, aic_dev);
      if (ret < 0) {
        gpioerr("ERROR: irq_attach failed: %d\n", ret);
        return ret;
      }
      up_enable_irq(irq);
    }
    hal_gpio_enable_irq(g, p);
  } else {
    hal_gpio_disable_irq(g, p);
    up_disable_irq(irq);
    irq_detach(irq);
  }

  return OK;
}

static int aic_gpio_setpintype(FAR struct gpio_dev_s *dev,
                               enum gpio_pintype_e pintype) {
  struct aic_gpio_dev_s *aic_dev = (struct aic_gpio_dev_s *)dev;
  unsigned int pin = aic_dev->pin;
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);
  unsigned int irq_mode = 0;
  bool is_irq = false;

  /* Default to GPIO function (usually 1) */
  hal_gpio_set_func(g, p, 1);

  switch (pintype) {
  case GPIO_INPUT_PIN:
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DIS);
    hal_gpio_direction_input(g, p);
    break;
  case GPIO_INPUT_PIN_PULLUP:
    hal_gpio_set_bias_pull(g, p, PIN_PULL_UP);
    hal_gpio_direction_input(g, p);
    break;
  case GPIO_INPUT_PIN_PULLDOWN:
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DOWN);
    hal_gpio_direction_input(g, p);
    break;
  case GPIO_OUTPUT_PIN:
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DIS);
    hal_gpio_direction_output(g, p);
    break;
  case GPIO_OUTPUT_PIN_OPENDRAIN:
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DIS);
    hal_gpio_direction_output(g, p);
    break;
  case GPIO_INTERRUPT_RISING_PIN:
  case GPIO_INTERRUPT_RISING_PIN_WAKEUP:
    irq_mode = PIN_IRQ_MODE_EDGE_RISING;
    is_irq = true;
    break;
  case GPIO_INTERRUPT_FALLING_PIN:
  case GPIO_INTERRUPT_FALLING_PIN_WAKEUP:
    irq_mode = PIN_IRQ_MODE_EDGE_FALLING;
    is_irq = true;
    break;
  case GPIO_INTERRUPT_BOTH_PIN:
  case GPIO_INTERRUPT_BOTH_PIN_WAKEUP:
    irq_mode = PIN_IRQ_MODE_EDGE_BOTH;
    is_irq = true;
    break;
  case GPIO_INTERRUPT_HIGH_PIN:
  case GPIO_INTERRUPT_HIGH_PIN_WAKEUP:
    irq_mode = PIN_IRQ_MODE_LEVEL_HIGH;
    is_irq = true;
    break;
  case GPIO_INTERRUPT_LOW_PIN:
  case GPIO_INTERRUPT_LOW_PIN_WAKEUP:
    irq_mode = PIN_IRQ_MODE_LEVEL_LOW;
    is_irq = true;
    break;
  default:
    return -EINVAL;
  }

  if (is_irq) {
    hal_gpio_set_irq_mode(g, p, irq_mode);
    hal_gpio_direction_input(g, p);
  }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct gpio_dev_s *aic_gpio_initialize(unsigned int pin) {
  struct aic_gpio_dev_s *aic_dev;

  aic_dev = (struct aic_gpio_dev_s *)kmm_zalloc(sizeof(struct aic_gpio_dev_s));
  if (!aic_dev) {
    return NULL;
  }

  aic_dev->pin = pin;
  /* Default uninitialized state */
  aic_dev->gpio.gp_pintype = GPIO_INPUT_PIN;
  aic_dev->gpio.gp_ops = &g_aic_gpio_ops;

  return &aic_dev->gpio;
}
#endif

int gpio_get_value(uint32_t pin) {
  int group = GPIO_GROUP(pin);
  int pin_index = GPIO_GROUP_PIN(pin);
  unsigned int status;

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    gpioinfo("GPIO Get Value: pin=%ld (group=%d, index=%d) error\n", pin, group,
             pin_index);
    return -EINVAL;
  }

  hal_gpio_get_value(group, pin_index, &status);
  gpioinfo("GPIO Get Value: pin=%ld (group=%d, index=%d) value=%d\n", pin, group,
           pin_index, status);
  return status;
}

int gpio_set_value(uint32_t pin, bool value) {
  int group = GPIO_GROUP(pin);
  int pin_index = GPIO_GROUP_PIN(pin);

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    return -EINVAL;
  }

  hal_gpio_set_value(group, pin_index, value);
  return OK;
}

/****************************************************************************
 * Name: aic_gpio_button_work
 *
 * Description:
 *   Work queue function for button debouncing and event reporting.
 *
 ****************************************************************************/

static void gpio_irq_work(FAR void *arg) {
  FAR struct gpio_irq_entry_s *entry = (FAR struct gpio_irq_entry_s *)arg;

  /* Process all buttons to find which one needs debouncing */

  if (entry->work_scheduled) {
    /* 这里需要做成一个工作队列，因为有多个中断发生时，需要按顺序处理，不能在中断上下文中处理过多逻辑
     */
    gpioinfo("INFO[%d]: GPIO IRQ: pin=%ld  %x\n", __LINE__, entry->pin,
             (unsigned int)entry->arg);
    entry->handler(entry->arg);
    // gpioinfo("GPIO IRQ Work Queue: %d\n", entry->pin);
    entry->work_scheduled = false;
  }
}

/****************************************************************************
 * Name: gpio_interrupt
 *
 * Description:
 *   GPIO中断服务程序
 *   这个函数被注册到D12X_IRQ_GPIO中断
 *   它会检查所有启用的GPIO中断，并调用相应的处理函数
 *
 ****************************************************************************/

int gpio_interrupt(int irq, void *context, void *arg) {
  struct gpio_irq_entry_s *entry;
  uint32_t irq_status;
  unsigned int group_irq_sta;
  unsigned int group_irq_en;
  int ret = OK;
  /* 遍历所有GPIO中断条目 */
  irqstate_t flags = spin_lock_irqsave(&g_gpio_irq_lock);
  int pin_group = 0;
  while (pin_group < GPIO_GROUP_MAX) {

    /* 获取GPIO中断状态 */
    hal_gpio_group_get_irq_stat(pin_group, &group_irq_sta);
    hal_gpio_group_get_irq_en(pin_group, &group_irq_en);
    irq_status = group_irq_sta & group_irq_en;
    int bit_pos = 0;
    while (irq_status != 0) {
      if (irq_status & 0x1) {

        // 这里需要做成一个工作队列，因为有多个中断发生时，需要按顺序处理，不能在中断上下文中处理过多逻辑
        /* 处理该引脚的中断 */
        int pin_number = pin_group * 32 + bit_pos;
        /* 查找对应的中断处理函数并调用 */

        entry = g_gpio_irq_list[pin_number];
        if (entry != NULL && entry->enabled && entry->handler != NULL) {
          gpioinfo("INFO[%d]: GPIO IRQ: pin=%d  %x\n", __LINE__, pin_number,
                   (unsigned int)entry->arg);
          hal_gpio_clr_irq_stat(pin_group, bit_pos);
          /* Schedule work for debouncing */
          if (!entry->work_scheduled) {
            // entry->work_scheduled = true;
            // gpioinfo("GPIO IRQ Work Queue: %d\n", entry->pin);

            ret = work_queue(HPWORK, &entry->work, gpio_irq_work, entry, 0);
            if (ret == OK) {
              entry->work_scheduled = true;
            }
          }
        }
      }
      irq_status >>= 1;
      bit_pos++;
    }
    pin_group++;
  }
  spin_unlock_irqrestore(&g_gpio_irq_lock, flags);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpio_irq_initialize
 *
 * Description:
 *   初始化GPIO中断子系统
 *
 ****************************************************************************/

int gpio_irq_initialize(void) {
  int ret;

  if (g_gpio_irq_initialized) {
    return OK;
  }
  spin_lock_init(&g_gpio_irq_lock);
  memset(g_gpio_irq_list, 0, sizeof(g_gpio_irq_list));
  /* 注册GPIO中断服务程序 */
  ret = irq_attach(AIC_IRQ_GPIO, gpio_interrupt, NULL);
  if (ret < 0) {
    gpioerr("Failed to attach GPIO interrupt handler: %d\n", ret);
    return ret;
  }

  /* 使能GPIO中断 */
  up_enable_irq(AIC_IRQ_GPIO);

  g_gpio_irq_initialized = true;
  gpioinfo("GPIO interrupt subsystem initialized\n");

  return OK;
}

struct gpio_irq_entry_s *gpio_irq_allocate_entry(void) {
  struct gpio_irq_entry_s *entry;

  entry =
      (struct gpio_irq_entry_s *)kmm_malloc(sizeof(struct gpio_irq_entry_s));
  if (entry == NULL) {
    return NULL;
  }

  memset(entry, 0, sizeof(struct gpio_irq_entry_s));
  return entry;
}

/****************************************************************************
 * Name: gpio_irq_free_entry
 *
 * Description:
 *   释放GPIO中断条目
 *
 ****************************************************************************/

void gpio_irq_free_entry(struct gpio_irq_entry_s *entry) {
  if (entry != NULL) {
    kmm_free(entry);
  }
}

/****************************************************************************
 * Name: gpio_irq_attach
 *
 * Description:
 *   为指定GPIO引脚注册中断处理函数
 *
 ****************************************************************************/

int gpio_irq_attach(uint32_t pin, gpio_irq_handler_t handler, void *arg) {
  struct gpio_irq_entry_s *entry;
  irqstate_t flags;
  int ret;
  int group = GPIO_GROUP(pin);
  int pin_index = GPIO_GROUP_PIN(pin);
  unsigned int status;

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    return -EINVAL;
  }

  if (handler == NULL) {
    return -EINVAL;
  }

  /* 确保GPIO中断子系统已初始化 */
  if (!g_gpio_irq_initialized) {
    ret = gpio_irq_initialize();
    if (ret < 0) {
      return ret;
    }
  }

  flags = spin_lock_irqsave(&g_gpio_irq_lock);

  /* 检查是否已存在该引脚的条目 */
  entry = g_gpio_irq_list[pin];
  if (entry != NULL) {
    /* 更新现有条目 */
    if (entry->enabled) {
      /* 如果中断已启用，先禁用以避免竞争条件 */
      hal_gpio_disable_irq(group, pin_index);
    }

    entry->handler = handler;
    entry->arg = arg;
    entry->enabled = false;
    entry->work_scheduled = false;

  } else {
    hal_gpio_get_func(group, pin_index, &status);
    if (status != 1) {
      return -EINVAL; /* 该引脚未配置为GPIO功能，无法附加处理函数 */
    }

    /* 创建新条目 */
    entry = g_gpio_irq_list[pin] = gpio_irq_allocate_entry();
    if (entry == NULL) {
      spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
      return -ENOMEM;
    }
    /* Configure interrupt for both edges */

    entry->pin = pin;
    entry->handler = handler;
    entry->arg = arg;
    entry->enabled = false;
    entry->work_scheduled = false;
  }

  spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: gpio_irq_detach
 *
 * Description:
 *   解除指定GPIO引脚的中断注册
 *
 ****************************************************************************/

int gpio_irq_detach(uint32_t pin) {
  struct gpio_irq_entry_s *entry;
  irqstate_t flags;

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    return -EINVAL;
  }

  flags = spin_lock_irqsave(&g_gpio_irq_lock);

  entry = g_gpio_irq_list[pin];

  if (entry == NULL) {
    spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
    return OK;
  }
  if (entry->work_scheduled) {
    /* 如果有工作计划，先取消 */
    work_cancel(HPWORK, &entry->work);
    entry->work_scheduled = false;
  }
  gpio_irq_free_entry(entry);
  g_gpio_irq_list[pin] = NULL;

  spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: gpio_irq_enable
 *
 * Description:
 *   启用指定GPIO引脚的中断
 *
 ****************************************************************************/

int gpio_irq_enable(uint32_t pin) {
  struct gpio_irq_entry_s *entry;
  irqstate_t flags;
  int group = GPIO_GROUP(pin);
  int pin_index = GPIO_GROUP_PIN(pin);

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    return -EINVAL;
  }

  flags = spin_lock_irqsave(&g_gpio_irq_lock);

  entry = g_gpio_irq_list[pin];

  if (entry == NULL) {
    spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
    return -ENOENT;
  }

  if (entry != NULL && entry->enabled) {
    spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
    return OK;
  }
  hal_gpio_enable_irq(group, pin_index);
  entry->enabled = true;
  spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: gpio_irq_disable
 *
 * Description:
 *   禁用指定GPIO引脚的中断
 *
 ****************************************************************************/

int gpio_irq_disable(uint32_t pin) {
  struct gpio_irq_entry_s *entry;
  irqstate_t flags;
  int group = GPIO_GROUP(pin);
  int pin_index = GPIO_GROUP_PIN(pin);

  /* 参数检查 */
  if (pin >= GPIO_MAX_PINS) {
    return -EINVAL;
  }

  flags = spin_lock_irqsave(&g_gpio_irq_lock);

  entry = g_gpio_irq_list[pin];

  if (entry == NULL || !entry->enabled) {

    return OK;
  }

  hal_gpio_disable_irq(group, pin_index);

  entry->enabled = false;

  spin_unlock_irqrestore(&g_gpio_irq_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: gpio_irq_configure
 *
 * Description:
 *   批量配置多个GPIO中断
 *
 ****************************************************************************/

int gpio_irq_configure(const struct gpio_irq_config_s *configs, int count) {
  int i;
  int ret;
  int pin_group = 0;
  int pin_index = 0;

  if (configs == NULL || count <= 0) {
    return -EINVAL;
  }

  for (i = 0; i < count; i++) {
    const struct gpio_irq_config_s *config = &configs[i];

    ret = gpio_irq_attach(config->pin, config->handler, config->arg);
    if (ret < 0) {
      return ret;
    }

    pin_group = GPIO_GROUP(config->pin);
    pin_index = GPIO_GROUP_PIN(config->pin);

    /* Active low: enable pull-up */
    hal_gpio_set_bias_pull(pin_group, pin_index, PIN_PULL_UP);
    gpiowarn("Configured GPIO pin %ld as active low with pull-up\n",
             config->pin);

    /* Set GPIO function and direction */
    hal_gpio_set_func(pin_group, pin_index, 1); /* GPIO function */
    hal_gpio_direction_input(pin_group, pin_index);

    hal_gpio_set_irq_mode(pin_group, pin_index, config->intr_type);
    if (config->enabled) {
      hal_gpio_enable_irq(pin_group, pin_index);
    }
  }

  return OK;
}
