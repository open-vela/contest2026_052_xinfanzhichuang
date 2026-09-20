#ifndef WIFI_CMD_WORKER_H
#define WIFI_CMD_WORKER_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>

typedef int (*wifi_cmd_worker_fn_t)(void *arg);

struct wifi_cmd_worker_job
{
  wifi_cmd_worker_fn_t fn;
  void *arg;
  sem_t done;
  int ret;
};

static int wifi_cmd_worker_entry(int argc, char *argv[])
{
  const char *arg = NULL;
  struct wifi_cmd_worker_job *job;

  if (argc > 1)
    {
      arg = argv[1];
    }
  else if (argc > 0)
    {
      arg = argv[0];
    }

  job = (struct wifi_cmd_worker_job *)(uintptr_t)strtoul(arg ? arg : "0",
                                                        NULL, 0);
  if (job && job->fn)
    {
      job->ret = job->fn(job->arg);
      nxsem_post(&job->done);
    }

  return 0;
}

static int wifi_cmd_run_worker(const char *name, int stack_size,
                               wifi_cmd_worker_fn_t fn, void *arg)
{
  struct wifi_cmd_worker_job job;
  char argbuf[16];
  char *argv[2];
  int pid;
  int ret;

  if (!fn)
    {
      return -EINVAL;
    }

  job.fn = fn;
  job.arg = arg;
  job.ret = -EIO;
  nxsem_init(&job.done, 0, 0);

  snprintf(argbuf, sizeof(argbuf), "%p", &job);
  argv[0] = argbuf;
  argv[1] = NULL;

  pid = kthread_create(name ? name : "wifi_cmd", 100, stack_size,
                       wifi_cmd_worker_entry, argv);
  if (pid < 0)
    {
      nxsem_destroy(&job.done);
      printf("%s: kthread_create failed: %d\n",
             name ? name : "wifi_cmd", pid);
      return pid;
    }

  do
    {
      ret = nxsem_wait(&job.done);
    }
  while (ret < 0 && get_errno() == EINTR);

  nxsem_destroy(&job.done);
  return ret < 0 ? ret : job.ret;
}

#endif
