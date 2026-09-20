
# LVGL Application
This directory contains the `lvgl_app` NuttX application entry, LVGL/NuttX
display initialization, and the optional quick-control panel Demo module.
The Demo business code is isolated in `ui_demo/`:
- `ui_demo/inc/`: public initialization and data refresh APIs.
- `ui_demo/src/`: LVGL page layout, static placeholder data, and callbacks.
- `ui_demo/resource/`: reserved icons, fonts, and future image assets.
- `ui_demo/Kconfig`: layered feature switches for the Demo.
`app_main/` owns only application startup, display initialization, and the
main LVGL timer loop.

