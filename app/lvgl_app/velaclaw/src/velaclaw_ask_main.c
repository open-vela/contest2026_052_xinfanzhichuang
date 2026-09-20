#include "bus/message_bus.h"
#include "velaclaw_config.h"
#include "velaclaw_service.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define ASK_WAIT_READY_RETRIES 90
#define ASK_WAIT_READY_US 500000

static void ask_join_args(int argc, char *argv[], char *out, size_t out_size)
{
  int i;

  if (out_size == 0)
    {
      return;
    }

  out[0] = '\0';
  for (i = 1; i < argc; i++)
    {
      size_t used = strlen(out);

      if (used + 1 >= out_size)
        {
          break;
        }

      if (i > 1)
        {
          strncat(out, " ", out_size - used - 1);
          used = strlen(out);
        }

      strncat(out, argv[i], out_size - used - 1);
    }
}

static int ask_wait_agent_ready(void)
{
  struct velaclaw_service_snapshot snapshot;
  int i;
  int ret;

  velaclaw_service_get_snapshot(&snapshot);
  if (snapshot.status == VELACLAW_SERVICE_STATUS_READY)
    {
      return OK;
    }

  ret = velaclaw_service_start();
  if (ret < 0)
    {
      printf("AI 启动失败：%s\n",
             snapshot.message[0] == '\0' ? "创建任务失败" :
             snapshot.message);
      return ret;
    }

  for (i = 0; i < ASK_WAIT_READY_RETRIES; i++)
    {
      velaclaw_service_get_snapshot(&snapshot);
      if (snapshot.status == VELACLAW_SERVICE_STATUS_READY)
        {
          return OK;
        }

      if (snapshot.status == VELACLAW_SERVICE_STATUS_ERROR)
        {
          printf("%s\n", snapshot.message[0] == '\0' ?
                 "AI 连接失败" : snapshot.message);
          return -ENETDOWN;
        }

      usleep(ASK_WAIT_READY_US);
    }

  velaclaw_service_get_snapshot(&snapshot);
  printf("%s\n", snapshot.message[0] == '\0' ?
         "AI 正在启动，请稍后再试" : snapshot.message);
  return -ETIMEDOUT;
}

int ask_main(int argc, char *argv[])
{
  velaclaw_msg_t msg;
  char content[512];
  int ret;

  if (argc < 2)
    {
      printf("Usage: ask <message>\n");
      return 1;
    }

  ask_join_args(argc, argv, content, sizeof(content));
  if (content[0] == '\0')
    {
      printf("Usage: ask <message>\n");
      return 1;
    }

  ret = ask_wait_agent_ready();
  if (ret != OK)
    {
      return 1;
    }

  memset(&msg, 0, sizeof(msg));
  snprintf(msg.channel, sizeof(msg.channel), "%s", VELACLAW_CHAN_CLI);
  snprintf(msg.chat_id, sizeof(msg.chat_id), "console");
  msg.content = strdup(content);
  if (msg.content == NULL)
    {
      printf("ask: out of memory\n");
      return 1;
    }

  if (message_bus_push_inbound(&msg) != OK)
    {
      free(msg.content);
      printf("ask: Agent 消息队列不可用\n");
      return 1;
    }

  printf("Sent to agent: %s\n", content);
  return 0;
}
