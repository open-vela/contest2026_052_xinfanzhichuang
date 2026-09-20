/*
 * BT8858A start/stop glue for NuttX/Vela.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "bt_config.h"
#include "bt_api.h"
#include "bt_os.h"

#define BT_GPIO_GROUP_SIZE 32
#define BT_GPIO_GROUP(pin) ((pin) / BT_GPIO_GROUP_SIZE)
#define BT_GPIO_GROUP_PIN(pin) ((pin) % BT_GPIO_GROUP_SIZE)

int hal_gpio_name2pin(const char *name);
int hal_gpio_direction_output(unsigned int group, unsigned int pin);
int hal_gpio_set_pin_value(unsigned int pin, unsigned int value);
int hal_gpio_set_func(unsigned int group, unsigned int pin, unsigned int func);
int hal_gpio_set_drive_strength(unsigned int group, unsigned int pin,
                                unsigned int strength);
int hal_gpio_set_bias_pull(unsigned int group, unsigned int pin,
                           unsigned int pull);

#define BT_UART2_TX_GPIO "PD.4"
#define BT_UART2_RX_GPIO "PD.5"
#define BT_UART2_RTS_GPIO "PA.3"
#define BT_UART2_CTS_GPIO "PA.2"
#define BT_UART2_TXRX_FUNC 5
#define BT_UART2_FLOW_FUNC 8
#define BT_UART2_DRV      3
#define BT_PIN_PULL_DIS   0
#define BT_PIN_PULL_UP    3

#ifndef CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO
#define CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO "PD.6"
#endif

#define BT_POWER_OFF_DELAY_US 100000
#define BT_POWER_ON_DELAY_US  800000
#define BT_CMD_DELAY_US       100000
#define BT_CMD_POWER_ON       "P1"
#define BT_CMD_POWER_OFF      "P0"

void BtCmdSend(char *CmdStr, unsigned char len);

static pthread_mutex_t g_bt_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_bt_started;
static int g_bt_on_pin = -1;

static int bt_uart_pinmux_one(const char *name, unsigned int func,
                              unsigned int pull)
{
    int pin = hal_gpio_name2pin(name);
    unsigned int group;
    unsigned int group_pin;

    if (pin < 0) {
        printf("BT: invalid UART2 pin %s\n", name);
        return -EINVAL;
    }

    group = BT_GPIO_GROUP(pin);
    group_pin = BT_GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(group, group_pin, func);
    hal_gpio_set_bias_pull(group, group_pin, pull);
    hal_gpio_set_drive_strength(group, group_pin, BT_UART2_DRV);

    return 0;
}

static int bt_uart_pinmux_init(void)
{
    int ret;

    ret = bt_uart_pinmux_one(BT_UART2_TX_GPIO, BT_UART2_TXRX_FUNC,
                             BT_PIN_PULL_DIS);
    if (ret < 0)
        return ret;

    ret = bt_uart_pinmux_one(BT_UART2_RX_GPIO, BT_UART2_TXRX_FUNC,
                             BT_PIN_PULL_UP);
    if (ret < 0)
        return ret;

#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
    ret = bt_uart_pinmux_one(BT_UART2_RTS_GPIO, BT_UART2_FLOW_FUNC,
                             BT_PIN_PULL_DIS);
    if (ret < 0)
        return ret;

    ret = bt_uart_pinmux_one(BT_UART2_CTS_GPIO, BT_UART2_FLOW_FUNC,
                             BT_PIN_PULL_UP);
    if (ret < 0)
        return ret;

    printf("BT: UART2 pinmux %s(TX)/%s(RX) func %d, %s(RTS)/%s(CTS) func %d\n",
           BT_UART2_TX_GPIO, BT_UART2_RX_GPIO, BT_UART2_TXRX_FUNC,
           BT_UART2_RTS_GPIO, BT_UART2_CTS_GPIO, BT_UART2_FLOW_FUNC);
#else
    printf("BT: UART2 pinmux %s(TX)/%s(RX) func %d, RTS/CTS disabled\n",
           BT_UART2_TX_GPIO, BT_UART2_RX_GPIO, BT_UART2_TXRX_FUNC);
#endif
    return 0;
}

static int bt_power_init(void)
{
    int pin;

    if (g_bt_on_pin >= 0)
        return 0;

    pin = hal_gpio_name2pin(CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO);
    if (pin < 0) {
        printf("BT: invalid BT_ON gpio %s\n",
               CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO);
        return -EINVAL;
    }

    hal_gpio_direction_output(BT_GPIO_GROUP(pin), BT_GPIO_GROUP_PIN(pin));
    g_bt_on_pin = pin;
    printf("BT: using BT_ON gpio %s pin %d\n",
           CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO, g_bt_on_pin);
    return 0;
}

static void bt_power_set(bool on)
{
    if (bt_power_init() == 0)
        hal_gpio_set_pin_value((unsigned int)g_bt_on_pin, on ? 1 : 0);
}

int aic_bt_core_start(void)
{
    int ret;

    pthread_mutex_lock(&g_bt_lock);

    if (g_bt_started) {
        printf("BT: already started\n");
        pthread_mutex_unlock(&g_bt_lock);
        return 0;
    }

    ret = bt_power_init();
    if (ret < 0)
        goto out;

    ret = bt_uart_pinmux_init();
    if (ret < 0)
        goto out;

    bt_power_set(false);
    usleep(BT_POWER_OFF_DELAY_US);
    bt_power_set(true);
    usleep(BT_POWER_ON_DELAY_US);

    ret = bt_para_init();
    if (ret != EPDK_OK) {
        ret = -ENOMEM;
        goto power_off;
    }

    ret = nuttx_com_uart_init();
    if (ret != EPDK_OK) {
        ret = -EIO;
        goto para_exit;
    }

    ret = nuttx_com_uart_flush();
    if (ret != EPDK_OK) {
        ret = -EIO;
        goto uart_exit;
    }

    ret = nuttx_bt_task_start();
    if (ret != EPDK_OK) {
        ret = -EIO;
        goto uart_exit;
    }

    g_bt_started = true;
    BtCmdSend((char *)BT_CMD_POWER_ON, sizeof(BT_CMD_POWER_ON) - 1);
    usleep(BT_CMD_DELAY_US);
    printf("BT: started\n");
    ret = 0;
    goto out;

uart_exit:
    nuttx_com_uart_deinit();
para_exit:
    bt_para_exit();
power_off:
    bt_power_set(false);
out:
    pthread_mutex_unlock(&g_bt_lock);
    return ret;
}

int aic_bt_core_stop(void)
{
    pthread_mutex_lock(&g_bt_lock);

    if (!g_bt_started) {
        bt_power_set(false);
        printf("BT: already stopped\n");
        pthread_mutex_unlock(&g_bt_lock);
        return 0;
    }

    BtCmdSend((char *)BT_CMD_POWER_OFF, sizeof(BT_CMD_POWER_OFF) - 1);
    usleep(BT_CMD_DELAY_US);

    nuttx_bt_task_stop();
    nuttx_com_uart_flush();
    nuttx_com_uart_deinit();
    bt_para_exit();
    bt_power_set(false);

    g_bt_started = false;
    printf("BT: stopped\n");

    pthread_mutex_unlock(&g_bt_lock);
    return 0;
}
