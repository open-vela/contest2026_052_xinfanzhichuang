/****************************************************************************
 * AIC8800D40L Bluetooth HCI transport for openVela/Zblue.
 ****************************************************************************/

#ifndef __AIC_BT_CONTROLLER_HCI_H4_AIC_BT_HCI_H
#define __AIC_BT_CONTROLLER_HCI_H4_AIC_BT_HCI_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int aic_bt_vela_hci_start(void);
int aic_bt_vela_hci_stop(void);
int aic_bt_vela_hci_is_registered(void);

#endif /* __AIC_BT_CONTROLLER_HCI_H4_AIC_BT_HCI_H */
