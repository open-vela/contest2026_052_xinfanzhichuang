#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/timers/arch_alarm.h>

#include <aic_core.h>
#include "riscv_mtimer.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#define D12X_MTIME (CORET_BASE + 0x7FF8)
#define D12X_MTIMECMP (CORET_BASE + 0x0000)

void up_timer_initialize(void) {
  /* Initialize the system timer
   * NuttX RISC-V usually expects MTIMER setup here.
   * We use the T-Head C906/E907 CORET.
   */
  struct oneshot_lowerhalf_s *lower =
      riscv_mtimer_initialize(D12X_MTIME, D12X_MTIMECMP, RISCV_IRQ_MTIMER,
                              (uint64_t)drv_get_sys_freq());

  DEBUGASSERT(lower);
  up_alarm_set_lowerhalf(lower);

  /* Config Core Timer for Systick */
  // csi_coret_config(drv_get_sys_freq() / CONFIG_USEC_PER_TICK, CORET_IRQn);

  /* Enable Timer Interrupt managed by NuttX IRQ subsystem later,
   * but we can enable the HW source here
   */
}
