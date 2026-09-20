# Contest 2026 team 000 board - src
nuttx_add_library(board)
target_sources(board PRIVATE board_boot.c)
if(CONFIG_AIC_EXBT)
  target_sources(board PRIVATE board_exbt.c)
endif()
