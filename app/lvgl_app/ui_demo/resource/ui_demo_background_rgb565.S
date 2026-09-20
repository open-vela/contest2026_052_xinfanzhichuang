  .section .rodata.ui_demo_background_rgb565, "a", @progbits
  .balign 4
  .global ui_demo_background_rgb565_data
  .type ui_demo_background_rgb565_data, @object
ui_demo_background_rgb565_data:
  .incbin "ui_demo/resource/ui_demo_background_rgb565.bin"
  .size ui_demo_background_rgb565_data, . - ui_demo_background_rgb565_data
