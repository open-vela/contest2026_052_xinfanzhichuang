#ifndef __BT_OS_H__
#define __BT_OS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bt_type.h"
#include "bt_api.h"
#include "string.h"

#if RTOS
#include "log.h"
#include "kapi.h"
#include "apps.h"
#include "tui.h"
#include "mod_uart.h"
#include "app_root.h"
#include "phonelink_wireless.h"
#endif

#if RTT
#include "rtthread.h"
#endif

#if NUTTX
#include <nuttx/config.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#endif

#if RTOS
extern __s32 rtos_com_uart_init(void);
extern __s32 rtos_com_uart_deinit(void);
extern __s32 rtos_com_uart_write(char* pbuf, __s32 size);
extern __s32 rtos_com_uart_read(char* pbuf, __s32 buf_size, __s32* size);
extern __s32 rtos_com_uart_flush(void);
extern __s32 rtos_bt_task_start(void);
extern __s32 rtos_bt_task_stop(void);
extern void  rtos_msleep(uint16_t ticks);
#endif

#if RTT
#define __msg		pr_info
#define __inf		pr_info
#define __wrn		pr_warn
#define __err		pr_err
#define __log		pr_debug

extern __s32 rtt_com_uart_init(void);
extern __s32 rtt_com_uart_deinit(void);
extern __s32 rtt_com_uart_write(char* pbuf, __s32 size);
extern __s32 rtt_com_uart_read(char* pbuf, __s32 buf_size, __s32* size);
extern __s32 rtt_com_uart_flush(void);
extern __s32 rtt_bt_task_start(void);
extern __s32 rtt_bt_task_stop(void);
extern void  rtt_msleep(uint16_t ticks);
#endif

#if NUTTX
#define __msg		printf
#define __inf		printf
#define __wrn		printf
#define __err		printf
#define __log		printf

extern __s32 nuttx_com_uart_init(void);
extern __s32 nuttx_com_uart_deinit(void);
extern __s32 nuttx_com_uart_write(char* pbuf, __s32 size);
extern __s32 nuttx_com_uart_read(char* pbuf, __s32 buf_size, __s32* size);
extern __s32 nuttx_com_uart_flush(void);
extern __s32 nuttx_bt_task_start(void);
extern __s32 nuttx_bt_task_stop(void);
extern void  nuttx_msleep(uint16_t ms);
#endif

#if LINUX
extern __s32 linux_com_uart_init(void);
extern __s32 linux_com_uart_deinit(void);
extern __s32 linux_com_uart_write(char* pbuf, __s32 size);
extern __s32 linux_com_uart_read(char* pbuf, __s32 buf_size, __s32* size);
extern __s32 linux_com_uart_flush(void);
extern __s32 linux_bt_task_start(void);
extern __s32 linux_bt_task_stop(void);
extern void linux_msleep(uint16_t ticks);
#endif

extern __s32 com_uart_init(void);
extern __s32 com_uart_deinit(void);
extern __s32 com_uart_write(char* pbuf, __s32 size);
extern __s32 com_uart_read(char* pbuf, __s32 buf_size, __s32* size);
extern __s32 com_uart_flush(void);
extern __s32 com_uart_state(void);
extern void	sys_bt_receive_cmd(void *parg);
extern void	sys_bt_decode_cmd(void*parg);

extern __s32 bt_para_init(void);
extern __s32 bt_para_exit(void);
extern __s32 bt_task_start(void);
extern __s32 bt_task_stop(void);
extern void bt_msleep(uint16_t ticks);

#endif
#ifdef __cplusplus
}
#endif
