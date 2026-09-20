/****************************************************************************
 * vendor/artinchip/chips/d12x/include/irq.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#ifndef __AIC_IRQ_D13X_H__
#define __AIC_IRQ_D13X_H__


/* Interrupt Number Definition (NuttX IRQ numbers) */

#define AIC_IRQ_NUM_BASE 0
#define AIC_IRQ(n) (RISCV_IRQ_ASYNC + AIC_IRQ_NUM_BASE + (n))

#define AIC_IRQ_DCE AIC_IRQ(31)
#define AIC_IRQ_DMA AIC_IRQ(32)
#define AIC_IRQ_SPI_ENC AIC_IRQ(41)
#define AIC_IRQ_QSPI0 AIC_IRQ(44)
#define AIC_IRQ_QSPI1 AIC_IRQ(45)
#define AIC_IRQ_SDMC0 AIC_IRQ(46)
#define AIC_IRQ_SDMC1 AIC_IRQ(47)
#define AIC_IRQ_XSPI AIC_IRQ(49)
#define AIC_IRQ_MTOP AIC_IRQ(51)
#define AIC_IRQ_AUDIO AIC_IRQ(54)
#define AIC_IRQ_LCD AIC_IRQ(55)
#define AIC_IRQ_DE AIC_IRQ(59)
#define AIC_IRQ_GE AIC_IRQ(60)
#define AIC_IRQ_VE AIC_IRQ(61)
#define AIC_IRQ_WDT AIC_IRQ(64)
#define AIC_IRQ_GPIO AIC_IRQ(68) /* 68~75 */
#define AIC_IRQ_UART0 AIC_IRQ(76)
#define AIC_IRQ_UART1 AIC_IRQ(77)
#define AIC_IRQ_UART2 AIC_IRQ(78)
#define AIC_IRQ_UART3 AIC_IRQ(79)
#define AIC_IRQ_UART4 AIC_IRQ(80)
#define AIC_IRQ_UART5 AIC_IRQ(81)
#define AIC_IRQ_UART6 AIC_IRQ(82)
#define AIC_IRQ_UART7 AIC_IRQ(83)
#define AIC_IRQ_I2C0 AIC_IRQ(84)
#define AIC_IRQ_I2C1 AIC_IRQ(85)
#define AIC_IRQ_I2C2 AIC_IRQ(86)
#define AIC_IRQ_CAN0 AIC_IRQ(88)
#define AIC_IRQ_CAN1 AIC_IRQ(89)
#define AIC_IRQ_PWM AIC_IRQ(90)
#define AIC_IRQ_GPAI AIC_IRQ(92)
#define AIC_IRQ_RTP AIC_IRQ(93)
#define AIC_IRQ_TSEN AIC_IRQ(94)
#define AIC_IRQ_CIR AIC_IRQ(95)

#define NR_IRQS 128

#endif /* __AIC_IRQ_D13X_H__ */
