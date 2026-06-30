#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "riscv_internal.h"

#include "chip.h"
#include <core_rv32.h>
#include <aic_soc.h>

extern void hal_clk_pll_lowpower(void);
void aic_clk_lowpower(void) { hal_clk_pll_lowpower(); }

void aic_gtc_enable(void) {
  /* enable gtc clk */
  *(volatile uint32_t *)(CMU_BASE + 0x090c) = 0x3100;
  /* enable gtc */
  *(volatile uint32_t *)GTC_BASE = 0x0001;
}

extern int _sheap;
extern int _eheap;
/****************************************************************************
 * Public Functions
 ****************************************************************************/
void up_allocate_heap(void **heap_start, size_t *heap_size) {
  *heap_start = (void *)&_sheap;
  *heap_size = (void *)&_eheap - (void *)&_sheap;
}

void up_irqinitialize(void) {

  /* enable mstatus FS */
#ifdef ARCH_RISCV_FPU
  uint32_t mstatus = __get_MSTATUS();
  mstatus |= (1 << 13);
  __set_MSTATUS(mstatus);
#endif

  /* enable mxstatus THEADISAEE */
  uint32_t mxstatus = __get_MXSTATUS();
  mxstatus |= (1 << 22);
  /* enable mxstatus MM */
  mxstatus |= (1 << 15);
  __set_MXSTATUS(mxstatus);

  /* enable mexstatus SPUSHEN/SPSWAPEN */
  uint32_t mexstatus = __get_MEXSTATUS();
  //#ifndef CONFIG_THEAD_EXT_SPUSHEN
  mexstatus &= ~(1 << 16); // SPUSHEN
                           //#endif
                           //#ifndef CONFIG_THEAD_EXT_SPSWAPEN
  mexstatus &= ~(1 << 17); // SPSWAPEN
                           //#endif
  __set_MEXSTATUS(mexstatus);

  /* get interrupt level from info */
  CLIC->CLICCFG = (((CLIC->CLICINFO & CLIC_INFO_CLICINTCTLBITS_Msk) >>
                    CLIC_INFO_CLICINTCTLBITS_Pos)
                   << CLIC_CLICCFG_NLBIT_Pos);

  for (int i = 0; i < MAX_IRQn; i++) {
    CLIC->CLICINT[i].IP = 0;
    CLIC->CLICINT[i].ATTR = 1; /* use vector interrupt */
  }

  /* tspend use positive interrupt */
  CLIC->CLICINT[Machine_Software_IRQn].ATTR = 0x3;

  csi_dcache_enable();
  csi_icache_enable();

  /* Disable Machine interrupts */
  up_irq_save();

#if defined(CONFIG_STACK_COLORATION) && CONFIG_ARCH_INTERRUPTSTACK > 15
  /* Colorize the interrupt stack for debug purposes */

  size_t intstack_size = (CONFIG_ARCH_INTERRUPTSTACK & ~15);
  riscv_stack_color(g_intstackalloc, intstack_size);
#endif

  //  for (int i = 0; i < MAX_IRQn; i++)
  //  {
  //     CLIC->CLICINT[i].ATTR = 0; /* dont use vector interrupt */
  //  }
  /* Disable all external interrupts and set a default priority */
  for (int id = 0; id < MAX_IRQn; id++) {
    csi_vic_disable_irq(id);
    csi_vic_clear_pending_irq(id);
    csi_vic_set_prio(id, 0);
  }

  csi_vic_set_threshold(0);

  /* Attach the common exception/interrupt handler */
  riscv_exception_attach();

#ifndef CONFIG_SUPPRESS_INTERRUPTS

  /* And finally, enable interrupts */

  up_irq_enable();
#endif

  aic_clk_lowpower();
  aic_gtc_enable();
}

void up_enable_irq(int irq) {
  //	if (irq == RISCV_IRQ_MTIMER)
  //		return;

  if (irq == RISCV_IRQ_MSOFT) {
    /* Enable machine software interrupt */

    SET_CSR(CSR_MIE, MIE_MSIE);
  } else if (irq >= RISCV_IRQ_ASYNC) {
    int extirq = irq - RISCV_IRQ_ASYNC;
    if (extirq >= 0 && extirq < NR_IRQS) {
      csi_vic_enable_irq(extirq);
    }
  }
}

void up_disable_irq(int irq) {
  if (irq == RISCV_IRQ_MSOFT) {
    /* Disable machine software interrupt */

    CLEAR_CSR(CSR_MIE, MIE_MSIE);
  } else if (irq >= RISCV_IRQ_ASYNC) {
    int extirq = irq - RISCV_IRQ_ASYNC;
    if (extirq >= 0 && extirq < NR_IRQS) {
      csi_vic_disable_irq(extirq);
    }
  }
}

void riscv_ack_irq(int irq) {
  if (irq >= RISCV_IRQ_ASYNC) {
    int extirq = irq - RISCV_IRQ_ASYNC;
    if (extirq >= 0 && extirq < NR_IRQS) {
      csi_vic_clear_pending_irq(extirq);
    }
  }
}

irqstate_t up_irq_enable(void) {
  irqstate_t oldstat;

  /* Enable MEIE (machine external interrupt enable) */

  SET_CSR(CSR_MIE, MIE_MEIE);

  /* Enable global machine interrupts */

  oldstat = READ_AND_SET_CSR(CSR_MSTATUS, MSTATUS_MIE);
  return oldstat;
}

void *riscv_dispatch_irq(uintptr_t vector, uintptr_t *regs) {
  int irq = vector & (0xFFF);

  if ((vector & RISCV_IRQ_BIT) != 0) {
    irq += RISCV_IRQ_ASYNC;
  }

  /* Acknowledge the interrupt */

  // riscv_ack_irq(irq);

  /* Deliver the IRQ */

  regs = (uintptr_t *)riscv_doirq(irq, (uintreg_t *)regs);
  return regs;
}