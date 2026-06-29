/*
 * Copyright (c) 2022, Artinchip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(__NuttX__)
#include <nuttx/config.h>
#else
 #include <rtconfig.h>
#endif
#include <cpuport.h>

.section .start_jump, "ax"
.globl __start_jump
__start_jump:
    j __start

.section .vectors, "aw", @progbits
    .align  6
    .globl  __Vectors
    .type   __Vectors, @object
__Vectors:
    .long   __trap_vec      /* 0 */
    .long   __trap_vec      /* 1 */
    .long   __trap_vec      /* 2 */
    .long   __trap_vec      /* 3,以前是PendSV_Handler,这块需要修改 */
    .long   __trap_vec      /* 4 */
    .long   __trap_vec      /* 5 */
    .long   __trap_vec      /* 6 */
    .long   __trap_vec   /* 7 */
    .long   __trap_vec      /* 8 */
    .long   __trap_vec      /* 9 */
    .long   __trap_vec      /* 10 */
    .long   __trap_vec      /* 11 */
    .long   __trap_vec      /* 12 */
    .long   __trap_vec      /* 13 */
    .long   __trap_vec      /* 14 */
    .long   __trap_vec      /* 15 */

    /* External interrupts */
    .long   __trap_vec   /* 16 */
    .long   __trap_vec   /* 17 */
    .long   __trap_vec   /* 18 */
    .long   __trap_vec   /* 19 */
    .long   __trap_vec   /* 20 */
    .long   __trap_vec   /* 21 */
    .long   __trap_vec   /* 22 */
    .long   __trap_vec   /* 23 */
    .long   __trap_vec   /* 24 */
    .long   __trap_vec   /* 25 */
    .long   __trap_vec   /* 26 */
    .long   __trap_vec   /* 27 */
    .long   __trap_vec   /* 28 */
    .long   __trap_vec   /* 29 */
    .long   __trap_vec   /* 30 */
    .long   __trap_vec   /* 31 */
    .long   __trap_vec   /* 32 */
    .long   __trap_vec   /* 33 */
    .long   __trap_vec   /* 34 */
    .long   __trap_vec   /* 35 */
    .long   __trap_vec   /* 36 */
    .long   __trap_vec   /* 37 */
    .long   __trap_vec   /* 38 */
    .long   __trap_vec   /* 39 */
    .long   __trap_vec   /* 40 */
    .long   __trap_vec   /* 41 */
    .long   __trap_vec   /* 42 */
    .long   __trap_vec   /* 43 */
    .long   __trap_vec   /* 44 */
    .long   __trap_vec   /* 45 */
    .long   __trap_vec   /* 46 */
    .long   __trap_vec   /* 47 */
    .long   __trap_vec   /* 48 */
    .long   __trap_vec   /* 49 */
    .long   __trap_vec   /* 50 */
    .long   __trap_vec   /* 51 */
    .long   __trap_vec   /* 52 */
    .long   __trap_vec   /* 53 */
    .long   __trap_vec   /* 54 */
    .long   __trap_vec   /* 55 */
    .long   __trap_vec   /* 56 */
    .long   __trap_vec   /* 57 */
    .long   __trap_vec   /* 58 */
    .long   __trap_vec   /* 59 */
    .long   __trap_vec   /* 60 */
    .long   __trap_vec   /* 61 */
    .long   __trap_vec   /* 62 */
    .long   __trap_vec   /* 63 */
    .long   __trap_vec   /* 64 */
    .long   __trap_vec   /* 65 */
    .long   __trap_vec   /* 66 */
    .long   __trap_vec   /* 67 */
    .long   __trap_vec   /* 68 */
    .long   __trap_vec   /* 69 */
    .long   __trap_vec   /* 70 */
    .long   __trap_vec   /* 71 */
    .long   __trap_vec   /* 72 */
    .long   __trap_vec   /* 73 */
    .long   __trap_vec   /* 74 */
    .long   __trap_vec   /* 75 */
    .long   __trap_vec   /* 76 */
    .long   __trap_vec   /* 77 */
    .long   __trap_vec   /* 78 */
    .long   __trap_vec   /* 79 */
    .long   __trap_vec   /* 80 */
    .long   __trap_vec   /* 81 */
    .long   __trap_vec   /* 82 */
    .long   __trap_vec   /* 83 */
    .long   __trap_vec   /* 84 */
    .long   __trap_vec   /* 85 */
    .long   __trap_vec   /* 86 */
    .long   __trap_vec   /* 87 */
    .long   __trap_vec   /* 88 */
    .long   __trap_vec   /* 89 */
    .long   __trap_vec   /* 90 */
    .long   __trap_vec   /* 91 */
    .long   __trap_vec   /* 92 */
    .long   __trap_vec   /* 93 */
    .long   __trap_vec   /* 94 */
    .long   __trap_vec   /* 95 */
    .long   __trap_vec   /* 96 */
    .long   __trap_vec   /* 97 */
    .long   __trap_vec   /* 98 */
    .long   __trap_vec   /* 99 */
    .size __Vectors,.-__Vectors

    .text
    .align  2
    .globl  __start
__start:
    .text
    .align  2
    .globl  Reset_Handler
    .globl  save_boot_params_ret
    .type   save_boot_params_ret,%function
    .type   Reset_Handler, %function
Reset_Handler:
.option push
.option norelax

    csrw mie, zero
    csrw mip, zero
    la      gp, __global_pointer$
.option pop
    la      a0, __trap_vec
    ori     a0, a0, 3
    csrw    mtvec, a0
    la      a0, __Vectors
    csrw    0x307, a0
    la      sp, _estack

    /* Enable FPU */
    li      t0, 0x00006000     /* mstatus.FS = 11 (Dirty) */
    csrs    mstatus, t0
    fscsr   zero

#if 0
    la      a5, icache_enable
    jalr    a5
    la      a5, dcache_enable
    jalr    a5
#endif
    /* Clear bss section */
    la      a0, _sbss
    la      a1, _ebss
    bgeu    a0, a1, 2f
1:
    sw      zero, (a0)
    addi    a0, a0, 4
    bltu    a0, a1, 1b
2:
    /* Simple delay */
    li      t2, 0x10000
delay_loop:
    addi    t2, t2, -1
    bnez    t2, delay_loop

    /* DEBUG: Memory Check at 0x40100000 (Away from code text) */
    li      t3, 0x40100000
    li      t4, 0x55aa55aa
    sw      t4, 0(t3)           /* Write pattern */
    lw      t5, 0(t3)           /* Read back */
    bne     t4, t5, mem_fail    /* If not match, jump to fail loop */

    /* Simple delay 2 */
    li      t2, 0x10000
delay_loop2:
    addi    t2, t2, -1
    bnez    t2, delay_loop2

    la      a5, nx_start
    jalr    a5

mem_fail:
    /* Print 'F' (Fail) and loop forever */
    li      t1, 0x46           /* 'F' */
    sw      t1, 0(t0)
1:  j       1b

    .size   Reset_Handler, . - Reset_Handler

__exit:
    j      __exit