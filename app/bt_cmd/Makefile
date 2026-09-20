include $(APPDIR)/Make.defs

AIC_BT8858A_BLE_DIR := $(APPDIR)/../contest2026_052_xinfanzhichuang/chips/artinchips/peripheral/bt/bt8858a
AIC_BT8858A_VELA_BLE_DIR := $(AIC_BT8858A_BLE_DIR)/bt_stack/vela_ble

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/connectivity/bluetooth/framework/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/connectivity/bluetooth/framework/common
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/system/libuv/libuv/include
CFLAGS += ${INCDIR_PREFIX}$(AIC_BT8858A_VELA_BLE_DIR)/common
CFLAGS += ${INCDIR_PREFIX}$(AIC_BT8858A_VELA_BLE_DIR)/gap
CFLAGS += ${INCDIR_PREFIX}$(AIC_BT8858A_VELA_BLE_DIR)/gatt

CSRCS += $(AIC_BT8858A_VELA_BLE_DIR)/common/aic_ble_common.c
CSRCS += $(AIC_BT8858A_VELA_BLE_DIR)/gap/aic_ble_gap.c
CSRCS += $(AIC_BT8858A_VELA_BLE_DIR)/gatt/aic_ble_gatt_lifecycle.c

PROGNAME  += btstart
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 8192
MAINSRC   += btstart.c

PROGNAME  += btstop
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 4096
MAINSRC   += btstop.c

PROGNAME  += bthci
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 8192
MAINSRC   += bthci.c

PROGNAME  += btle
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 8192
MAINSRC   += btle.c

include $(APPDIR)/Application.mk
