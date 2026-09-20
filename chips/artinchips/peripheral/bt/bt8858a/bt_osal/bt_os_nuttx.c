/*
 * BT OS Adaptation Layer for NuttX/Vela
 * Adapted from bt_os_rtt.c (RT-Thread) for NuttX RTOS
 */

#include "bt_config.h"
#include "bt_api.h"
#include "bt_os.h"
#include "bt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NUTTX

#include <nuttx/config.h>
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <termios.h>
#include <poll.h>
#include <stdlib.h>

/* Keep UART I/O in kernel task group. NuttX closes per-task-group file
 * descriptors when nsh commands exit, so command tasks must not own ttyS2. */
static int g_bt_uart_fd = -1;
static pthread_mutex_t g_bt_uart_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef CONFIG_AIC_BT_UART_PORT
#define BT_UART_DEV     CONFIG_AIC_BT_UART_PORT
#else
#define BT_UART_DEV     "/dev/ttyS2"
#endif

static speed_t bt_uart_speed(void)
{
#if defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 115200
    return B115200;
#elif defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 921600 && defined(B921600)
    return B921600;
#elif defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 1500000 && defined(B1500000)
    return B1500000;
#else
    return B115200;
#endif
}

static int bt_uart_baud_value(void)
{
#ifdef CONFIG_AIC_BT_UART_BAUD
    return CONFIG_AIC_BT_UART_BAUD;
#else
    return 115200;
#endif
}

#ifdef CONFIG_AIC_BT_UART_DUMP
static void bt_uart_dump(const char *tag, const char *buf, ssize_t len)
{
    ssize_t i;
    ssize_t dump_len = len > 64 ? 64 : len;

    printf("BT UART %s len=%d:", tag, (int)len);
    for (i = 0; i < dump_len; i++)
        printf(" %02x", (unsigned char)buf[i]);
    if (dump_len < len)
        printf(" ...");
    printf("\n");
}
#else
#define bt_uart_dump(t, b, l) do { } while (0)
#endif

static void bt_uart_log_error(const char *op, int err)
{
    if (err != EAGAIN && err != EINTR)
        printf("BT UART %s failed errno=%d\n", op, err);
}

static __s32 bt_uart_configure(int fd, bool flush)
{
    struct termios tio;

    if (tcgetattr(fd, &tio) < 0) {
        printf("BT: tcgetattr failed: %d\n", errno);
        return EPDK_FAIL;
    }

    cfsetispeed(&tio, bt_uart_speed());
    cfsetospeed(&tio, bt_uart_speed());

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

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        printf("BT: tcsetattr failed: %d\n", errno);
        return EPDK_FAIL;
    }

    if (flush)
        tcflush(fd, TCIOFLUSH);

    return EPDK_OK;
}

static int bt_uart_open_one(bool flush)
{
    int fd;

    fd = open(BT_UART_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("BT: open %s failed: %d\n", BT_UART_DEV, errno);
        return -1;
    }

    if (bt_uart_configure(fd, flush) != EPDK_OK) {
        close(fd);
        return -1;
    }

    bt_msleep(10);
    printf("BT: UART open success on %s baud %d%s fd=%d\n",
           BT_UART_DEV, bt_uart_baud_value(),
#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
           " flowctrl",
#else
           "",
#endif
           fd);

    return fd;
}

static void bt_uart_close_locked(void)
{
    if (g_bt_uart_fd >= 0) {
        close(g_bt_uart_fd);
        g_bt_uart_fd = -1;
    }
}

static __s32 bt_uart_open_locked(void)
{
    if (g_bt_uart_fd >= 0) {
        if (fcntl(g_bt_uart_fd, F_GETFL) >= 0)
            return EPDK_OK;

        if (errno == EBADF) {
            printf("BT: UART fd %d is stale, reopen %s\n",
                   g_bt_uart_fd, BT_UART_DEV);
            g_bt_uart_fd = -1;
        } else {
            bt_uart_log_error("fcntl", errno);
            return EPDK_FAIL;
        }
    }

    g_bt_uart_fd = bt_uart_open_one(true);
    if (g_bt_uart_fd < 0)
        return EPDK_FAIL;

    return EPDK_OK;
}

__s32 nuttx_com_uart_init(void)
{
    int fd;

    pthread_mutex_lock(&g_bt_uart_lock);
    fd = bt_uart_open_one(true);
    if (fd >= 0)
        close(fd);
    pthread_mutex_unlock(&g_bt_uart_lock);

    return fd >= 0 ? EPDK_OK : EPDK_FAIL;
}

__s32 nuttx_com_uart_deinit(void)
{
    pthread_mutex_lock(&g_bt_uart_lock);
    bt_uart_close_locked();
    pthread_mutex_unlock(&g_bt_uart_lock);

    bt_msleep(10);
    return EPDK_OK;
}

static __s32 bt_uart_reopen_after_ebadf_locked(const char *op)
{
    printf("BT: UART %s hit EBADF, reopening %s\n", op, BT_UART_DEV);
    g_bt_uart_fd = -1;
    return bt_uart_open_locked();
}

static __s32 bt_uart_write_locked(char *pbuf, __s32 size)
{
    char *orig = pbuf;
    __s32 orig_size = size;

    if (pbuf == NULL || size <= 0)
        return EPDK_FAIL;

    if (bt_uart_open_locked() != EPDK_OK)
        return EPDK_FAIL;

    bt_uart_dump("TX", orig, orig_size);

    while (size > 0) {
        ssize_t ret = write(g_bt_uart_fd, pbuf, (size_t)size);
        if (ret > 0) {
            size -= ret;
            pbuf += ret;
        } else if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd pfd;
            pfd.fd = g_bt_uart_fd;
            pfd.events = POLLOUT;
            poll(&pfd, 1, 20);
        } else if (ret == 0) {
            bt_msleep(1);
        } else if (ret < 0 && errno == EBADF) {
            if (bt_uart_reopen_after_ebadf_locked("write") != EPDK_OK)
                return EPDK_FAIL;
        } else if (ret < 0) {
            bt_uart_log_error("write", errno);
            return EPDK_FAIL;
        }
    }

    return EPDK_OK;
}

static __s32 bt_uart_write_direct(char *pbuf, __s32 size)
{
    __s32 ret;

    pthread_mutex_lock(&g_bt_uart_lock);
    ret = bt_uart_write_locked(pbuf, size);
    pthread_mutex_unlock(&g_bt_uart_lock);

    return ret;
}

struct bt_uart_tx_job
{
    char *buf;
    __s32 size;
    sem_t done;
    __s32 ret;
};

static int bt_uart_tx_thread_entry(int argc, char *argv[])
{
    const char *arg = NULL;
    struct bt_uart_tx_job *job;

    if (argc > 1)
        arg = argv[1];
    else if (argc > 0)
        arg = argv[0];

    job = (struct bt_uart_tx_job *)(uintptr_t)strtoul(arg ? arg : "0",
                                                      NULL, 0);
    if (job != NULL) {
        job->ret = bt_uart_write_direct(job->buf, job->size);
        nxsem_post(&job->done);
    }

    return 0;
}

__s32 nuttx_com_uart_write(char *pbuf, __s32 size)
{
    struct bt_uart_tx_job job;
    char argbuf[24];
    char *argv[2];
    int pid;
    int ret;

    if (pbuf == NULL || size <= 0)
        return EPDK_FAIL;

    job.buf = pbuf;
    job.size = size;
    job.ret = EPDK_FAIL;
    nxsem_init(&job.done, 0, 0);

    snprintf(argbuf, sizeof(argbuf), "%p", &job);
    argv[0] = argbuf;
    argv[1] = NULL;

    pid = kthread_create("bt_uart_tx", 102, 2048,
                         bt_uart_tx_thread_entry, argv);
    if (pid < 0) {
        nxsem_destroy(&job.done);
        printf("BT: create tx thread failed: %d\n", pid);
        return EPDK_FAIL;
    }

    do {
        ret = nxsem_wait(&job.done);
    } while (ret < 0 && errno == EINTR);

    nxsem_destroy(&job.done);
    if (ret < 0)
        return EPDK_FAIL;

    return job.ret;
}

static void bt_uart_probe_dump(const char *tag, const char *buf, ssize_t len)
{
    ssize_t i;
    ssize_t dump_len = len > 96 ? 96 : len;

    printf("BT probe %s len=%d hex:", tag, (int)len);
    for (i = 0; i < dump_len; i++)
        printf(" %02x", (unsigned char)buf[i]);
    if (dump_len < len)
        printf(" ...");

    printf(" ascii=\"");
    for (i = 0; i < dump_len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c >= 0x20 && c <= 0x7e)
            putchar(c);
        else
            putchar('.');
    }
    if (dump_len < len)
        printf("...");
    printf("\"\n");
}

int nuttx_bt_uart_probe(const char *body, unsigned int wait_ms)
{
    char cmd[160];
    char rx[128];
    size_t body_len;
    size_t len;
    unsigned int elapsed = 0;
    int got = 0;
    int ret = EPDK_FAIL;

    if (body == NULL || body[0] == '\0')
        body = "MY";

    body_len = strcspn(body, "\r\n");
    if (body_len > 120) {
        printf("BT probe: command too long\n");
        return -EINVAL;
    }

    if ((body[0] == 'A' || body[0] == 'a') &&
        (body[1] == 'T' || body[1] == 't') &&
        body[2] == '#') {
        len = snprintf(cmd, sizeof(cmd), "%.*s\r\n", (int)body_len, body);
    } else {
        len = snprintf(cmd, sizeof(cmd), "AT#%.*s\r\n", (int)body_len, body);
    }

    if (len >= sizeof(cmd)) {
        printf("BT probe: command buffer overflow\n");
        return -EINVAL;
    }

    if (wait_ms == 0)
        wait_ms = 1000;
    if (wait_ms > 5000)
        wait_ms = 5000;

    pthread_mutex_lock(&g_bt_uart_lock);

    if (bt_uart_open_locked() != EPDK_OK)
        goto out;

    tcflush(g_bt_uart_fd, TCIOFLUSH);
    printf("BT probe TX: %.*s", (int)len, cmd);
    if (bt_uart_write_locked(cmd, (__s32)len) != EPDK_OK)
        goto out;

    while (elapsed < wait_ms) {
        struct pollfd pfd;
        ssize_t nread;
        int pr;

        pfd.fd = g_bt_uart_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        pr = poll(&pfd, 1, 50);
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr < 0) {
            bt_uart_log_error("probe poll", errno);
            goto out;
        }

        elapsed += 50;
        if (pr == 0)
            continue;

        memset(rx, 0, sizeof(rx));
        nread = read(g_bt_uart_fd, rx, sizeof(rx));
        if (nread < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (nread < 0) {
            bt_uart_log_error("probe read", errno);
            goto out;
        }
        if (nread > 0) {
            got += nread;
            bt_uart_probe_dump("RX", rx, nread);
        }
    }

    printf("BT probe: total_rx=%d wait_ms=%u\n", got, wait_ms);
    ret = got > 0 ? EPDK_OK : -ETIMEDOUT;

out:
    pthread_mutex_unlock(&g_bt_uart_lock);
    return ret;
}

int nuttx_bt_uart_hci_reset_probe(unsigned int wait_ms)
{
    static const unsigned char hci_reset[] = {0x01, 0x03, 0x0c, 0x00};
    char rx[128];
    unsigned int elapsed = 0;
    int got = 0;
    int ret = EPDK_FAIL;

    if (wait_ms == 0)
        wait_ms = 1000;
    if (wait_ms > 5000)
        wait_ms = 5000;

    pthread_mutex_lock(&g_bt_uart_lock);

    if (bt_uart_open_locked() != EPDK_OK)
        goto out;

    tcflush(g_bt_uart_fd, TCIOFLUSH);
    printf("BT HCI reset probe TX: 01 03 0c 00\n");
    if (bt_uart_write_locked((char *)hci_reset, sizeof(hci_reset)) != EPDK_OK)
        goto out;

    while (elapsed < wait_ms) {
        struct pollfd pfd;
        ssize_t nread;
        int pr;

        pfd.fd = g_bt_uart_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        pr = poll(&pfd, 1, 50);
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr < 0) {
            bt_uart_log_error("hci probe poll", errno);
            goto out;
        }

        elapsed += 50;
        if (pr == 0)
            continue;

        memset(rx, 0, sizeof(rx));
        nread = read(g_bt_uart_fd, rx, sizeof(rx));
        if (nread < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (nread < 0) {
            bt_uart_log_error("hci probe read", errno);
            goto out;
        }
        if (nread > 0) {
            got += nread;
            bt_uart_probe_dump("HCI RX", rx, nread);
        }
    }

    printf("BT HCI reset probe: total_rx=%d wait_ms=%u\n", got, wait_ms);
    ret = got > 0 ? EPDK_OK : -ETIMEDOUT;

out:
    pthread_mutex_unlock(&g_bt_uart_lock);
    return ret;
}

__s32 nuttx_com_uart_read(char *pbuf, __s32 buf_size, __s32 *size)
{
    struct pollfd pfd;
    int pr;
    ssize_t ret;

    if (pbuf == NULL || buf_size <= 0 || size == NULL)
        return EPDK_FAIL;

    memset(pbuf, 0, buf_size);
    *size = 0;

    pthread_mutex_lock(&g_bt_uart_lock);

    if (bt_uart_open_locked() != EPDK_OK) {
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_FAIL;
    }

    pfd.fd = g_bt_uart_fd;
    pfd.events = POLLIN;

    pr = poll(&pfd, 1, 20);
    if (pr < 0 && errno == EBADF) {
        if (bt_uart_reopen_after_ebadf_locked("poll") != EPDK_OK) {
            pthread_mutex_unlock(&g_bt_uart_lock);
            return EPDK_FAIL;
        }
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_OK;
    }

    if (pr < 0 && errno != EINTR) {
        bt_uart_log_error("poll", errno);
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_FAIL;
    }

    if (pr <= 0) {
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_OK;
    }

    ret = read(g_bt_uart_fd, pbuf, (size_t)buf_size);
    if (ret < 0 && errno == EBADF) {
        if (bt_uart_reopen_after_ebadf_locked("read") != EPDK_OK) {
            pthread_mutex_unlock(&g_bt_uart_lock);
            return EPDK_FAIL;
        }
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_OK;
    }

    if (ret < 0 && errno != EAGAIN && errno != EINTR) {
        bt_uart_log_error("read", errno);
        pthread_mutex_unlock(&g_bt_uart_lock);
        return EPDK_FAIL;
    }

    if (ret > 0) {
        bt_uart_dump("RX", pbuf, ret);
        *size = (__s32)ret;
    }

    pthread_mutex_unlock(&g_bt_uart_lock);
    return EPDK_OK;
}

__s32 nuttx_com_uart_flush(void)
{
    int fd = -1;
    __s32 ret = EPDK_OK;

    pthread_mutex_lock(&g_bt_uart_lock);

    if (g_bt_uart_fd >= 0 && fcntl(g_bt_uart_fd, F_GETFL) >= 0) {
        tcflush(g_bt_uart_fd, TCIOFLUSH);
    } else {
        if (g_bt_uart_fd >= 0)
            g_bt_uart_fd = -1;

        fd = bt_uart_open_one(true);
        if (fd >= 0) {
            tcflush(fd, TCIOFLUSH);
            close(fd);
        } else {
            ret = EPDK_FAIL;
        }
    }

    pthread_mutex_unlock(&g_bt_uart_lock);

    bt_msleep(5);
    return ret;
}

static pid_t bt_decode_thread;
static pid_t bt_receive_thread;
static bool g_bt_tasks_running;
static bool g_bt_tasks_started;

static int bt_decode_thread_entry(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    while (g_bt_tasks_running)
        sys_bt_decode_cmd(NULL);

    return 0;
}

static int bt_receive_thread_entry(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    while (g_bt_tasks_running)
        sys_bt_receive_cmd(NULL);

    pthread_mutex_lock(&g_bt_uart_lock);
    bt_uart_close_locked();
    pthread_mutex_unlock(&g_bt_uart_lock);
    return 0;
}

__s32 nuttx_bt_task_start(void)
{
    int ret;

    printf("BT: starting tasks\n");

    if (g_bt_tasks_started)
        return EPDK_OK;

    g_bt_tasks_running = true;

    ret = kthread_create("bt_decode", 100, 4096,
                         bt_decode_thread_entry, NULL);
    if (ret < 0) {
        printf("BT: create decode thread failed: %d\n", ret);
        g_bt_tasks_running = false;
        return EPDK_FAIL;
    }
    bt_decode_thread = (pid_t)ret;

    ret = kthread_create("bt_recv", 101, 4096,
                         bt_receive_thread_entry, NULL);
    if (ret < 0) {
        printf("BT: create receive thread failed: %d\n", ret);
        g_bt_tasks_running = false;
        kthread_delete(bt_decode_thread);
        bt_decode_thread = 0;
        return EPDK_FAIL;
    }
    bt_receive_thread = (pid_t)ret;

    g_bt_tasks_started = true;
    printf("BT: tasks started decode=%d recv=%d\n",
           bt_decode_thread, bt_receive_thread);
    return EPDK_OK;
}

__s32 nuttx_bt_task_stop(void)
{
    if (!g_bt_tasks_started)
        return EPDK_OK;

    g_bt_tasks_running = false;
    usleep(50000);

    if (bt_receive_thread) {
        kthread_delete(bt_receive_thread);
        bt_receive_thread = 0;
    }
    if (bt_decode_thread) {
        kthread_delete(bt_decode_thread);
        bt_decode_thread = 0;
    }
    g_bt_tasks_started = false;
    return EPDK_OK;
}

void nuttx_msleep(uint16_t ms)
{
    usleep((useconds_t)ms * 1000);
}

#endif /* NUTTX */

#ifdef __cplusplus
}
#endif
