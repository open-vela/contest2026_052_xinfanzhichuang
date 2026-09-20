#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/tls/vela_tls.h"

int claw_config_get(const char *key, char *value, size_t value_size)
{
  (void)key;
  (void)value;
  (void)value_size;
  return -1;
}

int vela_https_request_stream(const char *host, const char *port,
                              const char *method, const char *path,
                              const vela_header_t *headers,
                              const char *body, size_t body_len,
                              vela_https_body_cb_t cb, void *user_data,
                              int *out_http_status)
{
  (void)host;
  (void)port;
  (void)method;
  (void)path;
  (void)headers;
  (void)body;
  (void)body_len;
  (void)cb;
  (void)user_data;
  (void)out_http_status;
  return -1;
}

int mimo_tts_test_parse_sse(const char *sse, size_t sse_len,
                            size_t input_chunk,
                            unsigned char *pcm, size_t pcm_cap,
                            size_t *pcm_len);

int main(void)
{
  static const char sse[] =
    "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{"
    "\"delta\":{}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":"
    "\"AAECAwQ\"}}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":"
    "\"FBgcICQ==\"}}}]}\n\n"
    "data: {\"audio\":\"data:audio/pcm;base64,CgsM\"}\n\n"
    "data: [DONE]\n\n";
  static const unsigned char expected[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
  };
  unsigned char pcm[32];
  size_t pcm_len = 0;
  const size_t repeats = 4096;
  static const char prefix[] =
    "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"";
  static const char suffix[] = "\"}}}]}\n\ndata: [DONE]\n\n";
  size_t long_len = sizeof(prefix) - 1 + repeats * 4 + sizeof(suffix) - 1;
  char *long_sse = malloc(long_len + 1);
  unsigned char *long_pcm = malloc(repeats * 3);
  size_t long_pcm_len = 0;

  assert(long_sse != NULL && long_pcm != NULL);

  memset(pcm, 0xff, sizeof(pcm));
  assert(mimo_tts_test_parse_sse(sse, sizeof(sse) - 1, 7,
                                 pcm, sizeof(pcm), &pcm_len) == 0);
  assert(pcm_len == sizeof(expected));
  assert(memcmp(pcm, expected, sizeof(expected)) == 0);

  memcpy(long_sse, prefix, sizeof(prefix) - 1);
  for (size_t i = 0; i < repeats; i++) {
    memcpy(long_sse + sizeof(prefix) - 1 + i * 4, "AAEC", 4);
  }
  memcpy(long_sse + sizeof(prefix) - 1 + repeats * 4,
         suffix, sizeof(suffix) - 1);
  long_sse[long_len] = '\0';

  assert(mimo_tts_test_parse_sse(long_sse, long_len, 13,
                                 long_pcm, repeats * 3,
                                 &long_pcm_len) == 0);
  assert(long_pcm_len == repeats * 3);
  for (size_t i = 0; i < long_pcm_len; i += 3) {
    assert(long_pcm[i] == 0);
    assert(long_pcm[i + 1] == 1);
    assert(long_pcm[i + 2] == 2);
  }

  free(long_pcm);
  free(long_sse);
  return 0;
}
