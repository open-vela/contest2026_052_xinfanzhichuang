#include <nuttx/config.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define UART2_TEST_DEV       "/dev/ttyS2"
#define UART2_TEST_BAUD      115200
#define UART2_TEST_MAX_TEXT  96
#define UART2_TEST_MAX_RX    64
#define UART2_TEST_TX_GPIO   "PD.4"
#define UART2_TEST_RX_GPIO   "PD.5"
#define UART2_TEST_RTS_GPIO  "PA.3"
#define UART2_TEST_CTS_GPIO  "PA.2"
#define UART2_TEST_TXRX_FUNC 5
#define UART2_TEST_FLOW_FUNC 8
#define UART2_TEST_DRV       3
#define UART2_TEST_PULL_DIS  0
#define UART2_TEST_PULL_UP   3
#define UART2_TEST_GPIO_PER_GROUP 32
#define UART2_TEST_BASE      0x18712000UL

#define UART_RBR_OFF         0x00
#define UART_THR_OFF         0x00
#define UART_IER_OFF         0x04
#define UART_IIR_OFF         0x08
#define UART_LCR_OFF         0x0c
#define UART_MCR_OFF         0x10
#define UART_LSR_OFF         0x14
#define UART_USR_OFF         0x7c
#define UART_RFL_OFF         0x84

#define UART_LSR_DR          0x01
#define UART_LSR_THRE        0x20
#define UART_LSR_TEMT        0x40

#define UART_RAW_INTERVAL_US 20000
#define UART_RAW_TICKS_PER_SEC (1000000 / UART_RAW_INTERVAL_US)

int hal_gpio_name2pin(const char *name);
int hal_gpio_set_func(unsigned int group, unsigned int pin, unsigned int func);
int hal_gpio_set_drive_strength(unsigned int group, unsigned int pin,
                                unsigned int strength);
int hal_gpio_set_bias_pull(unsigned int group, unsigned int pin,
                           unsigned int pull);

struct uart2test_regs
{
  uint32_t ier;
  uint32_t iir;
  uint32_t lcr;
  uint32_t mcr;
  uint32_t lsr;
  uint32_t usr;
  uint32_t rfl;
};

static uint32_t uart2test_getreg(uint32_t offset)
{
  return *(volatile uint32_t *)(uintptr_t)(UART2_TEST_BASE + offset);
}

static void uart2test_putreg(uint32_t value, uint32_t offset)
{
  *(volatile uint32_t *)(uintptr_t)(UART2_TEST_BASE + offset) = value;
}

static void uart2test_read_regs(struct uart2test_regs *regs)
{
  regs->ier = uart2test_getreg(UART_IER_OFF);
  regs->iir = uart2test_getreg(UART_IIR_OFF);
  regs->lcr = uart2test_getreg(UART_LCR_OFF);
  regs->mcr = uart2test_getreg(UART_MCR_OFF);
  regs->lsr = uart2test_getreg(UART_LSR_OFF);
  regs->usr = uart2test_getreg(UART_USR_OFF);
  regs->rfl = uart2test_getreg(UART_RFL_OFF);
}

static void uart2test_dump_regs(const char *tag)
{
  struct uart2test_regs regs;

  uart2test_read_regs(&regs);
  printf("uart2test: %s regs base=0x%08lx IER=0x%08lx IIR=0x%08lx "
         "LCR=0x%08lx MCR=0x%08lx LSR=0x%08lx USR=0x%08lx RFL=%lu\n",
         tag, (unsigned long)UART2_TEST_BASE, (unsigned long)regs.ier,
         (unsigned long)regs.iir, (unsigned long)regs.lcr,
         (unsigned long)regs.mcr, (unsigned long)regs.lsr,
         (unsigned long)regs.usr, (unsigned long)regs.rfl);
}

static unsigned int uart2test_group(unsigned int pin)
{
  return pin / UART2_TEST_GPIO_PER_GROUP;
}

static unsigned int uart2test_group_pin(unsigned int pin)
{
  return pin % UART2_TEST_GPIO_PER_GROUP;
}

static int uart2test_pinmux_one(const char *name, unsigned int func,
                                unsigned int pull)
{
  int pin = hal_gpio_name2pin(name);

  if (pin < 0)
    {
      printf("uart2test: invalid pin %s\n", name);
      return -EINVAL;
    }

  hal_gpio_set_func(uart2test_group((unsigned int)pin),
                    uart2test_group_pin((unsigned int)pin), func);
  hal_gpio_set_bias_pull(uart2test_group((unsigned int)pin),
                         uart2test_group_pin((unsigned int)pin), pull);
  hal_gpio_set_drive_strength(uart2test_group((unsigned int)pin),
                              uart2test_group_pin((unsigned int)pin),
                              UART2_TEST_DRV);
  return 0;
}

static int uart2test_pinmux(void)
{
  int ret;

  ret = uart2test_pinmux_one(UART2_TEST_TX_GPIO, UART2_TEST_TXRX_FUNC,
                             UART2_TEST_PULL_DIS);
  if (ret < 0)
    return ret;

  ret = uart2test_pinmux_one(UART2_TEST_RX_GPIO, UART2_TEST_TXRX_FUNC,
                             UART2_TEST_PULL_UP);
  if (ret < 0)
    return ret;

#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
  ret = uart2test_pinmux_one(UART2_TEST_RTS_GPIO, UART2_TEST_FLOW_FUNC,
                             UART2_TEST_PULL_DIS);
  if (ret < 0)
    return ret;

  ret = uart2test_pinmux_one(UART2_TEST_CTS_GPIO, UART2_TEST_FLOW_FUNC,
                             UART2_TEST_PULL_UP);
  if (ret < 0)
    return ret;

  printf("uart2test: pinmux TX=%s RX=%s func=%d RTS=%s CTS=%s func=%d\n",
         UART2_TEST_TX_GPIO, UART2_TEST_RX_GPIO, UART2_TEST_TXRX_FUNC,
         UART2_TEST_RTS_GPIO, UART2_TEST_CTS_GPIO, UART2_TEST_FLOW_FUNC);
#else
  printf("uart2test: pinmux TX=%s RX=%s func=%d, RTS/CTS disabled\n",
         UART2_TEST_TX_GPIO, UART2_TEST_RX_GPIO, UART2_TEST_TXRX_FUNC);
#endif
  return 0;
}

static int uart2test_configure(int fd)
{
  struct termios tio;

  if (tcgetattr(fd, &tio) < 0)
    {
      printf("uart2test: tcgetattr failed errno=%d\n", errno);
      return -errno;
    }

  cfsetispeed(&tio, B115200);
  cfsetospeed(&tio, B115200);
  tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
  tio.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
  tio.c_cflag |= CRTSCTS;
#endif
#endif
  tio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_oflag &= ~OPOST;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      printf("uart2test: tcsetattr failed errno=%d\n", errno);
      return -errno;
    }

  tcflush(fd, TCIOFLUSH);
  return 0;
}

static bool uart2test_is_number(const char *str)
{
  if (!str || !*str)
    return false;

  while (*str)
    {
      if (!isdigit((unsigned char)*str++))
        return false;
    }

  return true;
}

static void uart2test_build_text(char *dst, size_t dstlen, int argc,
                                 char *argv[], int start)
{
  size_t used = 0;
  int i;

  if (start >= argc)
    {
      snprintf(dst, dstlen, "UART2TEST");
      return;
    }

  for (i = start; i < argc && used + 1 < dstlen; i++)
    {
      int ret = snprintf(dst + used, dstlen - used, "%s%s",
                         i == start ? "" : " ", argv[i]);
      if (ret < 0)
        break;
      if ((size_t)ret >= dstlen - used)
        {
          used = dstlen - 1;
          break;
        }
      used += (size_t)ret;
    }
}

static void uart2test_dump_rx(const unsigned char *buf, ssize_t len)
{
  ssize_t i;

  printf("uart2test: RX len=%d hex:", (int)len);
  for (i = 0; i < len; i++)
    printf(" %02x", buf[i]);

  printf(" ascii=\"");
  for (i = 0; i < len; i++)
    putchar(isprint(buf[i]) ? buf[i] : '.');
  printf("\"\n");
}

static void uart2test_dump_stats(const char *tag)
{
  uart2test_dump_regs(tag);
}

static int uart2test_open(void)
{
  int fd = open(UART2_TEST_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd < 0)
    {
      printf("uart2test: open %s failed errno=%d\n", UART2_TEST_DEV, errno);
      return -errno;
    }

  if (uart2test_configure(fd) < 0)
    {
      close(fd);
      return -EIO;
    }

  return fd;
}

static void uart2test_usage(void)
{
  printf("Usage: uart2test [seconds] [text]\n");
  printf("       uart2test raw [seconds]\n");
  printf("Default: send UART2TEST once per second for 8 seconds at 115200 8N1.\n");
  printf("Raw mode polls UART2 RBR/LSR/RFL directly and bypasses serial read().\n");
}

static void uart2test_raw_putc(int ch)
{
  int guard = 100000;

  while ((uart2test_getreg(UART_LSR_OFF) & UART_LSR_THRE) == 0 && guard-- > 0)
    {
    }

  uart2test_putreg((uint32_t)ch, UART_THR_OFF);
}

static void uart2test_raw_puts(const char *str)
{
  while (*str)
    {
      uart2test_raw_putc((unsigned char)*str++);
    }
}

static ssize_t uart2test_raw_drain(unsigned char *buf, size_t buflen)
{
  ssize_t len = 0;
  int guard = 256;

  while (len < (ssize_t)buflen && guard-- > 0)
    {
      uint32_t lsr = uart2test_getreg(UART_LSR_OFF);
      uint32_t rfl = uart2test_getreg(UART_RFL_OFF);

      if ((lsr & UART_LSR_DR) == 0 && rfl == 0)
        {
          break;
        }

      buf[len++] = (unsigned char)(uart2test_getreg(UART_RBR_OFF) & 0xff);
    }

  return len;
}

static int uart2test_parse_seconds(int argc, char *argv[], int index,
                                   int dflt)
{
  int seconds = dflt;

  if (argc > index && uart2test_is_number(argv[index]))
    {
      seconds = atoi(argv[index]);
    }

  if (seconds <= 0)
    seconds = 1;
  if (seconds > 120)
    seconds = 120;

  return seconds;
}

static int uart2test_raw_main(int argc, char *argv[])
{
  unsigned char rx[UART2_TEST_MAX_RX];
  int seconds;
  int ticks;
  int fd;
  int i;

  seconds = uart2test_parse_seconds(argc, argv, 2, 20);

  if (uart2test_pinmux() < 0)
    return 1;

  fd = uart2test_open();
  if (fd < 0)
    return 1;

  uart2test_putreg(0, UART_IER_OFF);
  (void)uart2test_getreg(UART_IER_OFF);

  printf("uart2test: raw mode %s %d baud, duration=%d sec\n",
         UART2_TEST_DEV, UART2_TEST_BAUD, seconds);
  printf("uart2test: raw mode disables UART2 interrupts and reads FIFO directly\n");
  uart2test_dump_stats("raw-start");

  ticks = seconds * UART_RAW_TICKS_PER_SEC;
  for (i = 0; i < ticks; i++)
    {
      ssize_t ret;

      if ((i % UART_RAW_TICKS_PER_SEC) == 0)
        {
          uart2test_raw_puts("UART2RAW\r\n");
          uart2test_dump_regs("raw-tick");
        }

      ret = uart2test_raw_drain(rx, sizeof(rx));
      if (ret > 0)
        {
          uart2test_dump_rx(rx, ret);
        }

      usleep(UART_RAW_INTERVAL_US);
    }

  while ((uart2test_getreg(UART_LSR_OFF) & UART_LSR_TEMT) == 0)
    {
      usleep(UART_RAW_INTERVAL_US);
    }

  uart2test_dump_stats("raw-done");
  close(fd);
  printf("uart2test: raw done\n");
  return 0;
}

int uart2test_main(int argc, char *argv[])
{
  unsigned char rx[UART2_TEST_MAX_RX];
  char text[UART2_TEST_MAX_TEXT];
  char tx[UART2_TEST_MAX_TEXT + 32];
  int text_arg = 1;
  int seconds = 8;
  int fd;
  int i;

  if (argc > 1 && strcmp(argv[1], "-h") == 0)
    {
      uart2test_usage();
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "raw") == 0)
    {
      return uart2test_raw_main(argc, argv);
    }

  if (argc > 1 && uart2test_is_number(argv[1]))
    {
      seconds = uart2test_parse_seconds(argc, argv, 1, seconds);
      text_arg = 2;
    }

  uart2test_build_text(text, sizeof(text), argc, argv, text_arg);

  if (uart2test_pinmux() < 0)
    return 1;

  fd = uart2test_open();
  if (fd < 0)
    return 1;

  printf("uart2test: %s %d baud, duration=%d sec, text=\"%s\"\n",
         UART2_TEST_DEV, UART2_TEST_BAUD, seconds, text);
  uart2test_dump_stats("start");

  for (i = 0; i < seconds; i++)
    {
      struct pollfd pfd;
      ssize_t ret;
      int len;
      int elapsed_ms = 0;

      len = snprintf(tx, sizeof(tx), "%s #%d\r\n", text, i + 1);
      ret = write(fd, tx, (size_t)len);
      printf("uart2test: TX len=%d ret=%d\n", len, (int)ret);

      while (elapsed_ms < 1000)
        {
          pfd.fd = fd;
          pfd.events = POLLIN;
          pfd.revents = 0;
          ret = poll(&pfd, 1, 100);
          elapsed_ms += 100;

          if (ret > 0 && (pfd.revents & POLLIN))
            {
              ret = read(fd, rx, sizeof(rx));
              if (ret > 0)
                {
                  ssize_t echo;

                  uart2test_dump_rx(rx, ret);
                  echo = write(fd, rx, (size_t)ret);
                  printf("uart2test: echo ret=%d\n", (int)echo);
                }
            }
          else if (ret < 0)
            {
              printf("uart2test: poll failed errno=%d\n", errno);
              break;
            }
          else if (ret > 0)
            {
              printf("uart2test: poll revents=0x%lx\n",
                     (unsigned long)pfd.revents);
            }
        }

      uart2test_dump_stats("tick");
    }

  close(fd);
  uart2test_dump_stats("done");
  printf("uart2test: done\n");
  return 0;
}
