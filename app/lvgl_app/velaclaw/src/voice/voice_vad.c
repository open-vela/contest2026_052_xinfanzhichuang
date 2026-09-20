/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "voice_vad.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define VOICE_VAD_PREROLL_MS 400
#define VOICE_VAD_KEEP_TAIL_MS 200
#define VOICE_VAD_NOISE_SAMPLES 12
#define VOICE_VAD_FALLBACK_MAX_MS 800
#define VOICE_VAD_GROW_STEP_MS 400

static const char *TAG = "voice_vad";

static unsigned int voice_vad_max_u32(unsigned int a, unsigned int b)
{
  return a > b ? a : b;
}

static unsigned int voice_vad_energy(const unsigned char *pcm, size_t len)
{
  uint64_t sum = 0;
  size_t samples = len / 2;

  if (samples == 0) {
    return 0;
  }

  for (size_t i = 0; i < samples; i++) {
    int16_t s = (int16_t)(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
    int32_t v = s;

    if (v < 0) {
      v = -v;
    }
    sum += (uint32_t)v;
  }

  return (unsigned int)(sum / samples);
}

static size_t voice_vad_ms_to_bytes(const voice_vad_t *vad, unsigned int ms)
{
  uint64_t bytes = (uint64_t)vad->bytes_per_ms * ms;

  if (bytes > SIZE_MAX) {
    return SIZE_MAX;
  }

  return (size_t)bytes;
}

static void voice_vad_store_preroll(voice_vad_t *vad,
                                    const unsigned char *pcm, size_t len)
{
  if (vad->preroll_cap == 0 || pcm == NULL || len == 0) {
    return;
  }

  if (vad->preroll == NULL) {
    vad->preroll = malloc(vad->preroll_cap);
    if (vad->preroll == NULL) {
      return;
    }
  }

  if (len >= vad->preroll_cap) {
    memcpy(vad->preroll, pcm + len - vad->preroll_cap,
           vad->preroll_cap);
    vad->preroll_len = vad->preroll_cap;
    return;
  }

  if (vad->preroll_len + len > vad->preroll_cap) {
    size_t drop = vad->preroll_len + len - vad->preroll_cap;

    memmove(vad->preroll, vad->preroll + drop,
            vad->preroll_len - drop);
    vad->preroll_len -= drop;
  }

  memcpy(vad->preroll + vad->preroll_len, pcm, len);
  vad->preroll_len += len;
}

static int voice_vad_alloc_segment(voice_vad_t *vad)
{
  size_t initial_cap;

  initial_cap = voice_vad_ms_to_bytes(vad, VOICE_VAD_FALLBACK_MAX_MS);
  if (initial_cap < vad->min_bytes) {
    initial_cap = vad->min_bytes;
  }
  if (initial_cap > vad->max_bytes) {
    initial_cap = vad->max_bytes;
  }

  vad->segment_cap = initial_cap;
  vad->segment = malloc(initial_cap);
  if (vad->segment == NULL) {
    vad->segment_cap = 0;
    return -ENOMEM;
  }
  return 0;
}

static int voice_vad_append(voice_vad_t *vad,
                            const unsigned char *pcm, size_t len)
{
  if (len == 0) {
    return 0;
  }

  if (vad->segment == NULL) {
    if (voice_vad_alloc_segment(vad) < 0) {
      return -ENOMEM;
    }
  }

  if (vad->segment_len + len > vad->segment_cap &&
      vad->segment_cap < vad->max_bytes) {
    size_t grow = voice_vad_ms_to_bytes(vad, VOICE_VAD_GROW_STEP_MS);
    size_t needed = vad->segment_len + len;
    size_t new_cap = vad->segment_cap + grow;
    unsigned char *new_segment;

    if (new_cap < needed) {
      new_cap = needed;
    }
    if (new_cap > vad->max_bytes) {
      new_cap = vad->max_bytes;
    }
    new_segment = realloc(vad->segment, new_cap);
    if (new_segment != NULL) {
      vad->segment = new_segment;
      vad->segment_cap = new_cap;
    }
  }

  if (vad->segment_len + len > vad->segment_cap) {
    len = vad->segment_cap - vad->segment_len;
  }

  if (len == 0) {
    return -ENOSPC;
  }

  memcpy(vad->segment + vad->segment_len, pcm, len);
  vad->segment_len += len;
  return 0;
}
static int voice_vad_finish(voice_vad_t *vad,
                            unsigned char **out_pcm, size_t *out_len,
                            unsigned int reason, unsigned int energy)
{
  unsigned char *segment;
  size_t keep_tail;
  size_t trim;

  vad->last_finish_reason = reason;
  vad->last_finish_energy = energy;
  vad->last_finish_len = vad->segment_len;
  vad->last_finish_silence_bytes = vad->silence_bytes;

  if (vad->segment_len < vad->min_bytes) {
    voice_vad_reset(vad);
    return 0;
  }

  /* Wait for a long silence to avoid splitting the wake phrase, but do not
   * upload that whole silence. It wastes heap and can confuse short-clip ASR. */
  keep_tail = voice_vad_ms_to_bytes(vad, VOICE_VAD_KEEP_TAIL_MS);
  if (vad->silence_bytes > keep_tail) {
    trim = vad->silence_bytes - keep_tail;
    if (trim < vad->segment_len &&
        vad->segment_len - trim >= vad->min_bytes) {
      vad->segment_len -= trim;
    }
  }

  /* The listener discards audio while ASR is running. Transfer the segment
   * to the ASR worker and allocate the next one only when speech resumes. */
  segment = realloc(vad->segment, vad->segment_len);
  if (segment == NULL) {
    segment = vad->segment;
  }
  *out_pcm = segment;
  *out_len = vad->segment_len;
  vad->segment = NULL;
  vad->segment_cap = 0;
  voice_vad_reset(vad);
  return 1;
}

int voice_vad_init(voice_vad_t *vad, const voice_vad_config_t *cfg)
{
  unsigned int frame_bytes;

  if (vad == NULL || cfg == NULL || cfg->sample_rate == 0 ||
      (cfg->channels != 1 && cfg->channels != 2) ||
      cfg->bits_per_sample != 16 || cfg->max_utterance_ms == 0) {
    return -EINVAL;
  }

  memset(vad, 0, sizeof(*vad));
  vad->cfg = *cfg;

  frame_bytes = cfg->channels * (cfg->bits_per_sample / 8);
  vad->bytes_per_ms = (cfg->sample_rate * frame_bytes) / 1000;
  if (vad->bytes_per_ms == 0) {
    return -EINVAL;
  }

  vad->min_bytes = voice_vad_ms_to_bytes(vad, cfg->min_utterance_ms);
  vad->max_bytes = voice_vad_ms_to_bytes(vad, cfg->max_utterance_ms);
  vad->silence_bytes_limit = voice_vad_ms_to_bytes(vad, cfg->silence_ms);
  /* Processing stops as soon as max_bytes is reached, so reserving an extra
   * silence window can never be used and only increases startup pressure. */
  vad->segment_cap = vad->max_bytes;
  vad->preroll_cap = voice_vad_ms_to_bytes(vad, VOICE_VAD_PREROLL_MS);
  vad->threshold = cfg->min_energy;

  if (voice_vad_alloc_segment(vad) < 0) {
    voice_vad_deinit(vad);
    return -ENOMEM;
  }

  vad->preroll = malloc(vad->preroll_cap);
  if (vad->preroll == NULL) {
    syslog(LOG_WARNING, "[%s] VAD preroll disabled: cap=%zu\n",
           TAG, vad->preroll_cap);
    vad->preroll_cap = 0;
    vad->preroll_len = 0;
  }

  return 0;
}

void voice_vad_reset(voice_vad_t *vad)
{
  if (vad == NULL) {
    return;
  }

  vad->active = false;
  vad->silence_bytes = 0;
  vad->speech_chunks = 0;
  vad->segment_len = 0;
  vad->preroll_len = 0;
}

void voice_vad_release_buffers(voice_vad_t *vad)
{
  if (vad == NULL) {
    return;
  }

  free(vad->segment);
  free(vad->preroll);
  vad->segment = NULL;
  vad->segment_cap = 0;
  vad->preroll = NULL;
  voice_vad_reset(vad);
}

void voice_vad_deinit(voice_vad_t *vad)
{
  if (vad == NULL) {
    return;
  }

  voice_vad_release_buffers(vad);
  memset(vad, 0, sizeof(*vad));
}

int voice_vad_process(voice_vad_t *vad,
                      const unsigned char *pcm, size_t len,
                      unsigned char **out_pcm,
                      size_t *out_len)
{
  unsigned int energy;
  bool speech;

  if (vad == NULL || pcm == NULL || len == 0 ||
      out_pcm == NULL || out_len == NULL) {
    return -EINVAL;
  }

  *out_pcm = NULL;
  *out_len = 0;
  energy = voice_vad_energy(pcm, len);

  if (!vad->active && vad->noise_samples < VOICE_VAD_NOISE_SAMPLES) {
    unsigned int next = vad->noise_samples + 1;

    vad->noise_floor =
      (vad->noise_floor * vad->noise_samples + energy) / next;
    vad->noise_samples = next;
    vad->threshold = voice_vad_max_u32(
      vad->cfg.min_energy, vad->noise_floor * 2 + 200);
  }

  speech = energy >= vad->threshold;

  if (!vad->active) {
    voice_vad_store_preroll(vad, pcm, len);
    if (speech) {
      vad->speech_chunks++;
    } else {
      vad->speech_chunks = 0;
    }

    if (vad->speech_chunks >= vad->cfg.start_chunks) {
      vad->active = true;
      vad->silence_bytes = 0;
      vad->segment_len = 0;
      if (voice_vad_append(vad, vad->preroll, vad->preroll_len) < 0) {
        voice_vad_reset(vad);
        return -ENOMEM;
      }
      vad->preroll_len = 0;
      if (vad->segment_len >= vad->max_bytes) {
        return voice_vad_finish(vad, out_pcm, out_len,
                                VOICE_VAD_FINISH_MAX_MS, energy);
      }
    }
    return 0;
  }

  if (voice_vad_append(vad, pcm, len) < 0) {
    return voice_vad_finish(vad, out_pcm, out_len,
                            VOICE_VAD_FINISH_CAPACITY, energy);
  }

  if (speech) {
    vad->silence_bytes = 0;
  } else {
    vad->silence_bytes += len;
  }

  if (vad->segment_len >= vad->max_bytes ||
      (vad->segment_len >= vad->min_bytes &&
       vad->silence_bytes >= vad->silence_bytes_limit)) {
    return voice_vad_finish(vad, out_pcm, out_len,
                            vad->segment_len >= vad->max_bytes ?
                            VOICE_VAD_FINISH_MAX_MS :
                            VOICE_VAD_FINISH_SILENCE,
                            energy);
  }

  return 0;
}
