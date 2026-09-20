#ifndef __HAL_GPPAI_D13X_H__
#define __HAL_GPPAI_D13X_H__

#define GPAI_MCR            0x000
#define GPAI_INTR           0x004

#define GPAI_CHnCR(n)       (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x00)
#define GPAI_CHnINT(n)      (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x04)
#define GPAI_CHnPSI(n)      (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x08)
#define GPAI_CHnHLAT(n)     (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x10)
#define GPAI_CHnLLAT(n)     (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x14)
#define GPAI_CHnACR(n)      (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x18)
#define GPAI_CHnFCR(n)      (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x20)
#define GPAI_CHnDATA(n)     (0x100 + (((n) & AIC_GPAI_CH_NUM_MASK) << 6) + 0x24)
#define GPAI_VERSION        0xFFC

#define GPAI_MCR_CH0_EN                 BIT(8)
#define GPAI_MCR_CH_EN(n)               (GPAI_MCR_CH0_EN << (n))
#define GPAI_MCR_EN                     BIT(0)

#define GPAI_INTR_CH0_INT_FLAG          BIT(16)
#define GPAI_INTR_CH_INT_FLAG(n)        (GPAI_INTR_CH0_INT_FLAG << (n))
#define GPAI_INTR_CH0_INT_EN            BIT(0)
#define GPAI_INTR_CH_INT_EN(n)          (GPAI_INTR_CH0_INT_EN << (n))



#define GPAI_CHnCR_SBC_SHIFT        24
#define GPAI_CHnCR_SBC_2_POINTS     1
#define GPAI_CHnCR_SBC_4_POINTS     2
#define GPAI_CHnCR_SBC_8_POINTS     3

#define GPAI_CHnCR_SBC_SHIFT            24
#define GPAI_CHnCR_SBC_MASK             GENMASK(25, 24)
#define GPAI_CHnCR_ADC_ACQ_SHIFT        8
#define GPAI_CHnCR_ADC_ACQ_MASK         GENMASK(15, 8)
#define GPAI_CHnCR_HIGH_ADC_PRIORITY    BIT(4)
#define GPAI_CHnCR_PERIOD_SAMPLE_EN     BIT(1)
#define GPAI_CHnCR_SINGLE_SAMPLE_EN     BIT(0)

#define GPAI_CHnINT_LLA_RM_FLAG     BIT(23)
#define GPAI_CHnINT_LLA_VALID_FLAG  BIT(22)
#define GPAI_CHnINT_HLA_RM_FLAG     BIT(21)
#define GPAI_CHnINT_HLA_VALID_FLAG  BIT(20)
#define GPAI_CHnINT_FIFO_ERR_FLAG   BIT(17)
#define GPAI_CHnINT_DRDY_FLG        BIT(16)
#define GPAI_CHnINT_LLA_RM_IE       BIT(7)
#define GPAI_CHnINT_LLA_VALID_IE    BIT(6)
#define GPAI_CHnINT_HLA_RM_IE       BIT(5)
#define GPAI_CHnINT_HLA_VALID_IE    BIT(4)
#define GPAI_CHnINT_FIFO_ERR_IE     BIT(1)
#define GPAI_CHnINT_DAT_RDY_IE      BIT(0)

#define GPAI_CHnLAT_HLLA_RM_THD_SHIFT   16
#define GPAI_CHnLAT_HLLA_RM_THD_MASK    GENMASK(27, 16)
#define GPAI_CHnLAT_HLLA_THD_MASK       GENMASK(11, 0)
#define GPAI_CHnLAT_HLA_RM_THD(n)       ((n) - 30)
#define GPAI_CHnLAT_LLA_RM_THD(n)       ((n) + 30)

#define GPAI_CHnACR_DISCARD_NOR_DAT     BIT(6)
#define GPAI_CHnACR_DISCARD_LL_DAT      BIT(5)
#define GPAI_CHnACR_DISCARD_HL_DAT      BIT(4)
#define GPAI_CHnACR_LLA_EN              BIT(1)
#define GPAI_CHnACR_HLA_EN              BIT(0)

#define GPAI_CHnFCR_DAT_CNT_MAX(ch)     ((ch) > 1 ? 0x8 : 0x40)
#define GPAI_CHnFCR_DAT_CNT_SHIFT       24
#define GPAI_CHnFCR_DAT_CNT_MASK        GENMASK(30, 24)
#define GPAI_CHnFCR_UF_STS              BIT(18)
#define GPAI_CHnFCR_OF_STS              BIT(17)
#define GPAI_CHnFCR_DAT_RDY_THD_SHIFT   8
#define GPAI_CHnFCR_DAT_RDY_THD_MASK    GENMASK(15, 8)
#define GPAI_CHnFCR_FLUSH               BIT(0)

#define GPAI_SRC_RX_MAXBURST            1
#define GPAI_DST_RX_MAXBURST 16


#endif /* __HAL_GPPAI_D13X_H__ */