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

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio playback session handle (opaque). */
typedef struct audio_playback audio_playback_t;

/* Open the playback device and configure for PCM output.
 * Returns NULL on failure. */
audio_playback_t *audio_playback_open(const char *dev_path,
    unsigned int sample_rate, unsigned int channels,
    unsigned int bits_per_sample);

/* Write PCM data to the playback session.
 * Returns bytes written, or negative errno on error. */
int audio_playback_write(audio_playback_t *pb,
    const void *buf, size_t len);

/* Stop playback and release all resources.
 * Returns 0 on success, or negative errno on playback/write failure. */
int audio_playback_close(audio_playback_t *pb);

/* Discard buffered PCM without playing it and release all resources. */
int audio_playback_abort(audio_playback_t *pb);

#ifdef __cplusplus
}
#endif
