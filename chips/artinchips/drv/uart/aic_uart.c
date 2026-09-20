/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/serial/tioctl.h>

#include <aic_soc.h>
#include "aic_core.h"
#include "aic_hal_uart.h"
#include "aic_hal_clk.h"
#include "aic_hal_reset.h"
#include "aic_hal_gpio.h"
#include <debug.h>
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register Access Macros ***************************************************/

#ifndef getreg32
#define getreg32(a) (*(volatile uint32_t *)(a))
#endif

#ifndef putreg32
#define putreg32(v, a) (*(volatile uint32_t *)(a) = (v))
#endif

/* UART Register Offsets (Standard 16550) ***********************************/

#define UART_RBR_OFF 0x00 /* Receive Buffer Register */
#define UART_THR_OFF 0x00 /* Transmitter Holding Register */
#define UART_IER_OFF 0x04 /* Interrupt Enable Register */
#define UART_FCR_OFF 0x08 /* FIFO Control Register (Write Only) */

#define UART_FCR_ENABLE 0x01 /* FIFO Enable */
#define UART_FCR_RXRST 0x02  /* RX FIFO Reset */
#define UART_FCR_TXRST 0x04  /* TX FIFO Reset */
#define UART_FCR_TRIG1 0x00  /* RX Trigger Level 1 Char */
#define UART_IIR_OFF 0x08    /* Interrupt Identity Register */
#define UART_LSR_OFF 0x14    /* Line Status Register */
#define UART_USR_OFF 0x7C    /* UART Status Register */

#define UART_IER_ERBFI 0x01 /* Enable Received Data Available Interrupt */
#define UART_IER_ETBEI                                                         \
  0x02 /* Enable Transmitter Holding Register Empty Interrupt */

#define UART_IIR_NO_INT 0x01 /* No Interrupt Pending */

/* DW UART specific Bits */
#define UART_IIR_BUSY 0x07         /* DesignWare Busy Detect */
#define UART_IIR_RECV_DATA 0x04    /* Received Data Available */
#define UART_IIR_CHAR_TIMEOUT 0x0C /* Character Timeout */
#define UART_IIR_THR_EMPTY 0x02    /* THR Empty */
#define UART_IIR_RECV_LINE 0x06    /* Receiver Line Status */

/* LSR Bits */
#define UART_LSR_DR 0x01   /* Data Ready */
#define UART_LSR_THRE 0x20 /* THR Empty */
#define UART_LSR_TEMT 0x40 /* Transmitter Empty */

/* USR Bits (DesignWare specific) */
#define UART_USR_BUSY 0x01 /* UART Busy */

/* Default Values (Replaced CONFIG_UART* Macros) ***************************/

#define DEFAULT_BAUD 115200
#define DEFAULT_BITS 8
#define DEFAULT_PARITY 0   /* 0: None, 1: Odd, 2: Even */
#define DEFAULT_STOPBITS 0 /* 0: 1 bit, 1: 2 bits */
#define DEFAULT_BUFSIZE 512

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct up_dev_s {
  usart_handle_t handle; /* Handle provided by ArtInChip HAL */
  int idx;               /* UART index (0, 1, 2...) */
  int irq;               /* IRQ number */
  uint32_t baud;         /* Configured baud rate */
  uint32_t bits;         /* Data bits (5-8) */
  uint8_t parity;        /* 0=none, 1=odd, 2=even */
  uint32_t stopbits;     /* 1 or 2 stop bits */
  uint32_t clk_id;       /* Clock ID */
  uint32_t rst_id;       /* Reset ID */
  bool is_init;          /* Initialization flag */
  spinlock_t lock;       /* Spinlock for synchronization */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int up_setup(struct uart_dev_s *dev);
static void up_shutdown(struct uart_dev_s *dev);
static int up_attach(struct uart_dev_s *dev);
static void up_detach(struct uart_dev_s *dev);
static int up_configure(struct uart_dev_s *dev, bool initial);
static int up_interrupt(int irq, void *context, void *arg);
static int up_ioctl(struct file *filep, int cmd, unsigned long arg);
static int up_receive(struct uart_dev_s *dev, unsigned int *status);
static void up_rxint(struct uart_dev_s *dev, bool enable);
static bool up_rxavailable(struct uart_dev_s *dev);
static void up_send(struct uart_dev_s *dev, int ch);
static void up_txint(struct uart_dev_s *dev, bool enable);
static bool up_txready(struct uart_dev_s *dev);
static bool up_txempty(struct uart_dev_s *dev);
static ssize_t up_recvbuf(struct uart_dev_s *dev, FAR void *buf, size_t len);

#ifdef CONFIG_AIC_EXBT
extern bool aic_board_exbt_uart0_selected(void);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops = {
    .setup = up_setup,
    .shutdown = up_shutdown,
    .attach = up_attach,
    .detach = up_detach,
    .ioctl = up_ioctl,
    .receive = up_receive,
    .rxint = up_rxint,
    .rxavailable = up_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
    .rxflowcontrol = NULL,
#endif
    .send = up_send,
    .txint = up_txint,
    .txready = up_txready,
    .txempty = up_txempty,
    // .recvbuf        = up_recvbuf,
};

/* UART0 Setup **************************************************************/


#ifdef CONFIG_AIC_USING_UART0
static char g_uart0rxbuffer[DEFAULT_BUFSIZE];
static char g_uart0txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart0priv = {
    .idx = 0,
    .irq = AIC_IRQ_UART0,
    .baud = CONFIG_AIC_DEV_UART0_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART0_PARITY,
    .bits = CONFIG_AIC_DEV_UART0_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART0_STOPBITS,
    .clk_id = CLK_UART0,
    .rst_id = RESET_UART0,
};

__attribute__((unused)) static struct uart_dev_s g_uart0port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart0rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart0txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart0priv,
#ifdef CONFIG_AIC_USING_UART0_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART1 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART1
static char g_uart1rxbuffer[DEFAULT_BUFSIZE];
static char g_uart1txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart1priv = {
    .idx = 1,
    .irq = AIC_IRQ_UART1,
    .baud = CONFIG_AIC_DEV_UART1_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART1_PARITY,
    .bits = CONFIG_AIC_DEV_UART1_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART1_STOPBITS,
    .clk_id = CLK_UART1,
    .rst_id = RESET_UART1,
};

static struct uart_dev_s g_uart1port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart1rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart1txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart1priv,
#ifdef CONFIG_AIC_USING_UART1_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART2 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART2
static char g_uart2rxbuffer[DEFAULT_BUFSIZE];
static char g_uart2txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart2priv = {
    .idx = 2,
    .irq = AIC_IRQ_UART2,
    .baud = CONFIG_AIC_DEV_UART2_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART2_PARITY,
    .bits = CONFIG_AIC_DEV_UART2_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART2_STOPBITS,
    .clk_id = CLK_UART2,
    .rst_id = RESET_UART2,
};

__attribute__((unused)) static struct uart_dev_s g_uart2port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart2rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart2txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart2priv,
#ifdef CONFIG_AIC_USING_UART2_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART3 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART3
static char g_uart3rxbuffer[DEFAULT_BUFSIZE];
static char g_uart3txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart3priv = {
    .idx = 3,
    .irq = AIC_IRQ_UART3,
    .baud = CONFIG_AIC_DEV_UART3_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART3_PARITY,
    .bits = CONFIG_AIC_DEV_UART3_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART3_STOPBITS,
    .clk_id = CLK_UART3,
    .rst_id = RESET_UART3,
};

__attribute__((unused)) static struct uart_dev_s g_uart3port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart3rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart3txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart3priv,
#ifdef CONFIG_AIC_USING_UART3_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART4 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART4
static char g_uart4rxbuffer[DEFAULT_BUFSIZE];
static char g_uart4txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart4priv = {
    .idx = 4,
    .irq = AIC_IRQ_UART4,
    .baud = CONFIG_AIC_DEV_UART4_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART4_PARITY,
    .bits = CONFIG_AIC_DEV_UART4_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART4_STOPBITS,
    .clk_id = CLK_UART4,
    .rst_id = RESET_UART4,
};

__attribute__((unused)) static struct uart_dev_s g_uart4port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart4rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart4txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart4priv,
#ifdef CONFIG_AIC_USING_UART4_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART5 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART5
static char g_uart5rxbuffer[DEFAULT_BUFSIZE];
static char g_uart5txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart5priv = {
    .idx = 5,
    .irq = AIC_IRQ_UART5,
    .baud = CONFIG_AIC_DEV_UART5_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART5_PARITY,
    .bits = CONFIG_AIC_DEV_UART5_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART5_STOPBITS,
    .clk_id = CLK_UART5,
    .rst_id = RESET_UART5,
};

__attribute__((unused)) static struct uart_dev_s g_uart5port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart5rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart5txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart5priv,
#ifdef CONFIG_AIC_USING_UART5_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART6 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART6
static char g_uart6rxbuffer[DEFAULT_BUFSIZE];
static char g_uart6txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart6priv = {
    .idx = 6,
    .irq = AIC_IRQ_UART6,
    .baud = CONFIG_AIC_DEV_UART6_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART6_PARITY,
    .bits = CONFIG_AIC_DEV_UART6_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART6_STOPBITS,
    .clk_id = CLK_UART6,
    .rst_id = RESET_UART6,
};

__attribute__((unused)) static struct uart_dev_s g_uart6port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart6rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart6txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart6priv,
#ifdef CONFIG_AIC_USING_UART6_CONSOLE
    .isconsole = true,
#endif
};
#endif

/* UART7 Setup **************************************************************/
#ifdef CONFIG_AIC_USING_UART7
static char g_uart7rxbuffer[DEFAULT_BUFSIZE];
static char g_uart7txbuffer[DEFAULT_BUFSIZE];
static struct up_dev_s g_uart7priv = {
    .idx = 7,
    .irq = AIC_IRQ_UART7,
    .baud = CONFIG_AIC_DEV_UART7_BAUDRATE,
    .parity = CONFIG_AIC_DEV_UART7_PARITY,
    .bits = CONFIG_AIC_DEV_UART7_DATABITS,
    .stopbits = CONFIG_AIC_DEV_UART7_STOPBITS,
    .clk_id = CLK_UART7,
    .rst_id = RESET_UART7,
};

__attribute__((unused)) static struct uart_dev_s g_uart7port = {
    .recv = {.size = DEFAULT_BUFSIZE, .buffer = g_uart7rxbuffer},
    .xmit = {.size = DEFAULT_BUFSIZE, .buffer = g_uart7txbuffer},
    .ops = &g_uart_ops,
    .priv = &g_uart7priv,
#ifdef CONFIG_AIC_USING_UART7_CONSOLE
    .isconsole = true,
#endif
};
#endif

#ifdef CONFIG_AIC_USING_UART0_CONSOLE
static struct uart_dev_s* g_uart_console = &g_uart0port;
#elif defined(CONFIG_AIC_USING_UART1_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart1port;
#elif defined(CONFIG_AIC_USING_UART2_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart2port;
#elif defined(CONFIG_AIC_USING_UART3_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart3port;
#elif defined(CONFIG_AIC_USING_UART4_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart4port;
#elif defined(CONFIG_AIC_USING_UART5_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart5port;
#elif defined(CONFIG_AIC_USING_UART6_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart6port;
#elif defined(CONFIG_AIC_USING_UART7_CONSOLE)
static struct uart_dev_s* g_uart_console = &g_uart7port;
#else
static struct uart_dev_s* g_uart_console = NULL;
#endif

#ifdef CONFIG_AIC_EXBT
static bool g_uart_console_mux_checked;

static void up_console_mux_update(void)
{
  if (g_uart_console_mux_checked)
    {
      return;
    }

  g_uart_console_mux_checked = true;

#ifdef CONFIG_AIC_USING_UART0_CONSOLE
  if (g_uart_console == &g_uart0port && aic_board_exbt_uart0_selected())
    {
      g_uart0port.isconsole = false;
      g_uart_console = NULL;
    }
#endif
}
#else
#  define up_console_mux_update()
#endif




/* Baud rate clock frequencies list from vendor driver (D12x specific) */

struct uart_freq_baud {
  uint32_t baud;
  uint32_t freq;
};

static const struct uart_freq_baud uart_freq_baud_list[] = {
    {300, 53454545},     {600, 53454545},     {1200, 53454545},
    {2400, 53454545},    {4800, 53454545},    {9600, 53454545},
    {14400, 53454545},   {19200, 53454545},   {38400, 53454545},
    {57600, 53454545},   {115200, 53454545},  {230400, 58800000},
    {380400, 49000000},  {460800, 58800000},  {921600, 58800000},
    {1000000, 49000000}, {1152000, 36750000}, {1500000, 49000000},
    {1750000, 84000000}, {2000000, 65333333}, {2450000, 39200000},
    {2500000, 39200000}, {3000000, 49000000}, {0, 0}};

static void up_set_freq(uint32_t baud, int clk_id) {
  int i;
  for (i = 0; uart_freq_baud_list[i].baud != 0; i++) {
    if (baud == uart_freq_baud_list[i].baud) {
      hal_clk_set_freq(clk_id, uart_freq_baud_list[i].freq);
      return;
    }
  }
}

static usart_func_e up_get_uart_func(const struct up_dev_s *priv) {
  switch (priv->idx) {
#ifdef CONFIG_AIC_USING_UART0
  case 0:
    return (usart_func_e)CONFIG_AIC_DEV_UART0_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART1
  case 1:
    return (usart_func_e)CONFIG_AIC_DEV_UART1_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART2
  case 2:
    return (usart_func_e)CONFIG_AIC_DEV_UART2_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART3
  case 3:
    return (usart_func_e)CONFIG_AIC_DEV_UART3_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART4
  case 4:
    return (usart_func_e)CONFIG_AIC_DEV_UART4_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART5
  case 5:
    return (usart_func_e)CONFIG_AIC_DEV_UART5_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART6
  case 6:
    return (usart_func_e)CONFIG_AIC_DEV_UART6_MODE;
#endif
#ifdef CONFIG_AIC_USING_UART7
  case 7:
    return (usart_func_e)CONFIG_AIC_DEV_UART7_MODE;
#endif
  default:
    return USART_FUNC_RS232;
  }
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int up_configure(struct uart_dev_s *dev, bool initial) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  usart_mode_e mode = USART_MODE_ASYNCHRONOUS;
  usart_parity_e parity;
  usart_stop_bits_e stopbits;
  usart_data_bits_e databits;
  usart_func_e func;
  struct aic_usart_priv_t *priv_hal;
  uint32_t ier = 0;
  int ret;

  if (!priv->handle) {
    return -ENODEV;
  }

  /* Tune the module clock for high baud rates such as 1.5M BT HCI. */
  up_set_freq(priv->baud, priv->clk_id);

  /* Configuration Mapping */
  if (priv->bits == 5)
    databits = USART_DATA_BITS_5;
  else if (priv->bits == 6)
    databits = USART_DATA_BITS_6;
  else if (priv->bits == 7)
    databits = USART_DATA_BITS_7;
  else
    databits = USART_DATA_BITS_8;

  if (priv->stopbits == 2)
    stopbits = USART_STOP_BITS_2;
  else
    stopbits = USART_STOP_BITS_1;

  if (priv->parity == 1)
    parity = USART_PARITY_ODD;
  else if (priv->parity == 2)
    parity = USART_PARITY_EVEN;
  else
    parity = USART_PARITY_NONE;

  func = up_get_uart_func(priv);
  priv_hal = (struct aic_usart_priv_t *)priv->handle;

  if (!initial) {
    ier = getreg32(priv_hal->base + UART_IER_OFF);
    putreg32(0, priv_hal->base + UART_IER_OFF);
    (void)getreg32(priv_hal->base + UART_IER_OFF);
  }

  /* Hardware config */
  ret = hal_usart_config(priv->handle, priv->baud, mode, parity, stopbits,
                         databits, func);
  if (ret < 0) {
    return ret;
  }

  hal_uart_reset_fifo(priv->handle);

  /* Enable FIFO with 1-byte RX trigger (Standard 16550 / RT-Thread HAL config).
   * FCR: BIT0=Enable, BIT1=RX Reset, BIT2=TX Reset, BITS 6-7 = 00 (1 byte)
   */
  putreg32(UART_FCR_ENABLE | UART_FCR_RXRST | UART_FCR_TXRST | 0x01,
           priv_hal->base + UART_FCR_OFF);

  if (initial) {
    /* Final clear of IER to ensure clean state before enabling */
    putreg32(0, priv_hal->base + UART_IER_OFF);
  } else {
    putreg32(ier, priv_hal->base + UART_IER_OFF);
  }

  if (!initial && dev != g_uart_console) {
    printf("AIC UART%d: termios baud=%lu current=%lu bits=%lu parity=%u stop=%lu\n",
           priv->idx, (unsigned long)priv->baud,
           (unsigned long)hal_usart_get_cur_baudrate(priv->handle),
           (unsigned long)priv->bits, priv->parity,
           (unsigned long)priv->stopbits);
  }

  return OK;
}

static int up_setup(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  int ret;

  if (priv->is_init) {
    return OK;
  }

  /* Initialize Power/Clock/Reset */
  hal_clk_disable(priv->clk_id);
  hal_reset_assert(priv->rst_id);
  up_udelay(100);
  hal_reset_deassert(priv->rst_id);

  /* Precise frequency tuning for baud accuracy: UART clock is 48MHz */
  up_set_freq(priv->baud, priv->clk_id);
  hal_clk_enable(priv->clk_id);

  /* Init HAL Instance */
  priv->handle = hal_usart_initialize(priv->idx, NULL, NULL);
  if (!priv->handle) {
    return -ENODEV;
  }

  ret = up_configure(dev, true);
  if (ret < 0) {
    return ret;
  }

  priv->is_init = true;
  spin_lock_init(&priv->lock);
  return OK;
}

static void up_shutdown(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  up_detach(dev);
  hal_clk_disable(priv->clk_id);
  priv->is_init = false;
}

static int up_attach(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  int ret;

  ret = irq_attach(priv->irq, up_interrupt, dev);
  if (ret == OK) {
    up_enable_irq(priv->irq);
  }
  return ret;
}

static void up_detach(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);

  hal_usart_set_interrupt(priv->handle, USART_INTR_READ, 0);
  hal_usart_set_interrupt(priv->handle, USART_INTR_WRITE, 0);
}

extern volatile uint32_t g_timer_debug_count;
extern volatile uint32_t g_system_ticks;
volatile uint32_t g_uart_tx_count = 0;
volatile uint32_t g_uart_rx_count = 0;

static int up_interrupt(int irq, void *context, void *arg) {
  struct uart_dev_s *dev = (struct uart_dev_s *)arg;
  struct up_dev_s *priv;
  uint32_t status;
  uint32_t lsr;
  int passes = 0;

  priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;

  do {
    lsr = getreg32(priv_hal->base + UART_LSR_OFF);
    status = getreg32(priv_hal->base + UART_IIR_OFF);

    if ((status & UART_IIR_NO_INT) && (status & 0x0f) != UART_IIR_BUSY) {
      break;
    }

    switch (status & 0x0f) {
      case UART_IIR_BUSY:
        /* Clear DesignWare Busy bit. */
        (void)getreg32(priv_hal->base + UART_USR_OFF);
        break;

      case UART_IIR_RECV_LINE:
        /* Reading LSR clears line status interrupt causes. */
        (void)getreg32(priv_hal->base + UART_LSR_OFF);
        break;

      case UART_IIR_RECV_DATA:
      case UART_IIR_CHAR_TIMEOUT:
        g_uart_rx_count++;
        uart_recvchars(dev);
        break;

      case UART_IIR_THR_EMPTY:
        uart_xmitchars(dev);
        break;

      default:
        break;
    }
  } while (++passes < 64);

  return OK;
}

static int up_receive(struct uart_dev_s *dev, unsigned int *status) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;
  uint32_t lsr;
  int ch;

  lsr = getreg32(priv_hal->base + UART_LSR_OFF);
  ch = getreg32(priv_hal->base + UART_RBR_OFF) & 0xff;

  if (status) {
    *status = lsr;
  }

  return ch;
}

__attribute__((unused)) static ssize_t up_recvbuf(struct uart_dev_s *dev,
                                                  FAR void *buf, size_t len) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;
  uint8_t *ptr = (uint8_t *)buf;
  ssize_t recvd = 0;

  while (recvd < (ssize_t)len &&
         (getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_DR)) {
    *ptr++ = (uint8_t)getreg32(priv_hal->base + UART_RBR_OFF);
    recvd++;

    /* Bus sync delay for 400MHz DesignWare UART
     * One additional LSR read ensures the RX FIFO pointer has updated.
     */
    (void)getreg32(priv_hal->base + UART_LSR_OFF);
  }

  return recvd;
}

static void up_rxint(struct uart_dev_s *dev, bool enable) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;
  uint32_t ier;

  ier = getreg32(priv_hal->base + UART_IER_OFF);
  if (enable) {
    ier |= UART_IER_ERBFI;
  } else {
    ier &= ~UART_IER_ERBFI;
  }
  putreg32(ier, priv_hal->base + UART_IER_OFF);

  /* Committed write synchronization */
  (void)getreg32(priv_hal->base + UART_IER_OFF);
}

__attribute__((unused)) static void up_rxint_wrapper(struct uart_dev_s *dev,
                                                     bool enable) {
  up_rxint(dev, enable);
}

static bool up_rxavailable(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;

  /* Check LSR bit 0 (DR) */
  return (getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_DR) != 0;
}

static void up_send(struct uart_dev_s *dev, int ch) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;
  uint32_t count = 0;
  irqstate_t flags;

  /* Get spinlock to ensure atomic access to UART registers */
  flags = spin_lock_irqsave(&priv->lock);

  /*
   * Wait for LSR.THRE.
   * THR Empty must be true before writing for DesignWare stability.
   */
  while ((getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_THRE) == 0) {
    if (++count > 100000)
      break;
  }

  g_uart_tx_count++;
  putreg32(ch, priv_hal->base + UART_THR_OFF);

  /* Release spinlock */
  spin_unlock_irqrestore(&priv->lock, flags);
}

static void up_txint(struct uart_dev_s *dev, bool enable) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;
  irqstate_t flags;
  uint32_t ier;

  /* STABLE TX INT:
   * Enable ETBEI only when needed.
   * Ensure it doesn't storm by checking the THRE status.
   */
  flags = uart_spinlock(dev, true);
  ier = getreg32(priv_hal->base + UART_IER_OFF);
  if (enable) {
    ier = getreg32(priv_hal->base + UART_IER_OFF);
    if (!(ier & UART_IER_ETBEI)) {
      /* Enable TX empty interrupt */
      ier |= UART_IER_ETBEI;
      putreg32(ier, priv_hal->base + UART_IER_OFF);

      /* PRIME THE PUMP:
       * For DesignWare/16550, if the FIFO is empty, we must
       * manually trigger the first send to kick off interrupts.
       */
      if (getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_THRE) {
        uart_xmitchars(dev);
      }
    }
  } else {
    ier &= ~UART_IER_ETBEI;
    putreg32(ier, priv_hal->base + UART_IER_OFF);

    /* CRITICAL for 400MHz: Ensure IER write is committed
     * to drop the interrupt line immediately.
     */
    (void)getreg32(priv_hal->base + UART_IER_OFF);
  }

  uart_spinunlock(dev, true, flags);
}

static bool up_txready(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;

  /* Check LSR bit 5 (THRE) */
  return (getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_THRE) != 0;
}

static bool up_txempty(struct uart_dev_s *dev) {
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct aic_usart_priv_t *priv_hal = (struct aic_usart_priv_t *)priv->handle;

  /* Check LSR bit 6 (TEMT) */
  return (getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_TEMT) != 0;
}

static int up_ioctl(struct file *filep, int cmd, unsigned long arg) {
  struct uart_dev_s *dev = filep->f_inode->i_private;
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  struct termios *termiosp = (struct termios *)arg;
  int ret = OK;

  switch (cmd) {
  case TCGETS:
    if (!termiosp) {
      return -EINVAL;
    }

    termiosp->c_cflag = CLOCAL | CREAD;
    switch (priv->bits) {
    case 5:
      termiosp->c_cflag |= CS5;
      break;
    case 6:
      termiosp->c_cflag |= CS6;
      break;
    case 7:
      termiosp->c_cflag |= CS7;
      break;
    case 8:
    default:
      termiosp->c_cflag |= CS8;
      break;
    }

    if (priv->stopbits == 2) {
      termiosp->c_cflag |= CSTOPB;
    }

    if (priv->parity != 0) {
      termiosp->c_cflag |= PARENB;
      if (priv->parity == 1) {
        termiosp->c_cflag |= PARODD;
      }
    }

    cfsetispeed(termiosp, priv->baud);
    break;

  case TCSETS:
    if (!termiosp) {
      return -EINVAL;
    }

    switch (termiosp->c_cflag & CSIZE) {
    case CS5:
      priv->bits = 5;
      break;
    case CS6:
      priv->bits = 6;
      break;
    case CS7:
      priv->bits = 7;
      break;
    case CS8:
    default:
      priv->bits = 8;
      break;
    }

    if ((termiosp->c_cflag & PARENB) != 0) {
      priv->parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
    } else {
      priv->parity = 0;
    }

    priv->baud = cfgetispeed(termiosp);
    priv->stopbits = (termiosp->c_cflag & CSTOPB) ? 2 : 1;

    if (priv->is_init) {
      ret = up_configure(dev, false);
    }
    break;

  default:
    ret = -ENOTTY;
    break;
  }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Output one character on the console UART.
 *
 ****************************************************************************/

void up_putc(int ch) {

  if (!g_uart_console) {
    return;
  }

  struct up_dev_s *priv = g_uart_console->priv;

  struct aic_usart_priv_t *priv_hal;
  uint32_t count = 0;
  irqstate_t flags;

  if (!priv->is_init) {
    up_setup(g_uart_console);
  }

  priv_hal = (struct aic_usart_priv_t *)priv->handle;

  /* Get spinlock to ensure atomic access to UART registers */
  flags = spin_lock_irqsave(&priv->lock);

  /* Standard 16550: Wait for THR Empty (LSR Bit 5) */
  while ((getreg32(priv_hal->base + UART_LSR_OFF) & UART_LSR_THRE) == 0) {
    if (++count > 1000000) {
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }
  }

  /* Writing to THR is the fastest path. */
  putreg32(ch, priv_hal->base + UART_THR_OFF);

  /* Manual sync delay */
  (void)getreg32(priv_hal->base + UART_LSR_OFF);

  /* Release spinlock */
  spin_unlock_irqrestore(&priv->lock, flags);
}

void riscv_earlyserialinit(void) {
  /* Setup the console UART early */

  // up_setup(&g_uart1port);
}

void riscv_serialinit(void) {
  /* Register the console and other available UARTs */


  up_console_mux_update();

  if (g_uart_console != NULL) {
    uart_register("/dev/console", g_uart_console);
  }


#ifdef CONFIG_AIC_USING_UART0
  uart_register("/dev/ttyS0", &g_uart0port);
#endif

#ifdef CONFIG_AIC_USING_UART1
  uart_register("/dev/ttyS1", &g_uart1port);
#endif

#ifdef CONFIG_AIC_USING_UART2
  uart_register("/dev/ttyS2", &g_uart2port);
#endif

#ifdef CONFIG_AIC_USING_UART3
  uart_register("/dev/ttyS3", &g_uart3port);
#endif

#ifdef CONFIG_AIC_USING_UART4
  uart_register("/dev/ttyS4", &g_uart4port);
#endif

#ifdef CONFIG_AIC_USING_UART5
  uart_register("/dev/ttyS5", &g_uart5port);
#endif

#ifdef CONFIG_AIC_USING_UART6
  uart_register("/dev/ttyS6", &g_uart6port);
#endif

#ifdef CONFIG_AIC_USING_UART7
  uart_register("/dev/ttyS7", &g_uart7port);
#endif

}
