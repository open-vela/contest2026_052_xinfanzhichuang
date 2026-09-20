#include "../src/voice/voice_vad.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_BYTES 3200

static int feed(voice_vad_t *vad, const unsigned char *frame,
                unsigned char **utterance, size_t *len)
{
  return voice_vad_process(vad, frame, FRAME_BYTES, utterance, len);
}

static int contains_sample(const unsigned char *pcm, size_t len,
                           unsigned char lo, unsigned char hi)
{
  for (size_t i = 0; i + 1 < len; i += 2) {
    if (pcm[i] == lo && pcm[i + 1] == hi) {
      return 1;
    }
  }
  return 0;
}

int main(void)
{
  voice_vad_config_t cfg = {
    .sample_rate = 16000,
    .channels = 1,
    .bits_per_sample = 16,
    .min_utterance_ms = 500,
    .max_utterance_ms = 3500,
    .silence_ms = 800,
    .start_chunks = 2,
    .min_energy = 500,
  };
  voice_vad_t vad;
  unsigned char speech[FRAME_BYTES];
  unsigned char silence[FRAME_BYTES] = { 0 };
  unsigned char *first = NULL;
  unsigned char *second = NULL;
  size_t first_len = 0;
  size_t second_len = 0;
  int ret;

  for (size_t i = 0; i < FRAME_BYTES; i += 2) {
    speech[i] = 0xe8;
    speech[i + 1] = 0x03;
  }

  assert(voice_vad_init(&vad, &cfg) == 0);
  for (int i = 0; i < 12; i++) {
    assert(feed(&vad, silence, &first, &first_len) == 0);
  }
  for (int i = 0; i < 6; i++) {
    assert(feed(&vad, speech, &first, &first_len) == 0);
  }
  /* A natural pause between "Hello" and "openvela" must not split them. */
  for (int i = 0; i < 3; i++) {
    assert(feed(&vad, silence, &first, &first_len) == 0);
  }
  for (int i = 0; i < 5; i++) {
    assert(feed(&vad, speech, &first, &first_len) == 0);
  }
  for (int i = 0; i < 8; i++) {
    ret = feed(&vad, silence, &first, &first_len);
  }
  assert(ret == 1);
  assert(first != NULL && first_len >= 16000 && first_len <= 80000);
  assert(vad.last_finish_len - first_len == 600 * vad.bytes_per_ms);
  assert(contains_sample(first, first_len, speech[0], speech[1]));
  assert(vad.segment == NULL && vad.segment_cap == 0);

  voice_vad_release_buffers(&vad);
  assert(vad.segment == NULL && vad.segment_cap == 0);
  assert(vad.preroll == NULL && vad.preroll_len == 0);

  for (int i = 0; i < 6; i++) {
    assert(feed(&vad, speech, &second, &second_len) == 0);
  }
  assert(vad.preroll != NULL);
  assert(vad.segment != first);
  free(first);

  for (int i = 0; i < 8; i++) {
    ret = feed(&vad, silence, &second, &second_len);
  }
  assert(ret == 1);
  assert(second != NULL && second_len >= 16000 && second_len <= 80000);
  assert(vad.last_finish_len - second_len == 600 * vad.bytes_per_ms);
  assert(contains_sample(second, second_len, speech[0], speech[1]));
  free(second);
  voice_vad_deinit(&vad);
  return 0;
}
