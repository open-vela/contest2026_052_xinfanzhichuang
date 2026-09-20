#include "config/lvgl_app_config.h"
#include "ui_demo/inc/ui_demo_bluetooth.h"

#include <nuttx/config.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#ifndef CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_PRIORITY
#  define CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_PRIORITY 100
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_STACKSIZE
#  define CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_STACKSIZE 16384
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DAEMON_STACKSIZE
#  define CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DAEMON_STACKSIZE 16384
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_GATT_STACKSIZE
#  define CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_GATT_STACKSIZE 8192
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DEVICE_NAME
#  define CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DEVICE_NAME "AIC8800-Vela"
#endif

#if defined(CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_BACKEND) || \
    (defined(CONFIG_AIC_BT_CMD) && defined(CONFIG_BLUETOOTH_SERVER))
#  define UI_DEMO_BLUETOOTH_BACKEND_ENABLED 1
#endif

#define UI_DEMO_BLUETOOTH_LOG_TAG "ui_demo_bt"
#define UI_DEMO_BLUETOOTH_DAEMON_SETTLE_US 500000
#define UI_DEMO_BLUETOOTH_DAEMON_WAIT_MS 3000
#define UI_DEMO_BLUETOOTH_GATT_STOP_WAIT_MS 3000
#define UI_DEMO_BLUETOOTH_CMD_WAIT_MS 8000
#define UI_DEMO_BLUETOOTH_WAIT_SLICE_MS 100
#define UI_DEMO_BLUETOOTH_CAPTURE_CHUNK 160
#define UI_DEMO_BLUETOOTH_CAPTURE_LINE 384
#define UI_DEMO_BLUETOOTH_PROC_ROOT "/proc"

#ifdef UI_DEMO_BLUETOOTH_BACKEND_ENABLED
extern int bluetoothd_main(int argc, char *argv[]);
extern int btstart_main(int argc, char *argv[]);
extern int btstop_main(int argc, char *argv[]);
extern int btle_main(int argc, char *argv[]);
#endif

struct ui_demo_bluetooth_context
{
  ui_demo_bluetooth_event_cb_t cb;
  void *user_data;
  bool hci_started;
  bool prestart_in_progress;
  bool start_in_progress;
  bool stop_in_progress;
  bool server_running;
  bool connected;
  enum ui_demo_bluetooth_status status;
  char device_name[UI_DEMO_BLUETOOTH_NAME_LEN];
  char message[UI_DEMO_BLUETOOTH_MESSAGE_LEN];
  int last_error;
  uint32_t generation;
  uint32_t prestart_generation;
  uint32_t start_generation;
  uint32_t stop_generation;
  pid_t daemon_pid;
  pid_t gatt_pid;
};

struct ui_demo_bluetooth_output_arg
{
  uint32_t generation;
  bool stop_output;
};

static pthread_mutex_t g_bluetooth_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ui_demo_bluetooth_context g_bluetooth;

static void ui_demo_bluetooth_copy_text(char *dst, size_t dst_len,
                                        const char *src)
{
  if (dst_len == 0)
    {
      return;
    }

  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  snprintf(dst, dst_len, "%s", src);
}

static void ui_demo_bluetooth_set_message_locked(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(g_bluetooth.message, sizeof(g_bluetooth.message), fmt, ap);
  va_end(ap);
}

static void ui_demo_bluetooth_async_cb(void *user_data)
{
  enum ui_demo_bluetooth_event event =
    (enum ui_demo_bluetooth_event)(uintptr_t)user_data;
  ui_demo_bluetooth_event_cb_t cb;
  void *cb_user_data;

  pthread_mutex_lock(&g_bluetooth_lock);
  cb = g_bluetooth.cb;
  cb_user_data = g_bluetooth.user_data;
  pthread_mutex_unlock(&g_bluetooth_lock);

  if (cb != NULL)
    {
      cb(event, cb_user_data);
    }
}

static void ui_demo_bluetooth_notify(enum ui_demo_bluetooth_event event)
{
  if (lv_async_call(ui_demo_bluetooth_async_cb,
                    (void *)(uintptr_t)event) != LV_RESULT_OK)
    {
      syslog(LOG_WARNING, "%s: async notify failed event=%d\n",
             UI_DEMO_BLUETOOTH_LOG_TAG, event);
    }
}

static bool ui_demo_bluetooth_line_has(const char *line, const char *needle)
{
  return line != NULL && needle != NULL && strstr(line, needle) != NULL;
}

static bool ui_demo_bluetooth_is_pid_dir(const char *name)
{
  if (name == NULL || name[0] == '\0')
    {
      return false;
    }

  for (; *name != '\0'; name++)
    {
      if (*name < '0' || *name > '9')
        {
          return false;
        }
    }

  return true;
}

static bool ui_demo_bluetooth_cmdline_matches(const char *cmdline,
                                              const char *name)
{
  const char *base;
  const char *end;
  size_t name_len;
  size_t base_len;

  if (cmdline == NULL || name == NULL)
    {
      return false;
    }

  while (*cmdline == ' ' || *cmdline == '\t')
    {
      cmdline++;
    }

  base = strrchr(cmdline, '/');
  base = base == NULL ? cmdline : base + 1;
  end = base;
  while (*end != '\0' && *end != ' ' && *end != '\t' &&
         *end != '\r' && *end != '\n')
    {
      end++;
    }

  name_len = strlen(name);
  base_len = (size_t)(end - base);
  return base_len == name_len && strncmp(base, name, name_len) == 0;
}

static int ui_demo_bluetooth_read_cmdline(pid_t pid, char *buf, size_t len)
{
#ifdef CONFIG_FS_PROCFS
  char path[32];
  ssize_t nread;
  int fd;
  size_t i;

  if (buf == NULL || len == 0)
    {
      return -EINVAL;
    }

  snprintf(path, sizeof(path), UI_DEMO_BLUETOOTH_PROC_ROOT "/%d/cmdline",
           (int)pid);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  nread = read(fd, buf, len - 1);
  close(fd);
  if (nread <= 0)
    {
      return nread == 0 ? -ENOENT : -errno;
    }

  for (i = 0; i < (size_t)nread; i++)
    {
      if (buf[i] == '\0')
        {
          buf[i] = ' ';
        }
    }

  buf[nread] = '\0';
  return 0;
#else
  (void)pid;
  (void)buf;
  (void)len;
  return -ENOSYS;
#endif
}

static int ui_demo_bluetooth_count_processes(const char *name,
                                             pid_t *first_pid)
{
#ifdef CONFIG_FS_PROCFS
  DIR *dir;
  struct dirent *entry;
  int count = 0;

  if (first_pid != NULL)
    {
      *first_pid = -1;
    }

  if (name == NULL)
    {
      return -EINVAL;
    }

  dir = opendir(UI_DEMO_BLUETOOTH_PROC_ROOT);
  if (dir == NULL)
    {
      return -errno;
    }

  while ((entry = readdir(dir)) != NULL)
    {
      char cmdline[80];
      pid_t pid;

      if (!ui_demo_bluetooth_is_pid_dir(entry->d_name))
        {
          continue;
        }

      pid = (pid_t)atoi(entry->d_name);
      if (pid <= 0 ||
          ui_demo_bluetooth_read_cmdline(pid, cmdline,
                                         sizeof(cmdline)) < 0)
        {
          continue;
        }

      if (ui_demo_bluetooth_cmdline_matches(cmdline, name))
        {
          if (count == 0 && first_pid != NULL)
            {
              *first_pid = pid;
            }

          count++;
        }
    }

  closedir(dir);
  return count;
#else
  if (first_pid != NULL)
    {
      *first_pid = -1;
    }

  (void)name;
  return -ENOSYS;
#endif
}

static bool ui_demo_bluetooth_line_is_error(const char *line)
{
  return ui_demo_bluetooth_line_has(line, "failed") ||
         ui_demo_bluetooth_line_has(line, "timeout") ||
         ui_demo_bluetooth_line_has(line, "invalid") ||
         ui_demo_bluetooth_line_has(line, "unknown") ||
         ui_demo_bluetooth_line_has(line, "refuse") ||
         ui_demo_bluetooth_line_has(line, "requires") ||
         ui_demo_bluetooth_line_has(line, "returned NULL");
}

static bool ui_demo_bluetooth_apply_output_locked(const char *line,
                                                  bool stop_output)
{
  const bool is_error = ui_demo_bluetooth_line_is_error(line);

  if (stop_output)
    {
      return false;
    }

  if (!is_error && g_bluetooth.status == UI_DEMO_BLUETOOTH_STATUS_ERROR)
    {
      return false;
    }

  if (is_error)
    {
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.last_error = -EIO;
      ui_demo_bluetooth_set_message_locked("失败：%s", line);
      return true;
    }

  if (ui_demo_bluetooth_line_has(line, "gatts connected") ||
      ui_demo_bluetooth_line_has(line, "gattserver connected"))
    {
      g_bluetooth.connected = true;
      g_bluetooth.server_running = true;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_CONNECTED;
      ui_demo_bluetooth_set_message_locked("成功：手机已连接");
      return true;
    }

  if (ui_demo_bluetooth_line_has(line, "gatts disconnected") ||
      ui_demo_bluetooth_line_has(line, "peer disconnected"))
    {
      g_bluetooth.connected = false;
      g_bluetooth.server_running = true;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ADVERTISING;
      ui_demo_bluetooth_set_message_locked(
        "成功：连接已断开，继续等待手机连接");
      return true;
    }

  if (ui_demo_bluetooth_line_has(line, "advertising handle") ||
      ui_demo_bluetooth_line_has(line, "connect now") ||
      ui_demo_bluetooth_line_has(line, "advertising started") ||
      ui_demo_bluetooth_line_has(line,
        "LE_SET_EXT_ADV_ENABLE status=0x00"))
    {
      g_bluetooth.connected = false;
      g_bluetooth.server_running = true;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ADVERTISING;
      ui_demo_bluetooth_set_message_locked(
        "成功：蓝牙已开启，等待手机连接");
      return true;
    }

  if (ui_demo_bluetooth_line_has(line, "start persistent GATT server"))
    {
      g_bluetooth.connected = false;
      g_bluetooth.server_running = true;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_STARTING;
      ui_demo_bluetooth_set_message_locked(
        "正在设置 %s 并开启广播...",
        g_bluetooth.device_name);
      return true;
    }

  return false;
}

static void ui_demo_bluetooth_handle_output_line(const char *line,
                                                 void *user_data)
{
  const struct ui_demo_bluetooth_output_arg *arg = user_data;
  bool notify = false;

  if (line == NULL || line[0] == '\0' || arg == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  if (arg->generation == g_bluetooth.generation)
    {
      notify = ui_demo_bluetooth_apply_output_locked(line, arg->stop_output);
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  if (notify)
    {
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
    }
}

static int ui_demo_bluetooth_wait_pid(pid_t pid, unsigned int wait_ms,
                                      int *exit_code)
{
#ifdef CONFIG_SCHED_WAITPID
  unsigned int waited = 0;
  int status = 0;
  int ret;

  while (waited <= wait_ms)
    {
      ret = waitpid(pid, &status, WNOHANG);
      if (ret == pid)
        {
          int code = 0;

#ifdef WIFEXITED
          if (WIFEXITED(status))
            {
              code = WEXITSTATUS(status);
            }
          else
            {
              code = status == 0 ? 0 : 1;
            }
#else
          code = status == 0 ? 0 : 1;
#endif

          if (exit_code != NULL)
            {
              *exit_code = code;
            }

          return 0;
        }

      if (ret < 0)
        {
          if (errno == ECHILD || errno == ESRCH)
            {
              if (exit_code != NULL)
                {
                  *exit_code = 0;
                }

              return 0;
            }

          return -errno;
        }

      usleep(UI_DEMO_BLUETOOTH_WAIT_SLICE_MS * 1000);
      waited += UI_DEMO_BLUETOOTH_WAIT_SLICE_MS;
    }

  return -ETIMEDOUT;
#else
  (void)pid;
  usleep(wait_ms * 1000);
  if (exit_code != NULL)
    {
      *exit_code = 0;
    }

  return 0;
#endif
}

#if defined(UI_DEMO_BLUETOOTH_BACKEND_ENABLED) && \
    !defined(CONFIG_BUILD_KERNEL)
static int ui_demo_bluetooth_init_spawn_attr(posix_spawnattr_t *attr,
                                             int priority,
                                             int stacksize)
{
  struct sched_param param;
  int ret;

  ret = posix_spawnattr_init(attr);
  if (ret != 0)
    {
      return -ret;
    }

  memset(&param, 0, sizeof(param));
  param.sched_priority = priority;
  ret = posix_spawnattr_setschedparam(attr, &param);
  if (ret != 0)
    {
      posix_spawnattr_destroy(attr);
      return -ret;
    }

  ret = posix_spawnattr_setstacksize(attr, stacksize);
  if (ret != 0)
    {
      posix_spawnattr_destroy(attr);
      return -ret;
    }

#if defined(CONFIG_RR_INTERVAL) && CONFIG_RR_INTERVAL > 0
  ret = posix_spawnattr_setschedpolicy(attr, SCHED_RR);
  if (ret != 0)
    {
      posix_spawnattr_destroy(attr);
      return -ret;
    }

  ret = posix_spawnattr_setflags(attr,
                                 POSIX_SPAWN_SETSCHEDPARAM |
                                 POSIX_SPAWN_SETSCHEDULER);
#else
  ret = posix_spawnattr_setflags(attr, POSIX_SPAWN_SETSCHEDPARAM);
#endif
  if (ret != 0)
    {
      posix_spawnattr_destroy(attr);
      return -ret;
    }

  return 0;
}

static int ui_demo_bluetooth_btle_unbuffered_main(int argc, char *argv[])
{
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  return btle_main(argc, argv);
}

static int ui_demo_bluetooth_btstop_unbuffered_main(int argc, char *argv[])
{
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  return btstop_main(argc, argv);
}

static int ui_demo_bluetooth_spawn_daemon(void)
{
  posix_spawnattr_t attr;
  pid_t pid;
  int ret;

  ret = ui_demo_bluetooth_init_spawn_attr(
    &attr,
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_PRIORITY,
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DAEMON_STACKSIZE);
  if (ret < 0)
    {
      return ret;
    }

  pid = task_spawn("bluetoothd", bluetoothd_main, NULL, &attr, NULL, NULL);
  posix_spawnattr_destroy(&attr);
  if (pid < 0)
    {
      return pid;
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  g_bluetooth.daemon_pid = pid;
  pthread_mutex_unlock(&g_bluetooth_lock);

  return 0;
}

static int ui_demo_bluetooth_spawn_with_pipe(
  const char *name,
  main_t entry,
  char * const argv[],
  int stacksize,
  pid_t *pid_out,
  int *read_fd_out)
{
  posix_spawn_file_actions_t file_actions;
  posix_spawnattr_t attr;
  int pipefd[2];
  pid_t pid;
  int ret;

  if (pid_out == NULL || read_fd_out == NULL)
    {
      return -EINVAL;
    }

  ret = pipe(pipefd);
  if (ret < 0)
    {
      return -errno;
    }

  ret = posix_spawn_file_actions_init(&file_actions);
  if (ret != 0)
    {
      close(pipefd[0]);
      close(pipefd[1]);
      return -ret;
    }

  ret = posix_spawn_file_actions_adddup2(&file_actions, pipefd[1],
                                         STDOUT_FILENO);
  if (ret == 0)
    {
      ret = posix_spawn_file_actions_adddup2(&file_actions, pipefd[1],
                                             STDERR_FILENO);
    }

  if (ret == 0)
    {
      ret = posix_spawn_file_actions_addclose(&file_actions, pipefd[0]);
    }

  if (ret == 0)
    {
      ret = posix_spawn_file_actions_addclose(&file_actions, pipefd[1]);
    }

  if (ret != 0)
    {
      posix_spawn_file_actions_destroy(&file_actions);
      close(pipefd[0]);
      close(pipefd[1]);
      return -ret;
    }

  ret = ui_demo_bluetooth_init_spawn_attr(
    &attr,
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_PRIORITY,
    stacksize);
  if (ret < 0)
    {
      posix_spawn_file_actions_destroy(&file_actions);
      close(pipefd[0]);
      close(pipefd[1]);
      return ret;
    }

  pid = task_spawn(name, entry, &file_actions, &attr, argv, NULL);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&file_actions);
  close(pipefd[1]);
  if (pid < 0)
    {
      close(pipefd[0]);
      return pid;
    }

  *pid_out = pid;
  *read_fd_out = pipefd[0];
  return 0;
}

static int ui_demo_bluetooth_read_capture(
  int read_fd,
  pid_t pid,
  void (*line_cb)(const char *line, void *user_data),
  void *user_data)
{
  struct pollfd pfd;
  char chunk[UI_DEMO_BLUETOOTH_CAPTURE_CHUNK];
  char line[UI_DEMO_BLUETOOTH_CAPTURE_LINE];
  size_t line_len = 0;
  int ret = 0;
  int exit_code = 0;

  pfd.fd = read_fd;
  pfd.events = POLLIN | POLLHUP;
  pfd.revents = 0;

  for (;;)
    {
      ssize_t nread;
      size_t i;

      ret = poll(&pfd, 1, -1);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          ret = -errno;
          break;
        }

      if ((pfd.revents & POLLIN) == 0 &&
          (pfd.revents & POLLHUP) != 0)
        {
          ret = 0;
          break;
        }

      nread = read(read_fd, chunk, sizeof(chunk));
      if (nread < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          ret = -errno;
          break;
        }

      if (nread == 0)
        {
          ret = 0;
          break;
        }

      for (i = 0; i < (size_t)nread; i++)
        {
          char ch = chunk[i];

          if (ch == '\r')
            {
              continue;
            }

          if (ch == '\n')
            {
              line[line_len] = '\0';
              if (line_cb != NULL)
                {
                  line_cb(line, user_data);
                }

              line_len = 0;
              continue;
            }

          if (line_len + 1 < sizeof(line))
            {
              line[line_len++] = ch;
            }
          else
            {
              line_len = 0;
            }
        }
    }

  if (line_len > 0 && line_cb != NULL)
    {
      line[line_len] = '\0';
      line_cb(line, user_data);
    }

  close(read_fd);
  if (ret < 0)
    {
      (void)ui_demo_bluetooth_wait_pid(pid,
                                       UI_DEMO_BLUETOOTH_WAIT_SLICE_MS,
                                       NULL);
      return ret;
    }

  ret = ui_demo_bluetooth_wait_pid(pid, UI_DEMO_BLUETOOTH_CMD_WAIT_MS,
                                   &exit_code);
  if (ret < 0)
    {
      return ret;
    }

  return exit_code == 0 ? 0 : -EIO;
}
#else
static int ui_demo_bluetooth_spawn_daemon(void)
{
  return -ENOSYS;
}

static int ui_demo_bluetooth_spawn_with_pipe(
  const char *name,
  main_t entry,
  char * const argv[],
  int stacksize,
  pid_t *pid_out,
  int *read_fd_out)
{
  (void)name;
  (void)entry;
  (void)argv;
  (void)stacksize;
  (void)pid_out;
  (void)read_fd_out;
  return -ENOSYS;
}

static int ui_demo_bluetooth_read_capture(
  int read_fd,
  pid_t pid,
  void (*line_cb)(const char *line, void *user_data),
  void *user_data)
{
  (void)read_fd;
  (void)pid;
  (void)line_cb;
  (void)user_data;
  return -ENOSYS;
}
#endif

#ifdef UI_DEMO_BLUETOOTH_BACKEND_ENABLED
static int ui_demo_bluetooth_start_hci(void)
{
  char *start_argv[] =
  {
    "btstart",
    NULL
  };
  int ret;

  ret = btstart_main(1, start_argv);
  return ret == 0 ? 0 : (ret < 0 ? ret : -EIO);
}

static int ui_demo_bluetooth_wait_hci_ready(void)
{
  unsigned int waited = 0;
  pid_t daemon_pid = -1;
  int daemon_count;

  for (;;)
    {
      bool hci_started;
      bool prestart_in_progress;

      pthread_mutex_lock(&g_bluetooth_lock);
      hci_started = g_bluetooth.hci_started;
      prestart_in_progress = g_bluetooth.prestart_in_progress;
      pthread_mutex_unlock(&g_bluetooth_lock);

      if (hci_started)
        {
          return 0;
        }

      if (!prestart_in_progress)
        {
          break;
        }

      if (waited >= UI_DEMO_BLUETOOTH_CMD_WAIT_MS)
        {
          return -ETIMEDOUT;
        }

      usleep(UI_DEMO_BLUETOOTH_WAIT_SLICE_MS * 1000);
      waited += UI_DEMO_BLUETOOTH_WAIT_SLICE_MS;
    }

  daemon_count = ui_demo_bluetooth_count_processes("bluetoothd",
                                                   &daemon_pid);
  if (daemon_count > 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      g_bluetooth.hci_started = true;
      g_bluetooth.daemon_pid = daemon_pid;
      pthread_mutex_unlock(&g_bluetooth_lock);

      if (daemon_count > 1)
        {
          syslog(LOG_WARNING,
                 "%s: reuse existing duplicate bluetoothd instances: %d\n",
                 UI_DEMO_BLUETOOTH_LOG_TAG, daemon_count);
        }

      return 0;
    }

  return ui_demo_bluetooth_start_hci();
}

static int ui_demo_bluetooth_ensure_daemon(void)
{
  int ret;
  bool need_spawn;
  pid_t daemon_pid = -1;
  int daemon_count;

  ret = ui_demo_bluetooth_wait_hci_ready();
  if (ret < 0)
    {
      return ret;
    }

  daemon_count = ui_demo_bluetooth_count_processes("bluetoothd",
                                                   &daemon_pid);
  pthread_mutex_lock(&g_bluetooth_lock);
  g_bluetooth.hci_started = true;
  if (daemon_count > 0)
    {
      g_bluetooth.daemon_pid = daemon_pid;
      need_spawn = false;
    }
  else if (daemon_count == 0)
    {
      g_bluetooth.daemon_pid = -1;
      need_spawn = true;
    }
  else
    {
      need_spawn = g_bluetooth.daemon_pid <= 0;
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  if (daemon_count > 1)
    {
      syslog(LOG_WARNING,
             "%s: bluetoothd already has %d instances; reuse without spawn\n",
             UI_DEMO_BLUETOOTH_LOG_TAG, daemon_count);
    }

  if (!need_spawn)
    {
      usleep(UI_DEMO_BLUETOOTH_DAEMON_SETTLE_US);
      return 0;
    }

  ret = ui_demo_bluetooth_spawn_daemon();
  if (ret < 0)
    {
      return ret;
    }

  usleep(UI_DEMO_BLUETOOTH_DAEMON_SETTLE_US);
  return 0;
}

static int ui_demo_bluetooth_run_gattserver(uint32_t generation)
{
  struct ui_demo_bluetooth_output_arg output_arg;
  char name[UI_DEMO_BLUETOOTH_NAME_LEN];
  char notify_arg[] = "notify=0";
  char *gatt_argv[] =
  {
    "gattserver",
    name,
    notify_arg,
    NULL
  };
  pid_t pid;
  int read_fd;
  int ret;

  pthread_mutex_lock(&g_bluetooth_lock);
  ui_demo_bluetooth_copy_text(name, sizeof(name), g_bluetooth.device_name);
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_spawn_with_pipe(
    "btle",
    ui_demo_bluetooth_btle_unbuffered_main,
    gatt_argv,
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_GATT_STACKSIZE,
    &pid,
    &read_fd);
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  if (generation == g_bluetooth.generation)
    {
      g_bluetooth.gatt_pid = pid;
      g_bluetooth.server_running = true;
      g_bluetooth.connected = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_STARTING;
      ui_demo_bluetooth_set_message_locked(
        "正在设置 %s 并等待连接",
        name);
    }
  pthread_mutex_unlock(&g_bluetooth_lock);
  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_SERVER_STARTED);

  output_arg.generation = generation;
  output_arg.stop_output = false;
  ret = ui_demo_bluetooth_read_capture(
    read_fd,
    pid,
    ui_demo_bluetooth_handle_output_line,
    &output_arg);

  pthread_mutex_lock(&g_bluetooth_lock);
  if (generation == g_bluetooth.generation)
    {
      g_bluetooth.gatt_pid = -1;
      g_bluetooth.server_running = false;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.connected = false;

      if (ret < 0)
        {
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
          g_bluetooth.last_error = ret;
          if (g_bluetooth.message[0] == '\0')
            {
              ui_demo_bluetooth_set_message_locked(
                "失败：btle gattserver 退出，错误码 %d", ret);
            }
        }
      else if (!g_bluetooth.stop_in_progress)
        {
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_READY;
          ui_demo_bluetooth_set_message_locked("蓝牙服务已停止");
        }
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  return ret;
}

static int ui_demo_bluetooth_stop_gatt(void)
{
  pid_t pid;
  int ret = 0;

  pthread_mutex_lock(&g_bluetooth_lock);
  pid = g_bluetooth.gatt_pid;
  pthread_mutex_unlock(&g_bluetooth_lock);

  if (pid <= 0)
    {
      return 0;
    }

  if (kill(pid, SIGINT) < 0 && errno != ESRCH)
    {
      ret = -errno;
    }

  if (ret == 0)
    {
      ret = ui_demo_bluetooth_wait_pid(pid,
                                       UI_DEMO_BLUETOOTH_GATT_STOP_WAIT_MS,
                                       NULL);
    }

  if (ret == -ETIMEDOUT)
    {
      (void)kill(pid, SIGTERM);
      (void)ui_demo_bluetooth_wait_pid(pid,
                                       UI_DEMO_BLUETOOTH_WAIT_SLICE_MS,
                                       NULL);
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  if (g_bluetooth.gatt_pid == pid)
    {
      g_bluetooth.gatt_pid = -1;
      g_bluetooth.server_running = false;
      g_bluetooth.connected = false;
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  return ret == -ETIMEDOUT ? 0 : ret;
}

static int ui_demo_bluetooth_run_btstop(uint32_t generation)
{
  struct ui_demo_bluetooth_output_arg output_arg;
  pid_t pid;
  int read_fd;
  int ret;

  ret = ui_demo_bluetooth_spawn_with_pipe(
    "btstop",
    ui_demo_bluetooth_btstop_unbuffered_main,
    NULL,
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_STACKSIZE,
    &pid,
    &read_fd);
  if (ret < 0)
    {
      return ret;
    }

  output_arg.generation = generation;
  output_arg.stop_output = true;
  return ui_demo_bluetooth_read_capture(
    read_fd,
    pid,
    ui_demo_bluetooth_handle_output_line,
    &output_arg);
}
#else
static int ui_demo_bluetooth_start_hci(void)
{
  return -ENOSYS;
}

static int ui_demo_bluetooth_ensure_daemon(void)
{
  return -ENOSYS;
}

static int ui_demo_bluetooth_run_gattserver(uint32_t generation)
{
  (void)generation;
  return -ENOSYS;
}

static int ui_demo_bluetooth_stop_gatt(void)
{
  return 0;
}

static int ui_demo_bluetooth_run_btstop(uint32_t generation)
{
  (void)generation;
  return -ENOSYS;
}
#endif

static int ui_demo_bluetooth_create_task(
  const char *name,
  int (*entry)(int argc, char *argv[]))
{
  char *task_argv[] =
  {
    (char *)name,
    NULL
  };
  int pid;

  pid = task_create(name,
                    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_PRIORITY,
                    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_TASK_STACKSIZE,
                    entry,
                    task_argv);
  if (pid < 0)
    {
      return errno == 0 ? -EIO : -errno;
    }

  return 0;
}

static int ui_demo_bluetooth_prestart_entry(int argc, char *argv[])
{
  uint32_t generation;
  int ret;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_bluetooth_lock);
  generation = g_bluetooth.prestart_generation;
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_start_hci();

  pthread_mutex_lock(&g_bluetooth_lock);
  if (generation == g_bluetooth.prestart_generation)
    {
      g_bluetooth.prestart_in_progress = false;
      if (ret == 0)
        {
          g_bluetooth.hci_started = true;
          g_bluetooth.last_error = 0;
          if (!g_bluetooth.server_running &&
              !g_bluetooth.start_in_progress &&
              !g_bluetooth.stop_in_progress)
            {
              g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_READY;
              ui_demo_bluetooth_set_message_locked(
                "成功：btstart 已完成，点击蓝牙开始等待连接");
            }
        }
      else
        {
          g_bluetooth.hci_started = false;
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
          g_bluetooth.last_error = ret;
          ui_demo_bluetooth_set_message_locked(
            "失败：btstart 失败，错误码 %d", ret);
        }
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_PRESTART_FINISHED);
  return ret;
}

static int ui_demo_bluetooth_start_entry(int argc, char *argv[])
{
  uint32_t generation;
  int ret;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_bluetooth_lock);
  generation = g_bluetooth.start_generation;
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_ensure_daemon();
  if (ret < 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      if (generation == g_bluetooth.generation)
        {
          g_bluetooth.start_in_progress = false;
          g_bluetooth.server_running = false;
          g_bluetooth.connected = false;
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
          g_bluetooth.last_error = ret;
          ui_demo_bluetooth_set_message_locked(
            ret == -ENOSYS ?
            "失败：当前固件未启用蓝牙支持" :
            "失败：bluetoothd 启动失败，错误码 %d",
            ret);
        }
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return ret;
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  if (generation == g_bluetooth.generation)
    {
      g_bluetooth.hci_started = true;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_STARTING;
      ui_demo_bluetooth_set_message_locked(
        "bluetoothd 已就绪，正在设置 %s...",
        g_bluetooth.device_name);
    }
  pthread_mutex_unlock(&g_bluetooth_lock);
  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);

  ret = ui_demo_bluetooth_run_gattserver(generation);

  if (ret < 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      if (generation == g_bluetooth.generation &&
          g_bluetooth.status != UI_DEMO_BLUETOOTH_STATUS_ERROR)
        {
          g_bluetooth.start_in_progress = false;
          g_bluetooth.server_running = false;
          g_bluetooth.connected = false;
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
          g_bluetooth.last_error = ret;
          ui_demo_bluetooth_set_message_locked(
            "失败：btle gattserver 启动失败，错误码 %d", ret);
        }
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
    }

  return ret;
}

static int ui_demo_bluetooth_stop_entry(int argc, char *argv[])
{
  uint32_t generation;
  int ret = 0;
  pid_t daemon_pid = -1;
  int daemon_count;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_bluetooth_lock);
  generation = g_bluetooth.stop_generation;
  pthread_mutex_unlock(&g_bluetooth_lock);

  (void)ui_demo_bluetooth_stop_gatt();
  ret = ui_demo_bluetooth_run_btstop(generation);
  daemon_count = ui_demo_bluetooth_count_processes("bluetoothd",
                                                   &daemon_pid);

  pthread_mutex_lock(&g_bluetooth_lock);
  if (generation == g_bluetooth.generation)
    {
      g_bluetooth.stop_in_progress = false;
      g_bluetooth.start_in_progress = false;
      g_bluetooth.server_running = false;
      g_bluetooth.connected = false;

      if (ret == 0)
        {
          g_bluetooth.hci_started = false;
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_OFF;
          g_bluetooth.last_error = 0;
          g_bluetooth.daemon_pid = -1;
          g_bluetooth.gatt_pid = -1;
          ui_demo_bluetooth_set_message_locked("成功：蓝牙已关闭");
        }
      else if (daemon_count > 0)
        {
          ret = 0;
          g_bluetooth.hci_started = true;
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_OFF;
          g_bluetooth.last_error = 0;
          g_bluetooth.daemon_pid = daemon_pid;
          g_bluetooth.gatt_pid = -1;
          ui_demo_bluetooth_set_message_locked("成功：蓝牙已关闭");
        }
      else
        {
          g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
          g_bluetooth.last_error = ret;
          if (g_bluetooth.message[0] == '\0')
            {
              ui_demo_bluetooth_set_message_locked(
                ret == -ENOSYS ?
                "失败：当前固件未启用蓝牙支持" :
                "失败：btstop 失败，错误码 %d",
                ret);
            }
        }
    }
  pthread_mutex_unlock(&g_bluetooth_lock);

  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STOP_FINISHED);
  return ret;
}

void ui_demo_bluetooth_init(ui_demo_bluetooth_event_cb_t cb,
                            void *user_data)
{
  pthread_mutex_lock(&g_bluetooth_lock);
  memset(&g_bluetooth, 0, sizeof(g_bluetooth));
  g_bluetooth.cb = cb;
  g_bluetooth.user_data = user_data;
  g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_OFF;
  g_bluetooth.daemon_pid = -1;
  g_bluetooth.gatt_pid = -1;
  ui_demo_bluetooth_copy_text(
    g_bluetooth.device_name,
    sizeof(g_bluetooth.device_name),
    CONFIG_LVGL_APP_UI_DEMO_BLUETOOTH_DEVICE_NAME);
  ui_demo_bluetooth_set_message_locked(
#ifdef UI_DEMO_BLUETOOTH_BACKEND_ENABLED
    "蓝牙底层未启动"
#else
    "失败：当前固件未启用蓝牙支持"
#endif
  );
  pthread_mutex_unlock(&g_bluetooth_lock);
}

void ui_demo_bluetooth_deinit(void)
{
  pthread_mutex_lock(&g_bluetooth_lock);
  g_bluetooth.cb = NULL;
  g_bluetooth.user_data = NULL;
  pthread_mutex_unlock(&g_bluetooth_lock);
}

void ui_demo_bluetooth_get_snapshot(
  struct ui_demo_bluetooth_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_bluetooth_lock);
  snapshot->hci_started = g_bluetooth.hci_started;
  snapshot->prestart_in_progress = g_bluetooth.prestart_in_progress;
  snapshot->start_in_progress = g_bluetooth.start_in_progress;
  snapshot->stop_in_progress = g_bluetooth.stop_in_progress;
  snapshot->server_running = g_bluetooth.server_running;
  snapshot->connected = g_bluetooth.connected;
  snapshot->status = g_bluetooth.status;
  snapshot->last_error = g_bluetooth.last_error;
  ui_demo_bluetooth_copy_text(snapshot->device_name,
                              sizeof(snapshot->device_name),
                              g_bluetooth.device_name);
  ui_demo_bluetooth_copy_text(snapshot->message, sizeof(snapshot->message),
                              g_bluetooth.message);
  pthread_mutex_unlock(&g_bluetooth_lock);
}

int ui_demo_bluetooth_prestart(void)
{
  int ret;

  pthread_mutex_lock(&g_bluetooth_lock);
  if (g_bluetooth.hci_started || g_bluetooth.prestart_in_progress)
    {
      pthread_mutex_unlock(&g_bluetooth_lock);
      return 0;
    }

  g_bluetooth.prestart_generation++;
  g_bluetooth.prestart_in_progress = true;
  g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_PRESTARTING;
  ui_demo_bluetooth_set_message_locked("正在执行 btstart...");
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_create_task("ui_bt_prestart",
                                      ui_demo_bluetooth_prestart_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      g_bluetooth.prestart_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
      g_bluetooth.last_error = ret;
      ui_demo_bluetooth_set_message_locked("失败：创建 btstart 任务失败：%d",
                                           ret);
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_PRESTART_FINISHED);
      return ret;
    }

  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
  return 0;
}

int ui_demo_bluetooth_start_server(void)
{
  int ret;

  pthread_mutex_lock(&g_bluetooth_lock);
  if (g_bluetooth.stop_in_progress)
    {
      ui_demo_bluetooth_set_message_locked("蓝牙正在关闭，请稍候");
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return -EBUSY;
    }

  if (g_bluetooth.server_running || g_bluetooth.connected)
    {
      ui_demo_bluetooth_set_message_locked(
        g_bluetooth.connected ?
        "蓝牙已连接" :
        "蓝牙已在等待连接");
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return 0;
    }

  if (g_bluetooth.start_in_progress)
    {
      ui_demo_bluetooth_set_message_locked("蓝牙正在打开，请稍候");
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  g_bluetooth.generation++;
  g_bluetooth.start_generation = g_bluetooth.generation;
  g_bluetooth.start_in_progress = true;
  g_bluetooth.connected = false;
  g_bluetooth.server_running = false;
  g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_STARTING;
  g_bluetooth.last_error = 0;
  ui_demo_bluetooth_set_message_locked(
    "正在启动 bluetoothd 并设置 %s...",
    g_bluetooth.device_name);
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_create_task("ui_bt_start",
                                      ui_demo_bluetooth_start_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      g_bluetooth.start_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
      g_bluetooth.last_error = ret;
      ui_demo_bluetooth_set_message_locked("失败：创建蓝牙打开任务失败：%d",
                                           ret);
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return ret;
    }

  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_SERVER_STARTED);
  return 0;
}

int ui_demo_bluetooth_stop(void)
{
  int ret;

  pthread_mutex_lock(&g_bluetooth_lock);
  if (g_bluetooth.stop_in_progress)
    {
      ui_demo_bluetooth_set_message_locked("正在关闭蓝牙，请稍候");
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  g_bluetooth.generation++;
  g_bluetooth.stop_generation = g_bluetooth.generation;
  g_bluetooth.start_in_progress = false;
  g_bluetooth.stop_in_progress = true;
  g_bluetooth.connected = false;
  g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_STOPPING;
  g_bluetooth.last_error = 0;
  ui_demo_bluetooth_set_message_locked("正在执行 btstop...");
  pthread_mutex_unlock(&g_bluetooth_lock);

  ret = ui_demo_bluetooth_create_task("ui_bt_stop",
                                      ui_demo_bluetooth_stop_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_bluetooth_lock);
      g_bluetooth.stop_in_progress = false;
      g_bluetooth.status = UI_DEMO_BLUETOOTH_STATUS_ERROR;
      g_bluetooth.last_error = ret;
      ui_demo_bluetooth_set_message_locked("失败：创建蓝牙关闭任务失败：%d",
                                           ret);
      pthread_mutex_unlock(&g_bluetooth_lock);
      ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STOP_FINISHED);
      return ret;
    }

  ui_demo_bluetooth_notify(UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED);
  return 0;
}
