
############################################################################
# app/lvgl_app/Makefile
#
# Minimal LVGL application shell built as a NuttX application.
############################################################################
include $(APPDIR)/Make.defs
LVGL_APP_ROOT = $(APPDIR)/../contest2026_052_xinfanzhichuang/app/lvgl_app
PROGNAME  = lvgl_app
PRIORITY  = $(CONFIG_LVGL_APP_APP_MAIN_PRIORITY)
STACKSIZE = $(CONFIG_LVGL_APP_APP_MAIN_STACKSIZE)
MODULE    = $(CONFIG_LVGL_APP)
MAINSRC = lvgl_app_main.c
CFLAGS += ${INCDIR_PREFIX}$(LVGL_APP_ROOT)
CFLAGS += ${INCDIR_PREFIX}$(LVGL_APP_ROOT)/ui_demo/inc
CFLAGS += ${INCDIR_PREFIX}$(LVGL_APP_ROOT)/velaclaw/include
CFLAGS += ${INCDIR_PREFIX}$(LVGL_APP_ROOT)/velaclaw/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
VPATH += :app_main
CSRCS = lvgl_app.c
CSRCS += lvgl_app_backlight.c
CSRCS += lvgl_app_fbdev_compat.c
ifeq ($(CONFIG_LVGL_APP_UI_DEMO),y)
VPATH += :ui_demo/src
VPATH += :ui_demo/resource
CSRCS += ui_demo.c
CSRCS += ui_demo_ai.c
CSRCS += ui_demo_netinfo.c
CSRCS += ui_demo_wifi.c
CSRCS += ui_demo_bluetooth.c
CSRCS += ui_demo_font_16.c
endif
include $(APPDIR)/Application.mk
