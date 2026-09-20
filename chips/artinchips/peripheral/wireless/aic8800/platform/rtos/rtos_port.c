/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rtos_port.h"
#include "dbg_assert.h"
#include "aic_plat_log.h"
#include "aic_plat_time.h"

#include <nuttx/config.h>
#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <nuttx/mutex.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/wdog.h>
#include <nuttx/signal.h>
#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>
#include <mqueue.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>

#if defined(CONFIG_PLAT_NUTTX)
/* Wrapper for syslog - NuttX syslog requires priority as first argument */
static void aic_syslog_wrapper(const char *fmt, ...)
{
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vsyslog(LOG_INFO, fmt, ap);
    va_end(ap);
}
#define AIC_PLAT_PRINTF_API         aic_syslog_wrapper
#endif

#define RTOS_AL_INFO_DUMP   0
#define RTOS_INFO_SHORT     0
#define RTOS_TASK_DELETE_WAIT_MS 500
#define RTOS_TASK_DELETE_POLL_MS 5

static uint32_t rtos_mem_cnt = 0;
static uint32_t rtos_free_cnt = 0;
static uint32_t rtos_remain_cnt = 0;
struct co_list res_list[RES_TYPE_MAX] = {0};

#define RTOS_CRITICAL_NEST_MAX 8
static irqstate_t g_critical_flags[RTOS_CRITICAL_NEST_MAX];
static int g_critical_nest;

#ifdef CONFIG_RX_NOCOPY
#define AIC_RTOS_MEM_MAX        ((1024)*1024)
#else
#define AIC_RTOS_MEM_MAX        ((1024+256)*1024)
#endif
const osal_porting_ops_t osal_porting_ops = {
    .osal_dbg_printf    = AIC_PLAT_PRINTF_API,
};

/*
 * Internal: per-task notification support
 ****************************************************************************************
 */
struct task_notify_ctx {
    rtos_task_handle task;
    sem_t sem;
};

/* NuttX PIDs are not dense enough to use directly as small array indexes. */
#define MAX_TASK_NOTIFY_SLOTS  32
static struct task_notify_ctx *g_notify_table[MAX_TASK_NOTIFY_SLOTS];
static mutex_t g_notify_lock = NXMUTEX_INITIALIZER;

/*
 * Internal: queue wrapper for tracking msg_size and maxmsg
 ****************************************************************************************
 */
struct aic_mq_wrapper {
    uint32_t magic;
    int      msg_size;
    int      maxmsg;
    int      head;
    int      tail;
    int      count;
    uint8_t *storage;
    sem_t    items;
    sem_t    slots;
    sem_t    lock;
    char     name[24];
    char     user_name[32];
};
static int g_mq_counter = 0;

#define AIC_MQ_MAGIC 0x41494351

static void rtos_queue_dump_error(const char *op,
                                  const struct aic_mq_wrapper *w,
                                  int timeout,
                                  int err)
{
    if (w == NULL || w->magic != AIC_MQ_MAGIC) {
        AIC_LOG_PRINTF("[rtos]queue %s failed: null queue timeout=%d errno=%d\n",
                       op, timeout, err);
        return;
    }

    AIC_LOG_PRINTF("[rtos]queue %s failed: name=%s alias=%s "
                   "msg_size=%d maxmsg=%d cur=%d timeout=%d errno=%d\n",
                   op, w->user_name, w->name, w->msg_size,
                   w->maxmsg, w->count, timeout, err);
}

static int rtos_queue_wait_sem(sem_t *sem, int timeout)
{
    int ret;

    if (timeout < 0) {
        do {
            ret = nxsem_wait(sem);
        } while (ret < 0 && (ret == -EINTR || get_errno() == EINTR));
    } else if (timeout == 0) {
        ret = nxsem_trywait(sem);
    } else {
        ret = nxsem_tickwait(sem, MSEC2TICK(timeout));
    }

    if (ret < 0 && (ret == -ETIMEDOUT || ret == -EAGAIN || ret == -EINTR))
        set_errno(-ret);

    return ret;
}

static bool rtos_task_pid_alive(rtos_task_handle task)
{
    FAR struct tcb_s *tcb;

    if (task <= 0) {
        return false;
    }

    tcb = nxsched_get_tcb((pid_t)task);
    if (tcb == NULL) {
        return false;
    }

    nxsched_put_tcb(tcb);
    return true;
}

/*
 * Internal: watchdog timer callback
 ****************************************************************************************
 */
static void aic_wdog_handler(wdparm_t arg)
{
    struct aic_wdog_wrapper *tw = (struct aic_wdog_wrapper *)(uintptr_t)arg;
    if (tw && tw->func) {
        tw->func(tw->arg);
    }
    /* Re-arm for periodic timers */
    if (tw && tw->periodic && tw->period > 0) {
        wd_start(&tw->wdog, tw->period, aic_wdog_handler, (wdparm_t)(uintptr_t)tw);
    }
}

/*
 * Internal: task entry wrapper
 ****************************************************************************************
 */
struct task_wrap {
    rtos_task_fct func;
    void *arg;
    int slot;
};

#define MAX_TASK_START_SLOTS 16
static struct task_wrap *g_task_start_slots[MAX_TASK_START_SLOTS];
static mutex_t g_task_start_lock = NXMUTEX_INITIALIZER;

static struct task_notify_ctx *rtos_task_find_notify_locked(rtos_task_handle task)
{
    int i;

    for (i = 0; i < MAX_TASK_NOTIFY_SLOTS; i++) {
        if (g_notify_table[i] && g_notify_table[i]->task == task) {
            return g_notify_table[i];
        }
    }

    return NULL;
}

static void rtos_task_remove_notification(rtos_task_handle task)
{
    int i;

    nxmutex_lock(&g_notify_lock);
    for (i = 0; i < MAX_TASK_NOTIFY_SLOTS; i++) {
        struct task_notify_ctx *ctx = g_notify_table[i];

        if (ctx && ctx->task == task) {
            g_notify_table[i] = NULL;
            nxsem_destroy(&ctx->sem);
            kmm_free(ctx);
            break;
        }
    }
    nxmutex_unlock(&g_notify_lock);
}

struct rtos_task_delete_work {
    struct work_s work;
    sem_t done;
    rtos_task_handle task;
    int ret;
};

static void rtos_task_delete_worker(void *arg)
{
    struct rtos_task_delete_work *del = arg;

    del->ret = kthread_delete(del->task);
    nxsem_post(&del->done);
}

static int rtos_task_delete_from_kernel(rtos_task_handle task)
{
    struct rtos_task_delete_work del;
    int ret;

    memset(&del, 0, sizeof(del));
    del.task = task;
    nxsem_init(&del.done, 0, 0);

    ret = work_queue(HPWORK, &del.work, rtos_task_delete_worker, &del, 0);
    if (ret < 0) {
        nxsem_destroy(&del.done);
        return ret;
    }

    do {
        ret = nxsem_wait(&del.done);
    } while (ret < 0 && (ret == -EINTR || get_errno() == EINTR));

    nxsem_destroy(&del.done);
    return del.ret;
}

static int task_entry(int argc, char *argv[])
{
    struct task_wrap *wrap = NULL;
    rtos_task_fct func = NULL;
    void *params = NULL;
    int slot = -1;

    if (argc > 1 && argv[1]) {
        slot = atoi(argv[1]);
    } else if (argc > 0 && argv[0]) {
        slot = atoi(argv[0]);
    }

    nxmutex_lock(&g_task_start_lock);
    if (slot >= 0 && slot < MAX_TASK_START_SLOTS) {
        wrap = g_task_start_slots[slot];
        g_task_start_slots[slot] = NULL;
    }
    nxmutex_unlock(&g_task_start_lock);

    if (wrap) {
        func = wrap->func;
        params = wrap->arg;
        kmm_free(wrap);
    }

    if (func) {
        func(params);
    } else {
        AIC_LOG_PRINTF("[rtos] task entry missing func argc=%d slot=%d\n",
                       argc, slot);
    }

    rtos_task_remove_notification((rtos_task_handle)getpid());
    return 0;
}

/*
 * FUNCTIONS
 ****************************************************************************************
 */
void rtos_remove_all(void)
{
    AIC_LOG_PRINTF("[rtos]%s not support!!!\n", __func__);
}

void rtos_res_list_info_show(void)
{
    AIC_LOG_PRINTF("[rtos]%s not support!!!\n", __func__);
}

unsigned long rtos_now(bool isr)
{
    /* clock_systime_ticks() returns ticks; convert to ms */
    clock_t ticks = clock_systime_ticks();
    return (unsigned long)TICK2MSEC(ticks);
}

void rtos_msleep(uint32_t time_in_ms)
{
    nxsig_usleep((useconds_t)time_in_ms * 1000);
}

void rtos_udelay(unsigned int us)
{
    up_udelay(us);
}

void rtos_mem_free_task(void * taskref)
{
    AIC_LOG_PRINTF("[rtos]%s not support!!! task=%p\n", __func__, taskref);
}

void *rtos_malloc(uint32_t size)
{
    void *p = kmm_malloc((size_t)size);
    if (p) {
        rtos_mem_cnt++;
    }
    return p;
}

void *rtos_calloc(uint32_t nb_elt, uint32_t size)
{
    void *p = kmm_calloc((size_t)nb_elt, (size_t)size);
    if (p) {
        rtos_mem_cnt++;
    }
    return p;
}

void rtos_free(void *ptr)
{
    if (ptr) {
        kmm_free(ptr);
        rtos_free_cnt++;
    }
}

void rtos_mem_cur_cnt(uint32_t* mtot,uint32_t*ftot,uint32_t*rtot)
{
    *mtot = rtos_mem_cnt;
    *ftot = rtos_free_cnt;
    *rtot = rtos_remain_cnt;
}

void rtos_memcpy(void *pdest, const void *psrc, uint32_t size)
{
    memcpy(pdest, psrc, size);
}

void rtos_memset(void *pdest, uint8_t value, uint32_t size)
{
    memset(pdest, value, size);
}

void rtos_net_mempush(void*pbuf,uint32_t size,void*pmsg,uint16_t type)
{
    /* not implemented */
}

void rtos_net_mempop(void*pmsg,uint16_t type)
{
    /* not implemented */
}

void rtos_net_mem_free(void*pmsg,uint16_t type)
{
    /* not implemented */
}

void rtos_socket_push(int sock)
{
    /* not implemented */
}

void rtos_socket_pop(int sock)
{
    /* not implemented */
}

uint32_t rtos_get_task_status(rtos_task_handle ref)
{
    /* NuttX does not expose per-task status via simple field access.
     * Return READY as default so callers don't get stuck. */
    AIC_LOG_PRINTF("[rtos]%s: handle=%d\n", __func__, ref);
    return AIC_RTOS_READY;
}

uint32_t rtos_wait_task_suspend(rtos_task_handle ref)
{
    /* Stub: NuttX tasks are cooperative in the areas this is used */
    rtos_msleep(1);
    return 0;
}

uint32_t rtos_wait_task_suspend_only(rtos_task_handle ref)
{
    rtos_msleep(1);
    return 0;
}

void rtos_wait_all_task_suspend(void)
{
    AIC_LOG_PRINTF("[rtos]%s not support!!\n", __func__);
}

rtos_task_handle rtos_get_current_task(void)
{
    return (rtos_task_handle)getpid();
}

char * rtos_get_task_name(void* ref)
{
    /* NuttX: get task name from /proc is expensive; return a static string */
    return (char *)"aic_task";
}

int rtos_task_create(rtos_task_fct func,
                     const char * const name,
                     int task_id,
                     const uint16_t stack_depth,
                     void * const params,
                     rtos_prio prio,
                     rtos_task_handle * task_handle)
{
    int ret;
    int slot;
    char slot_arg[12];
    char *argv[2];

    AIC_ASSERT_ERR(task_handle != NULL);

    if (prio < 1) prio = 1;
    if (prio > 254) prio = 254;

    struct task_wrap *wrap = kmm_zalloc(sizeof(struct task_wrap));
    if (!wrap) {
        return -3;
    }

    wrap->func = func;
    wrap->arg = params;
    wrap->slot = -1;

    nxmutex_lock(&g_task_start_lock);
    for (slot = 0; slot < MAX_TASK_START_SLOTS; slot++) {
        if (g_task_start_slots[slot] == NULL) {
            wrap->slot = slot;
            g_task_start_slots[slot] = wrap;
            break;
        }
    }
    nxmutex_unlock(&g_task_start_lock);

    if (wrap->slot < 0) {
        kmm_free(wrap);
        AIC_LOG_PRINTF("[rtos] no start slot for task '%s'\n", name);
        return -3;
    }

    snprintf(slot_arg, sizeof(slot_arg), "%d", wrap->slot);
    argv[0] = slot_arg;
    argv[1] = NULL;

    ret = kthread_create(name ? name : "aic_task", prio, stack_depth,
                         task_entry, argv);
    if (ret < 0) {
        int err = get_errno();
        struct mallinfo mi = mallinfo();

        nxmutex_lock(&g_task_start_lock);
        if (wrap->slot >= 0 && wrap->slot < MAX_TASK_START_SLOTS &&
            g_task_start_slots[wrap->slot] == wrap) {
            g_task_start_slots[wrap->slot] = NULL;
        }
        nxmutex_unlock(&g_task_start_lock);
        kmm_free(wrap);
        AIC_LOG_PRINTF("[rtos] kthread_create '%s' failed: ret=%d errno=%d "
                       "stack=%u prio=%d free=%u maxfree=%u used=%u\n",
                       name, ret, err, (unsigned int)stack_depth, prio,
                       mi.fordblks, mi.mxordblk, mi.uordblks);
        return -3;
    }

    *task_handle = (rtos_task_handle)ret;
    (void)task_id;

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("[rtos]%s '%s', pid=%d, stack=%d\n",
                   __func__, name, ret, stack_depth);
#endif

    return 0;
}

int rtos_task_create_only(rtos_task_fct func,
                          const char * const name,
                          int task_id,
                          const uint16_t stack_depth,
                          void * const params,
                          rtos_prio prio,
                          rtos_task_handle * task_handle)
{
    /* Same as rtos_task_create for NuttX - thread starts immediately */
    return rtos_task_create(func, name, task_id, stack_depth, params, prio, task_handle);
}

void rtos_task_delete(rtos_task_handle task_handle)
{
    rtos_task_handle task = task_handle;
    int waited = 0;
    int ret;

    if (task_handle == 0) {
        task = (rtos_task_handle)getpid();
        rtos_task_remove_notification(task);
        kthread_delete(0);
        return;
    }

    ret = kthread_delete(task_handle);
    if (ret == -EACCES) {
        AIC_LOG_PRINTF("[rtos] task delete %d via HPWORK\n", task_handle);
        ret = rtos_task_delete_from_kernel(task_handle);
    }

    if (ret < 0 && ret != -ESRCH) {
        AIC_LOG_PRINTF("[rtos] task delete %d failed: %d errno=%d\n",
                       task_handle, ret, get_errno());
        return;
    }

    while (rtos_task_pid_alive(task) && waited < RTOS_TASK_DELETE_WAIT_MS) {
        rtos_msleep(RTOS_TASK_DELETE_POLL_MS);
        waited += RTOS_TASK_DELETE_POLL_MS;
    }

    if (rtos_task_pid_alive(task)) {
        AIC_LOG_PRINTF("[rtos] task delete %d still alive after %d ms\n",
                       task, waited);
    } else {
        rtos_task_remove_notification(task);
    }
}

bool rtos_task_isvalid(rtos_task_handle task_handle)
{
    /* In NuttX, we can't easily check if a PID is still alive without /proc.
     * Just return true. */
    return true;
}

void rtos_task_suspend(int duration)
{
    if (-1 == duration) {
        /* Suspend forever - sleep in a loop */
        while (1) {
            nxsig_usleep(100000000);  /* 100s chunks */
        }
    } else if (duration > 0) {
        rtos_msleep((uint32_t)duration);
    }
}

void rtos_task_resume(rtos_task_handle task_handle)
{
    /* NuttX does not have direct task resume by PID.
     * In the AIC driver this is used for cleanup paths.
     * Signal the task with SIGCONT if possible, else no-op. */
    (void)task_handle;
}

uint32_t rtos_task_get_priority(rtos_task_handle task_handle)
{
    /* NuttX: sched_getparam could be used, but needs pid validation.
     * Return a sensible default. */
    return 10;
}

void rtos_task_set_priority(rtos_task_handle task_handle, uint32_t priority)
{
    /* NuttX: sched_setparam could be used. Stub for now. */
    (void)task_handle;
    (void)priority;
}

/*
 * Task notification - using per-task semaphores
 */
int rtos_task_init_notification(rtos_task_handle task)
{
    struct task_notify_ctx *ctx = kmm_zalloc(sizeof(struct task_notify_ctx));
    int i;

    if (!ctx) {
        return -1;
    }

    ctx->task = task;
    nxsem_init(&ctx->sem, 0, 0);  /* binary semaphore, initial count 0 */

    nxmutex_lock(&g_notify_lock);
    if (rtos_task_find_notify_locked(task)) {
        nxmutex_unlock(&g_notify_lock);
        nxsem_destroy(&ctx->sem);
        kmm_free(ctx);
        return 0;
    }

    for (i = 0; i < MAX_TASK_NOTIFY_SLOTS; i++) {
        if (g_notify_table[i] == NULL) {
            g_notify_table[i] = ctx;
            nxmutex_unlock(&g_notify_lock);
            return 0;
        }
    }

    nxmutex_unlock(&g_notify_lock);
    nxsem_destroy(&ctx->sem);
    kmm_free(ctx);
    return -1;
}

uint32_t rtos_task_wait_notification(int timeout)
{
    struct task_notify_ctx *ctx;
    int ret;

    nxmutex_lock(&g_notify_lock);
    ctx = rtos_task_find_notify_locked((rtos_task_handle)getpid());
    nxmutex_unlock(&g_notify_lock);

    if (!ctx) {
        /* No notification context - just sleep for the timeout */
        if (timeout > 0) {
            rtos_msleep((uint32_t)timeout);
        }
        return 0;
    }

    if (timeout < 0) {
        /* Wait forever */
        do {
            ret = nxsem_wait(&ctx->sem);
        } while (ret < 0 && (ret == -EINTR || get_errno() == EINTR));
    } else if (timeout == 0) {
        ret = nxsem_trywait(&ctx->sem);
        if (ret < 0) return 0;
    } else {
        /* Timed wait - convert ms to ticks */
        clock_t ticks = MSEC2TICK(timeout);
        ret = nxsem_tickwait(&ctx->sem, ticks);
        if (ret == -ETIMEDOUT) {
            return 0;
        }
    }

    return 1;  /* notified */
}

void rtos_task_notify(rtos_task_handle task_handle, uint32_t value, bool isr)
{
    struct task_notify_ctx *ctx;

    nxmutex_lock(&g_notify_lock);
    ctx = rtos_task_find_notify_locked(task_handle);
    nxmutex_unlock(&g_notify_lock);

    if (ctx) {
        nxsem_post(&ctx->sem);
    }
}

void rtos_task_notify_setbits(rtos_task_handle task_handle, uint32_t value, bool isr)
{
    /* Same as rtos_task_notify for our semaphore-based impl */
    rtos_task_notify(task_handle, value, isr);
}

/*
 * Message Queue - fixed-size RTOS queue
 */
int rtos_queue_create(int elt_size, int nb_elt, rtos_queue *queue, const char * const name)
{
    AIC_ASSERT_ERR(queue != NULL);

    if (elt_size <= 0 || nb_elt <= 0) {
        AIC_LOG_PRINTF("[rtos]create queue '%s' invalid size=%d num=%d\n",
                       name ? name : "aic_mq", elt_size, nb_elt);
        return -2;
    }

    struct aic_mq_wrapper *w = kmm_zalloc(sizeof(struct aic_mq_wrapper));
    if (!w) {
        return -2;
    }

    w->storage = kmm_malloc((size_t)elt_size * (size_t)nb_elt);
    if (!w->storage) {
        kmm_free(w);
        return -2;
    }

    if (nxsem_init(&w->items, 0, 0) < 0) {
        kmm_free(w->storage);
        kmm_free(w);
        return -2;
    }

    if (nxsem_init(&w->slots, 0, (unsigned int)nb_elt) < 0) {
        nxsem_destroy(&w->items);
        kmm_free(w->storage);
        kmm_free(w);
        return -2;
    }

    if (nxsem_init(&w->lock, 0, 1) < 0) {
        nxsem_destroy(&w->slots);
        nxsem_destroy(&w->items);
        kmm_free(w->storage);
        kmm_free(w);
        return -2;
    }

    w->magic = AIC_MQ_MAGIC;
    w->msg_size = elt_size;
    w->maxmsg = nb_elt;
    snprintf(w->user_name, sizeof(w->user_name), "%s",
             name ? name : "aic_mq");

    int id = __atomic_fetch_add(&g_mq_counter, 1, __ATOMIC_RELAXED);
    snprintf(w->name, sizeof(w->name), "aq%d", id);

    *queue = w;

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("[rtos]%s '%s', wrapper=%p msg_size=%d maxmsg=%d\n",
                    __func__, name, w, w->msg_size, w->maxmsg);
#endif

    return 0;
}

void rtos_queue_delete(rtos_queue queue)
{
    struct aic_mq_wrapper *w = queue;

    if (w == NULL || w->magic != AIC_MQ_MAGIC) {
        return;
    }

    w->magic = 0;
    nxsem_destroy(&w->lock);
    nxsem_destroy(&w->slots);
    nxsem_destroy(&w->items);
    kmm_free(w->storage);
    kmm_free(w);
}

bool rtos_queue_is_empty(rtos_queue queue)
{
    struct aic_mq_wrapper *w = queue;
    bool empty;

    if (w == NULL || w->magic != AIC_MQ_MAGIC) {
        return true;
    }

    nxsem_wait(&w->lock);
    empty = (w->count == 0);
    nxsem_post(&w->lock);

    return empty;
}

bool rtos_queue_is_full(rtos_queue queue)
{
    struct aic_mq_wrapper *w = queue;
    bool full;

    if (w == NULL || w->magic != AIC_MQ_MAGIC) {
        return false;
    }

    nxsem_wait(&w->lock);
    full = (w->count >= w->maxmsg);
    nxsem_post(&w->lock);

    return full;
}

int rtos_queue_cnt(rtos_queue queue)
{
    struct aic_mq_wrapper *w = queue;
    int count;

    if (w == NULL || w->magic != AIC_MQ_MAGIC) {
        return 0;
    }

    nxsem_wait(&w->lock);
    count = w->count;
    nxsem_post(&w->lock);

    return count;
}

int rtos_queue_write(rtos_queue queue, void *msg, int timeout, bool isr)
{
    struct aic_mq_wrapper *w = queue;
    int ret;

    if (w == NULL || w->magic != AIC_MQ_MAGIC || msg == NULL) {
        return -1;
    }

    ret = rtos_queue_wait_sem(&w->slots, isr ? 0 : timeout);
    if (ret < 0) {
        rtos_queue_dump_error("write", w, timeout, get_errno());
        return -2;
    }

    nxsem_wait(&w->lock);
    memcpy(&w->storage[w->tail * w->msg_size], msg, (size_t)w->msg_size);
    w->tail = (w->tail + 1) % w->maxmsg;
    w->count++;
    nxsem_post(&w->lock);

    nxsem_post(&w->items);
    return 0;
}

int rtos_queue_read(rtos_queue queue, void *msg, int timeout, bool isr)
{
    struct aic_mq_wrapper *w = queue;
    int ret;

    if (w == NULL || w->magic != AIC_MQ_MAGIC || msg == NULL) {
        return -1;
    }

    ret = rtos_queue_wait_sem(&w->items, isr ? 0 : timeout);
    if (ret < 0) {
        int err = get_errno();

        if (timeout >= 0 && (err == ETIMEDOUT || err == EAGAIN)) {
            return AIC_RTOS_WAIT_ERROR;
        }

        rtos_queue_dump_error("read", w, timeout, err);
        return AIC_RTOS_ERR;
    }

    nxsem_wait(&w->lock);
    memcpy(msg, &w->storage[w->head * w->msg_size], (size_t)w->msg_size);
    w->head = (w->head + 1) % w->maxmsg;
    w->count--;
    nxsem_post(&w->lock);

    nxsem_post(&w->slots);
    return 0;
}

/*
 * Semaphore - using NuttX nxsem
 */
int rtos_semaphore_create(rtos_semaphore *semaphore, const char * const name, int max_count, int init_count)
{
    int ret = 0;
    if (semaphore == NULL) {
        return -1;
    }

    sem_t *sem = kmm_zalloc(sizeof(sem_t));
    if (sem == NULL) {
        return -2;
    }

    ret = nxsem_init(sem, 0, (unsigned int)init_count);
    if (ret < 0) {
        kmm_free(sem);
        return -2;
    }

    *semaphore = (rtos_semaphore)sem;
    return ret;
}

int rtos_semaphore_create_only(rtos_semaphore *semaphore, const char * const name, int max_count, int init_count)
{
    return rtos_semaphore_create(semaphore, name, max_count, init_count);
}

void rtos_semaphore_delete(rtos_semaphore semaphore)
{
    if (semaphore) {
        nxsem_destroy(semaphore);
        kmm_free(semaphore);
    }
}

int rtos_semaphore_get_count(rtos_semaphore semaphore)
{
    int val = 0;
    if (semaphore) {
        nxsem_get_value(semaphore, &val);
    }
    return val;
}

int rtos_semaphore_wait(rtos_semaphore semaphore, int timeout)
{
    int ret;

    if (semaphore == NULL) {
        return -1;
    }

    if (timeout < 0) {
        /* Wait forever */
        do {
            ret = nxsem_wait(semaphore);
        } while (ret < 0 && get_errno() == EINTR);
        if (ret < 0) return -1;
        return 0;
    } else if (timeout == 0) {
        ret = nxsem_trywait(semaphore);
        if (ret < 0) return 1;  /* not available => timeout */
        return 0;
    } else {
        /* Timed wait in ticks */
        clock_t ticks = MSEC2TICK(timeout);
        ret = nxsem_tickwait(semaphore, ticks);
        if (ret == -ETIMEDOUT) {
            return 1;  /* timeout */
        }
        if (ret < 0) return -1;
        return 0;
    }
}

int rtos_semaphore_signal(rtos_semaphore semaphore, bool isr)
{
    if (semaphore == NULL) {
        aic_dbg("NULL SEM!!!\n");
        return -1;
    }
    return nxsem_post(semaphore);
}

/*
 * Timer - using NuttX watchdog
 */
int rtos_timer_create(const char * const name,
                      rtos_timer *timer,
                      const uint32_t ms,
                      const uint8_t periodic,
                      void * const args,
                      rtos_timer_fct func)
{
    if (!timer) {
        AIC_LOG_PRINTF("[rtos] null timer: %s\n", name);
        return -1;
    }

    struct aic_wdog_wrapper *tw = kmm_zalloc(sizeof(struct aic_wdog_wrapper));
    if (!tw) {
        AIC_LOG_PRINTF("[rtos] timer alloc fail: %s\n", name);
        return -1;
    }

    wd_init(&tw->wdog);
    tw->func = func;
    tw->arg = args;
    tw->period = MSEC2TICK(ms);
    tw->periodic = (periodic != 0);

    *timer = tw;
    return 0;
}

int rtos_timer_start(rtos_timer timer, const uint32_t ms)
{
    if (timer == NULL) {
        AIC_LOG_PRINTF("[rtos] null timer\n");
        return -1;
    }

    struct aic_wdog_wrapper *tw = timer;

    /* Cancel if already running */
    if (WDOG_ISACTIVE(&tw->wdog)) {
        wd_cancel(&tw->wdog);
    }

    /* Update period if ms changed */
    if (ms > 0) {
        tw->period = MSEC2TICK(ms);
    }

    if (tw->period <= 0) {
        tw->period = 1;  /* at least 1 tick */
    }

    int ret = wd_start(&tw->wdog, tw->period, aic_wdog_handler,
                        (wdparm_t)(uintptr_t)tw);
    if (ret < 0) {
        AIC_LOG_PRINTF("[rtos] timer start fail: %d\n", ret);
        return -2;
    }
    return 0;
}

int rtos_timer_get_status(rtos_timer timer, rtos_timer_status *timer_status)
{
    if (timer == NULL) {
        AIC_LOG_PRINTF("[rtos] null timer\n");
        return -1;
    }

    struct aic_wdog_wrapper *tw = timer;
    if (timer_status) {
        *timer_status = WDOG_ISACTIVE(&tw->wdog) ? AIC_RTOS_TIMER_ACT
                                                   : AIC_RTOS_TIMER_DACT;
    }
    return 0;
}

int rtos_timer_stop(rtos_timer timer)
{
    if (timer == NULL) {
        AIC_LOG_PRINTF("[rtos] null timer\n");
        return -1;
    }

    struct aic_wdog_wrapper *tw = timer;
    int ret = wd_cancel(&tw->wdog);
    if (ret < 0) {
        AIC_LOG_PRINTF("[rtos] timer stop fail: %d\n", ret);
        return -2;
    }
    return 0;
}

int rtos_timer_stop_isr(rtos_timer timer)
{
    AIC_ASSERT_ERR(0);
    return 0;
}

int rtos_timer_delete(rtos_timer timer)
{
    if (timer == NULL) {
        AIC_LOG_PRINTF("[rtos] null timer\n");
        return -1;
    }

    struct aic_wdog_wrapper *tw = timer;

    /* Cancel if running */
    if (WDOG_ISACTIVE(&tw->wdog)) {
        wd_cancel(&tw->wdog);
    }

    kmm_free(tw);
    return 0;
}

/*
 * Mutex - represented as a binary semaphore.
 *
 * The vendor stack deletes worker tasks while they may be blocked on RTOS
 * mutexes. NuttX mutexes track owners and assert on some RT-Thread-style
 * forced-delete paths, while a binary semaphore matches this stack's simple
 * lock/unlock use and keeps stop/deinit robust.
 */
int rtos_mutex_create(rtos_mutex *mutex, const char * const name)
{
    if (mutex == NULL) {
        return -1;
    }

    sem_t *mtx = kmm_zalloc(sizeof(sem_t));
    if (mtx == NULL) {
        return -3;
    }

    int ret = nxsem_init(mtx, 0, 1);
    if (ret < 0) {
        kmm_free(mtx);
        return -3;
    }

    *mutex = (rtos_mutex)mtx;
    return 0;
}

void rtos_mutex_delete(rtos_mutex mutex)
{
    if (mutex) {
        nxsem_destroy(mutex);
        kmm_free(mutex);
    }
}

int rtos_mutex_lock(rtos_mutex mutex, int timeout)
{
    if (mutex == NULL) {
        AIC_LOG_PRINTF("[rtos]lock mutex null\n");
        return -1;
    }

    if (timeout < 0) {
        int ret;
        do {
            ret = nxsem_wait(mutex);
        } while (ret < 0 && (ret == -EINTR || get_errno() == EINTR));
        return ret;
    } else if (timeout == 0) {
        int ret = nxsem_trywait(mutex);
        return ret;
    } else {
        return nxsem_tickwait(mutex, MSEC2TICK(timeout));
    }
}

int rtos_mutex_unlock(rtos_mutex mutex)
{
    int val = 0;

    if (mutex == NULL) {
        AIC_LOG_PRINTF("[rtos]unlock mutex null\n");
        return -1;
    }

    nxsem_get_value(mutex, &val);
    if (val > 0) {
        return -EPERM;
    }

    return nxsem_post(mutex);
}

extern int clock_gettime(clockid_t clockid, struct timespec *tp);

int aic_time_get(enum time_origin_t origin, uint32_t *sec, uint32_t *usec)
{
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);

    *sec = tp.tv_sec;
    *usec = tp.tv_nsec / 1000;

    return 0;
}

void rtos_critical_enter(void)
{
    irqstate_t flags = enter_critical_section();

    if (g_critical_nest < RTOS_CRITICAL_NEST_MAX) {
        g_critical_flags[g_critical_nest++] = flags;
    } else {
        AIC_LOG_PRINTF("[rtos]critical nesting overflow\n");
    }
}

void rtos_critical_exit(void)
{
    if (g_critical_nest > 0) {
        leave_critical_section(g_critical_flags[--g_critical_nest]);
    } else {
        AIC_LOG_PRINTF("[rtos]critical exit without enter\n");
    }
}

/*
 * Additional stubs
 */
void rtos_mem_list_info(void)
{
    AIC_LOG_PRINTF("[rtos]%s not support!!!\n", __func__);
}

OsResSt* rtos_res_find_res(void *ref, uint16_t type)
{
    return NULL;
}
