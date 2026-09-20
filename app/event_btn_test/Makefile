include $(APPDIR)/Make.defs

# Touchscreen test application

PROGNAME = btn_test
PRIORITY = 100
STACKSIZE = 2048
MODULE = $(CONFIG_AIC_EVENT_BTN_TEST)

MAINSRC = aic_gpio_button_test.c

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/contest2026_052_xinfanzhichuang/chips/artinchips/drv/gpio
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/contest2026_052_xinfanzhichuang/artinchips/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/contest2026_052_xinfanzhichuang/artinchips/include/hal
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/contest2026_052_xinfanzhichuang/chips/artinchips/common/include

include $(APPDIR)/Application.mk
