/*
 * WiFi link stubs for NuttX/Vela.
 *
 * Keep this file limited to platform compatibility symbols. WPA supplicant
 * symbols must come from wpas/ so station connect can run the real state
 * machine instead of a dummy implementation.
 */

#include <nuttx/config.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

/* ===== ulog stubs (RT-Thread logging) ===== */

void ulog_output(int level, const char *tag, int newline, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)newline;
    (void)fmt;
    /* Silently drop - RT-Thread ulog not available on NuttX */
}

void ulog_flush(void)
{
    /* No-op */
}

/* ===== lwIP byte-order stubs ===== */

uint32_t lwip_htonl(uint32_t n)
{
    return htonl(n);
}

uint16_t lwip_htons(uint16_t n)
{
    return htons(n);
}

int closesocket(int s)
{
    return close(s);
}

/* ===== WiFi P2P stubs ===== */

void wifi_p2p_go_started(void *dev)
{
    (void)dev;
}

void wifi_p2p_go_stopped(void *dev)
{
    (void)dev;
}

/* ===== RT-Thread WiFi framework stubs ===== */

struct rt_wlan_device;
typedef int rt_err_t;

int aic_wifi_netdev_receive_frame(const uint8_t *data, unsigned int len);

rt_err_t rt_wlan_dev_report_data(struct rt_wlan_device *dev, void *buff, int len)
{
    (void)dev;

    if (buff == NULL || len <= 0) {
        return -1;
    }

    return aic_wifi_netdev_receive_frame((const uint8_t *)buff,
                                         (unsigned int)len);
}


/* ===== Safe string wrappers for prebuilt library ===== */
/* The prebuilt libwlan_aic8800_e907f.a symbols have been renamed via objcopy
 * (strlen->aic8800_strlen, sprintf->aic8800_sprintf, etc.) to avoid conflicts
 * with libc. Provide NULL-safe implementations here. */

#include <stdarg.h>

size_t aic8800_strlen(const char *s)
{
    if (!s) return 0;
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int aic8800_sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int ret;
    if (!fmt) { if (str) str[0] = '\0'; return 0; }
    va_start(ap, fmt);
    ret = vsnprintf(str, (size_t)65535, fmt, ap);
    va_end(ap);
    return ret;
}

int aic8800_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;
    if (!fmt) { if (str && size > 0) str[0] = '\0'; return 0; }
    va_start(ap, fmt);
    ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int aic8800_vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    if (!fmt) { if (str && size > 0) str[0] = '\0'; return 0; }
    return vsnprintf(str, size, fmt, ap);
}
