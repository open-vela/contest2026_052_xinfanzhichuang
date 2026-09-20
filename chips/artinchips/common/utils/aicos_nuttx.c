/*
 * ArtInChip OS Abstraction Layer - NuttX Implementation
 *
 * Provides aicos_sem_create/take/release for HAL drivers
 */

#include <nuttx/config.h>
#include <stdlib.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <nuttx/clock.h>
#include <nuttx/semaphore.h>
#include "aic_osal.h"

aicos_sem_t aicos_sem_create(uint32_t init_val)
{
    sem_t *sem = (sem_t *)malloc(sizeof(sem_t));
    if (!sem)
        return NULL;

    nxsem_init(sem, 0, init_val);
    return (aicos_sem_t)sem;
}

int aicos_sem_take(aicos_sem_t sem, uint32_t timeout_ms)
{
    if (!sem)
        return -1;

    sem_t *s = (sem_t *)sem;

    if (timeout_ms == 0xFFFFFFFF)
    {
        return (nxsem_wait(s) == 0) ? 0 : -1;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)timeout_ms * 1000000ULL;
    ts.tv_sec  += (time_t)(ns / 1000000000ULL);
    ts.tv_nsec += (long)(ns % 1000000000ULL);
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    return (nxsem_tickwait(s, MSEC2TICK(timeout_ms)) == 0) ? 0 : -1;
}

void aicos_sem_release(aicos_sem_t sem)
{
    if (sem)
        nxsem_post((sem_t *)sem);
}

void aicos_sem_delete(aicos_sem_t sem)
{
    if (sem)
    {
        nxsem_destroy((sem_t *)sem);
        free(sem);
    }
}
