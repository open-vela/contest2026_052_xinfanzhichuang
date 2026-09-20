#include "app_main/lvgl_app.h"
#include "config/lvgl_app_config.h"

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/compiler.h>
#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL
#include <sys/boardctl.h>
#endif

#define LVGL_APP_MAIN_TAG "main"
#define LVGL_APP_AUTOSTART_ARG "autostart"

int main(int argc, char *argv[]);

#ifdef CONFIG_LVGL_APP_APP_MAIN_AUTOSTART
static int lvgl_app_autostart_entry(int argc, char *argv[])
{
  char *child_argv[] =
  {
    "lvgl_app",
    LVGL_APP_AUTOSTART_ARG,
    NULL
  };
  int pid;

  (void)argc;
  (void)argv;

  usleep(CONFIG_LVGL_APP_APP_MAIN_AUTOSTART_DELAY_MS * 1000);

  pid = task_create("lvgl_app",
                    CONFIG_LVGL_APP_APP_MAIN_PRIORITY,
                    CONFIG_LVGL_APP_APP_MAIN_STACKSIZE,
                    main,
                    child_argv);
  if (pid < 0)
    {
      syslog(LOG_ERR, "%s: autostart failed errno=%d\n",
             LVGL_APP_MAIN_TAG, errno);
      return -errno;
    }

  return 0;
}

constructor_fuction static void lvgl_app_autostart_init(void)
{
  char *argv[] =
  {
    "lvgl_app_start",
    NULL
  };

  (void)task_create("lvgl_app_start",
                    CONFIG_LVGL_APP_APP_MAIN_PRIORITY,
                    2048,
                    lvgl_app_autostart_entry,
                    argv);
}
#endif

static void lvgl_app_main_board_init(void)
{
#if defined(CONFIG_LVGL_APP_APP_MAIN_BOARD_INIT) && \
    defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
  (void)boardctl(BOARDIOC_INIT, 0);
#endif
}

static int lvgl_app_entry(int argc, char *argv[])
{
  uint32_t idle_ms;
  int ret;

  (void)argc;
  (void)argv;

  lvgl_app_main_board_init();

  ret = lvgl_app_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: init failed: %d\n", LVGL_APP_MAIN_TAG, ret);
      lvgl_app_shutdown();
      return ret;
    }

  while (true)
    {
      idle_ms = lvgl_app_run_once();
      if (idle_ms < CONFIG_LVGL_APP_APP_MAIN_LOOP_MIN_SLEEP_MS)
        {
          idle_ms = CONFIG_LVGL_APP_APP_MAIN_LOOP_MIN_SLEEP_MS;
        }
      else if (idle_ms > CONFIG_LVGL_APP_APP_MAIN_LOOP_MAX_SLEEP_MS)
        {
          idle_ms = CONFIG_LVGL_APP_APP_MAIN_LOOP_MAX_SLEEP_MS;
        }

      usleep(idle_ms * 1000u);
    }

  return 0;
}

int main(int argc, char *argv[])
{
  return lvgl_app_entry(argc, argv);
}
