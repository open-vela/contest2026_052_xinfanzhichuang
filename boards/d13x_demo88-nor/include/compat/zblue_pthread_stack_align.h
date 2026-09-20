/*
 * Zblue's POSIX port passes Zephyr stack objects directly to
 * pthread_attr_setstack().  On D13x RISC-V, NuttX requires the provided stack
 * base to be 16-byte aligned, while the imported POSIX arch shim may create
 * 8-byte aligned stack objects for 32-bit builds.
 */

#ifndef D13X_ZBLUE_PTHREAD_STACK_ALIGN_H
#define D13X_ZBLUE_PTHREAD_STACK_ALIGN_H

#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#define D13X_ZBLUE_STACK_ALIGN 16

static inline int d13x_zblue_pthread_attr_setstack(pthread_attr_t *attr,
                                                   void *stackaddr,
                                                   size_t stacksize)
{
  uintptr_t base = (uintptr_t)stackaddr;
  uintptr_t aligned = (base + D13X_ZBLUE_STACK_ALIGN - 1) &
                      ~(uintptr_t)(D13X_ZBLUE_STACK_ALIGN - 1);
  size_t delta = aligned - base;
  size_t aligned_size;

  if (stacksize <= delta)
    {
      return EINVAL;
    }

  aligned_size = (stacksize - delta) &
                 ~(size_t)(D13X_ZBLUE_STACK_ALIGN - 1);
  if (aligned_size < PTHREAD_STACK_MIN)
    {
      return EINVAL;
    }

  return pthread_attr_setstack(attr, (void *)aligned, aligned_size);
}

#define pthread_attr_setstack(attr, stackaddr, stacksize) \
  d13x_zblue_pthread_attr_setstack((attr), (stackaddr), (stacksize))

#endif /* D13X_ZBLUE_PTHREAD_STACK_ALIGN_H */
