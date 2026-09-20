/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Raw PCM playback through NxPlayer after the complete TTS response arrives. */

#include "voice/audio_playback.h"
#include "velaclaw_config.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifdef CONFIG_SYSTEM_NXPLAYER
#include <nuttx/audio/audio.h>
#include <system/nxplayer.h>
#else
#include <media_player.h>
#endif

static const char *TAG = "audio_pb";

#define PB_TMP_TEMPLATE VELACLAW_DATA_DIR "/.tts_XXXXXX"
#define PB_IDLE_MIN_WAIT_MS 7000
#define PB_IDLE_EXTRA_MS 2500
#define AUDIO_PB_NXPLAYER_STATE_IDLE 0

struct audio_playback
{
  int tmp_fd;
  size_t total_written;
  unsigned int sample_rate;
  unsigned int channels;
  unsigned int bits_per_sample;
  int write_error;
  char path[128];
#ifdef CONFIG_NXPLAYER_INCLUDE_PREFERRED_DEVICE
  char dev_path[128];
#endif
};

static uint64_t audio_pb_now_ms(void)
{
  struct timespec ts;

#ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint64_t audio_playback_pcm_ms(const audio_playback_t *pb)
{
  uint64_t bytes_per_second;

  if (pb == NULL || pb->sample_rate == 0 || pb->channels == 0 ||
      pb->bits_per_sample == 0) {
    return 0;
  }

  bytes_per_second = (uint64_t)pb->sample_rate * pb->channels *
                     (pb->bits_per_sample / 8);
  return (uint64_t)pb->total_written * 1000 / bytes_per_second;
}

#ifdef CONFIG_SYSTEM_NXPLAYER
static int audio_playback_wait_idle(struct nxplayer_s *player,
                                    const audio_playback_t *pb,
                                    uint64_t start_ms)
{
  uint64_t limit_ms;

  if (player == NULL || pb == NULL) {
    return -EINVAL;
  }

  limit_ms = audio_playback_pcm_ms(pb) + PB_IDLE_EXTRA_MS;
  if (limit_ms < PB_IDLE_MIN_WAIT_MS) {
    limit_ms = PB_IDLE_MIN_WAIT_MS;
  }

  for (;;) {
    int state;

    pthread_mutex_lock(&player->mutex);
    state = player->state;
    pthread_mutex_unlock(&player->mutex);
    if (state == AUDIO_PB_NXPLAYER_STATE_IDLE) {
      return 0;
    }
    if (audio_pb_now_ms() - start_ms > limit_ms) {
      syslog(LOG_ERR,
             "[%s] playback idle timeout (audio=%llums bytes=%zu state=%d)\n",
             TAG, (unsigned long long)audio_playback_pcm_ms(pb),
             pb->total_written, state);
      return -ETIMEDOUT;
    }
    usleep(20 * 1000);
  }
}
#endif

audio_playback_t *audio_playback_open(const char *dev_path,
                                      unsigned int sample_rate,
                                      unsigned int channels,
                                      unsigned int bits_per_sample)
{
  audio_playback_t *pb;

  if (sample_rate == 0 || (channels != 1 && channels != 2) ||
      bits_per_sample != 16) {
    return NULL;
  }

  pb = calloc(1, sizeof(*pb));
  if (pb == NULL) {
    return NULL;
  }

  pb->tmp_fd = -1;
  pb->sample_rate = sample_rate;
  pb->channels = channels;
  pb->bits_per_sample = bits_per_sample;
#ifdef CONFIG_NXPLAYER_INCLUDE_PREFERRED_DEVICE
  if (dev_path != NULL) {
    snprintf(pb->dev_path, sizeof(pb->dev_path), "%s", dev_path);
  }
#else
  (void)dev_path;
#endif

  {
    char tmp_path[] = PB_TMP_TEMPLATE;

    pb->tmp_fd = mkstemp(tmp_path);
    if (pb->tmp_fd >= 0) {
      snprintf(pb->path, sizeof(pb->path), "%s", tmp_path);
    }
  }

  if (pb->tmp_fd < 0) {
    syslog(LOG_ERR, "[%s] cannot create playback buffer: %d\n", TAG, errno);
    free(pb);
    return NULL;
  }

  return pb;
}

int audio_playback_write(audio_playback_t *pb,
                         const void *buf, size_t len)
{
  const unsigned char *src;
  size_t off = 0;

  if (pb == NULL || pb->tmp_fd < 0 || buf == NULL || len == 0) {
    return -EINVAL;
  }

  src = (const unsigned char *)buf;
  while (off < len) {
    ssize_t n = write(pb->tmp_fd, src + off, len - off);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      pb->write_error = errno == 0 ? EIO : errno;
      return -pb->write_error;
    }
    if (n == 0) {
      pb->write_error = EIO;
      return -EIO;
    }
    off += (size_t)n;
  }

  pb->total_written += off;
  return (int)off;
}

#ifndef CONFIG_SYSTEM_NXPLAYER
static int audio_playback_close_legacy(audio_playback_t *pb)
{
  char url[256];
  char opts[128];
  void *player;
  int ret;

  snprintf(url, sizeof(url), "file://%s", pb->path);
  snprintf(opts, sizeof(opts),
           "format=s%ule:sample_rate=%u:ch_layout=%s",
           pb->bits_per_sample, pb->sample_rate,
           pb->channels == 1 ? "mono" : "stereo");

  player = media_player_open(MEDIA_STREAM_MUSIC);
  if (player == NULL) {
    syslog(LOG_ERR, "[%s] media_player_open failed\n", TAG);
    return -EIO;
  }

  ret = media_player_prepare(player, url, opts);
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] media_player_prepare failed: %d\n", TAG, ret);
    media_player_close(player, 0);
    return ret;
  }

  ret = media_player_start(player);
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] media_player_start failed: %d\n", TAG, ret);
    media_player_close(player, 0);
    return ret;
  }

  usleep((useconds_t)(audio_playback_pcm_ms(pb) + 500) * 1000);
  media_player_close(player, 0);
  return 0;
}
#endif

int audio_playback_close(audio_playback_t *pb)
{
  int ret = 0;

  if (pb == NULL) {
    return -EINVAL;
  }
  if (pb->write_error != 0) {
    ret = -pb->write_error;
    goto out;
  }
  if (pb->total_written == 0) {
    ret = -EIO;
    goto out;
  }

#ifdef CONFIG_SYSTEM_NXPLAYER
  {
    struct nxplayer_s *player = nxplayer_create();
    uint64_t play_start_ms;

    if (player == NULL) {
      ret = -EIO;
      goto out;
    }
#ifdef CONFIG_NXPLAYER_INCLUDE_PREFERRED_DEVICE
    ret = nxplayer_setdevice(player, pb->dev_path[0] != '\0'
                                      ? pb->dev_path
                                      : VELACLAW_AUDIO_PLAYBACK_DEV);
    if (ret < 0) {
      syslog(LOG_WARNING, "[%s] nxplayer_setdevice failed: %d\n", TAG, ret);
    }
#endif
    play_start_ms = audio_pb_now_ms();
    ret = nxplayer_playraw(player, pb->path, AUDIO_FMT_PCM,
                           AUDIO_FMT_UNDEF, (uint8_t)pb->channels,
                           (uint8_t)pb->bits_per_sample, pb->sample_rate, 0);
    if (ret == 0) {
      ret = audio_playback_wait_idle(player, pb, play_start_ms);
    }
    nxplayer_release(player);
    if (ret == 0) {
      syslog(LOG_INFO,
             "[%s] playback done bytes=%zu audio=%llums elapsed=%llums\n",
             TAG, pb->total_written,
             (unsigned long long)audio_playback_pcm_ms(pb),
             (unsigned long long)(audio_pb_now_ms() - play_start_ms));
    }
  }
#else
  ret = audio_playback_close_legacy(pb);
#endif

out:
  if (pb->path[0] != '\0') {
    unlink(pb->path);
  }
  if (pb->tmp_fd >= 0) {
    close(pb->tmp_fd);
  }
  free(pb);
  return ret;
}

int audio_playback_abort(audio_playback_t *pb)
{
  if (pb == NULL) {
    return -EINVAL;
  }
  if (pb->tmp_fd >= 0) {
    close(pb->tmp_fd);
  }
  if (pb->path[0] != '\0') {
    unlink(pb->path);
  }
  free(pb);
  return 0;
}
