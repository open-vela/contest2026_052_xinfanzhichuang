/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#define VOICE_VAD_FINISH_NONE 0
#define VOICE_VAD_FINISH_MAX_MS 1
#define VOICE_VAD_FINISH_SILENCE 2
#define VOICE_VAD_FINISH_CAPACITY 3
#define VOICE_VAD_FINISH_BORROWED 4

typedef struct
{
  unsigned int sample_rate;
  unsigned int channels;
  unsigned int bits_per_sample;
  unsigned int min_utterance_ms;
  unsigned int max_utterance_ms;
  unsigned int silence_ms;
  unsigned int start_chunks;
  unsigned int min_energy;
} voice_vad_config_t;

typedef struct
{
  voice_vad_config_t cfg;
  bool active;
  size_t bytes_per_ms;
  size_t min_bytes;
  size_t max_bytes;
  size_t silence_bytes_limit;
  size_t silence_bytes;
  unsigned int speech_chunks;
  unsigned int threshold;
  unsigned int noise_floor;
  unsigned int noise_samples;
  unsigned int last_finish_reason;
  unsigned int last_finish_energy;
  size_t last_finish_len;
  size_t last_finish_silence_bytes;
  unsigned char *segment;
  size_t segment_len;
  size_t segment_cap;
  unsigned char *preroll;
  size_t preroll_len;
  size_t preroll_cap;
} voice_vad_t;

int voice_vad_init(voice_vad_t *vad, const voice_vad_config_t *cfg);
void voice_vad_reset(voice_vad_t *vad);
void voice_vad_release_buffers(voice_vad_t *vad);
void voice_vad_deinit(voice_vad_t *vad);

int voice_vad_process(voice_vad_t *vad,
                      const unsigned char *pcm, size_t len,
                      unsigned char **out_pcm,
                      size_t *out_len);
