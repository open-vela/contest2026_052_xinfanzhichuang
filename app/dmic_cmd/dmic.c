/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <nuttx/cache.h>

#include <aic_core.h>
#include <aic_drv_dma.h>
#include <drv_dma.h>
#include <hal_audio.h>

#ifndef CONFIG_AIC_DMIC_CMD_DMA_CHANNEL
#define CONFIG_AIC_DMIC_CMD_DMA_CHANNEL 1
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 32
#endif

#define DMIC_DEFAULT_RATE 16000
#define DMIC_DEFAULT_CHANNELS 2
#define DMIC_DEFAULT_SECONDS 3
#define DMIC_DEFAULT_PERIOD_BYTES 4096
#define DMIC_BUFFER_PERIODS 2
#define DMIC_SAMPLE_BITS 16
#define DMIC_DEFAULT_REG_VOLUME 160
#define DMIC_POLL_US 1000
#define DMIC_NO_DATA_TIMEOUT_US 3000000

struct dmic_options {
  uint32_t samplerate;
  uint8_t channels;
  uint32_t seconds;
  uint32_t period_bytes;
  const char *outfile;
  bool wav;
};

struct dmic_channel_stats {
  int16_t min;
  int16_t max;
  uint32_t peak;
  uint64_t sum_abs;
  uint64_t samples;
  uint64_t nonzero;
};

struct dmic_runtime {
  hal_audio_handle_t audio;
  struct aic_dma_chan_s rxchan;
  uint8_t *buffer;
  uint32_t buffer_bytes;
  uint32_t period_bytes;
  uint8_t channels;
  int fd;
  bool wav;
  bool dma_inited;
  bool dma_registered;
  bool audio_inited;
  bool started;
  volatile uint32_t ready[DMIC_BUFFER_PERIODS];
  volatile uint32_t next_slot;
  uint32_t processed_periods;
  uint32_t lost_periods;
  uint64_t data_bytes;
  struct dmic_channel_stats stats[DMIC_DEFAULT_CHANNELS];
};

static struct dmic_runtime *g_dmic_active;
static volatile sig_atomic_t g_dmic_stop;

static void dmic_usage(void) {
  printf("Usage: dmic [start] [-r rate] [-c 1|2] [-t seconds] "
         "[-p period_bytes] [-o path] [-w]\n");
  printf("       dmic regs\n");
  printf("Default: 16000 Hz, 2 channels, 3 seconds, stats only\n");
  printf("Examples:\n");
  printf("  dmic\n");
  printf("  dmic start -t 5 -o /data/dmic.raw\n");
  printf("  dmic start -r 16000 -c 2 -t 5 -o /data/dmic.wav -w\n");
}

static void dmic_signal_handler(int signo) {
  (void)signo;
  g_dmic_stop = 1;
}

static bool dmic_valid_rate(uint32_t samplerate) {
  static const uint32_t rates[] = {
      8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
  };

  for (unsigned int i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
    if (samplerate == rates[i]) {
      return true;
    }
  }

  return false;
}

static bool dmic_path_is_wav(const char *path) {
  const char *ext;

  if (path == NULL) {
    return false;
  }

  ext = strrchr(path, '.');
  return ext != NULL && strcasecmp(ext, ".wav") == 0;
}

static uint32_t dmic_frame_bytes(uint8_t channels) {
  return (uint32_t)channels * (DMIC_SAMPLE_BITS / 8);
}

static int dmic_parse_u32(const char *text, uint32_t *value) {
  char *endptr;
  unsigned long parsed;

  if (text == NULL || value == NULL) {
    return -EINVAL;
  }

  errno = 0;
  parsed = strtoul(text, &endptr, 0);
  if (errno != 0 || endptr == text || *endptr != '\0' ||
      parsed > UINT32_MAX) {
    return -EINVAL;
  }

  *value = (uint32_t)parsed;
  return 0;
}

static void dmic_put_u16(uint8_t *buf, uint16_t value) {
  buf[0] = (uint8_t)(value & 0xff);
  buf[1] = (uint8_t)((value >> 8) & 0xff);
}

static void dmic_put_u32(uint8_t *buf, uint32_t value) {
  buf[0] = (uint8_t)(value & 0xff);
  buf[1] = (uint8_t)((value >> 8) & 0xff);
  buf[2] = (uint8_t)((value >> 16) & 0xff);
  buf[3] = (uint8_t)((value >> 24) & 0xff);
}

static void dmic_make_wav_header(uint8_t header[44], uint32_t samplerate,
                                 uint8_t channels, uint32_t data_bytes) {
  uint32_t byte_rate = samplerate * channels * (DMIC_SAMPLE_BITS / 8);
  uint16_t block_align = channels * (DMIC_SAMPLE_BITS / 8);

  memset(header, 0, 44);
  memcpy(&header[0], "RIFF", 4);
  dmic_put_u32(&header[4], data_bytes + 36);
  memcpy(&header[8], "WAVE", 4);
  memcpy(&header[12], "fmt ", 4);
  dmic_put_u32(&header[16], 16);
  dmic_put_u16(&header[20], 1);
  dmic_put_u16(&header[22], channels);
  dmic_put_u32(&header[24], samplerate);
  dmic_put_u32(&header[28], byte_rate);
  dmic_put_u16(&header[32], block_align);
  dmic_put_u16(&header[34], DMIC_SAMPLE_BITS);
  memcpy(&header[36], "data", 4);
  dmic_put_u32(&header[40], data_bytes);
}

static int dmic_write_all(int fd, const void *buffer, size_t bytes) {
  const uint8_t *ptr = buffer;

  while (bytes > 0) {
    ssize_t written = write(fd, ptr, bytes);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }

      return -errno;
    }

    if (written == 0) {
      return -EIO;
    }

    ptr += written;
    bytes -= (size_t)written;
  }

  return 0;
}

static int dmic_write_wav_header(int fd, const struct dmic_options *opts,
                                 uint32_t data_bytes) {
  uint8_t header[44];

  dmic_make_wav_header(header, opts->samplerate, opts->channels, data_bytes);
  return dmic_write_all(fd, header, sizeof(header));
}

static void dmic_stats_init(struct dmic_runtime *rt) {
  for (unsigned int i = 0; i < DMIC_DEFAULT_CHANNELS; i++) {
    rt->stats[i].min = INT16_MAX;
    rt->stats[i].max = INT16_MIN;
    rt->stats[i].peak = 0;
    rt->stats[i].sum_abs = 0;
    rt->stats[i].samples = 0;
    rt->stats[i].nonzero = 0;
  }
}

static void dmic_stats_add(struct dmic_channel_stats *stats, int16_t sample) {
  int32_t value = sample;
  uint32_t absval = value < 0 ? (uint32_t)(-value) : (uint32_t)value;

  if (sample < stats->min) {
    stats->min = sample;
  }

  if (sample > stats->max) {
    stats->max = sample;
  }

  if (absval > stats->peak) {
    stats->peak = absval;
  }

  if (sample != 0) {
    stats->nonzero++;
  }

  stats->sum_abs += absval;
  stats->samples++;
}

static void dmic_process_period(struct dmic_runtime *rt, uint8_t *period) {
  int16_t *samples = (int16_t *)period;
  size_t sample_count = rt->period_bytes / sizeof(int16_t);
  uint8_t channels = rt->channels;

  for (size_t i = 0; i + channels <= sample_count; i += channels) {
    dmic_stats_add(&rt->stats[0], samples[i]);
    if (channels == 2) {
      dmic_stats_add(&rt->stats[1], samples[i + 1]);
    }
  }
}

static void dmic_print_stats(const struct dmic_runtime *rt,
                             const struct dmic_options *opts) {
  uint64_t frames = rt->data_bytes / dmic_frame_bytes(opts->channels);

  printf("dmic: done rate=%" PRIu32 " channels=%u bits=%u bytes=%" PRIu64
         " frames=%" PRIu64 " periods=%" PRIu32,
         opts->samplerate, opts->channels, DMIC_SAMPLE_BITS, rt->data_bytes,
         frames, rt->processed_periods);

  if (rt->lost_periods != 0) {
    printf(" lost=%" PRIu32, rt->lost_periods);
  }

  printf("\n");

  for (uint8_t ch = 0; ch < opts->channels; ch++) {
    const struct dmic_channel_stats *stats = &rt->stats[ch];
    uint64_t mean_abs = stats->samples == 0 ? 0 :
                        stats->sum_abs / stats->samples;
    char name = ch == 0 ? 'L' : 'R';

    printf("dmic: %c samples=%" PRIu64 " min=%" PRId16 " max=%" PRId16
           " peak=%" PRIu32 " mean_abs=%" PRIu64 " nonzero=%" PRIu64 "\n",
           name, stats->samples, stats->min, stats->max, stats->peak,
           mean_abs, stats->nonzero);
  }
}

static void dmic_dump_audio_regs(const hal_audio_handle_t *audio) {
  static const uint8_t offsets[] = {
      RX_DMIC_IF_CTRL, RX_HPF_1_2_CTRL, RX_DVC_1_2_CTRL,
      DMIC_RXFIFO_CTRL, FIFO_INT_EN, FIFO_STA,
      DMIC_RX_CNT, GLOBE_CTL,
  };

  printf("dmic: AUDIO_BASE=0x%08" PRIxPTR "\n", (uintptr_t)audio->regbase);
  for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    uintptr_t addr = audio->regbase + offsets[i];
    printf("  [0x%02x] 0x%08" PRIx32 "\n", offsets[i],
           (uint32_t)readl(addr));
  }
}

static void dmic_dump_cmu_regs(void) {
  static const uint32_t offsets[] = {
      CLK_AUDIO_REG,
      CLK_CODEC_REG,
  };

  printf("dmic: CMU_BASE=0x%08" PRIxPTR "\n", (uintptr_t)CMU_BASE);
  for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    uintptr_t addr = CMU_BASE + offsets[i];
    printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n", offsets[i],
           (uint32_t)readl(addr));
  }
}

static void dmic_dump_dma_regs(const hal_dma_handle_t *hdma) {
  uint32_t ch = hdma->init.channel_id;
  static const uint32_t offsets[] = {
      DMA_IRQ_EN, DMA_IRQ_STA, DMA_CH_STA,
  };

  printf("dmic: DMA_BASE=0x%08" PRIxPTR " ch=%" PRIu32 "\n",
         (uintptr_t)hdma->regbase, ch);
  for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    uintptr_t addr = hdma->regbase + offsets[i];
    printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n", (uint32_t)offsets[i],
           (uint32_t)readl(addr));
  }

  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_CH_EN(ch),
         (uint32_t)readl(DMA_CH_EN_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_CH_PAUSE(ch),
         (uint32_t)readl(DMA_CH_PAUSE_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_CH_TASK(ch),
         (uint32_t)readl(DMA_CH_TASK_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_SRC_ADDR(ch),
         (uint32_t)readl(DMA_SRC_ADDR_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_SINK_ADDR(ch),
         (uint32_t)readl(DMA_SINK_ADDR_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_BCNT_LEFT(ch),
         (uint32_t)readl(DMA_BCNT_LEFT_REG(hdma, ch)));
  printf("  [0x%03" PRIx32 "] 0x%08" PRIx32 "\n",
         (uint32_t)DMA_MODE(ch),
         (uint32_t)readl(DMA_MODE_REG(hdma, ch)));
}

static void dmic_dump_runtime_regs(const struct dmic_runtime *rt,
                                   const char *reason) {
  const hal_dma_handle_t *hdma = &rt->rxchan.hal;

  printf("dmic: register dump (%s)\n",
         reason != NULL ? reason : "runtime");
  printf("dmic: audio state=%u err=%u dma state=%u err=%u\n",
         (unsigned int)rt->audio.state, (unsigned int)rt->audio.errcode,
         (unsigned int)hal_dma_get_state((hal_dma_handle_t *)hdma),
         (unsigned int)hal_dma_get_error((hal_dma_handle_t *)hdma));
  dmic_dump_audio_regs(&rt->audio);
  dmic_dump_cmu_regs();
  dmic_dump_dma_regs(hdma);
}

static void dmic_period_callback(hal_audio_handle_t *haudio) {
  struct dmic_runtime *rt = g_dmic_active;
  uint32_t slot;

  if (rt == NULL || &rt->audio != haudio) {
    return;
  }

  slot = rt->next_slot % DMIC_BUFFER_PERIODS;
  rt->ready[slot]++;
  rt->next_slot++;
}

static int dmic_dma_prepare(struct dmic_runtime *rt,
                            const struct dmic_options *opts) {
  hal_dma_handle_t *hdma = &rt->rxchan.hal;
  hal_dma_data_width_e width;
  int ret;

  ret = drv_dma_init();
  if (ret < 0) {
    fprintf(stderr, "dmic: drv_dma_init failed: %d\n", ret);
    return ret;
  }

  hal_dma_handle_init(hdma);
  hdma->regbase = DMA_BASE;
  hdma->init.channel_id = CONFIG_AIC_DMIC_CMD_DMA_CHANNEL;
  hdma->work_mode = DMA_WORK_MODE_CYCLIC;
  hdma->cyclic_period_len = rt->period_bytes;
  hdma->init.direction = DMA_DEVICE_TO_MEMORY;
  hdma->init.src_dev = HAL_DMA_ID_AUDIO;
  hdma->init.src_mode = DMA_MODE_HANDSHAKE;
  hdma->init.src_burst = DMA_XFER_BURST_1;
  hdma->init.src_addr_mode = DMA_ADDR_FIXED_MODE;

  width = opts->channels == 1 ? DMA_DATA_WIDTH_2_BYTES :
                                DMA_DATA_WIDTH_4_BYTES;
  hdma->init.src_data_width = width;
  hdma->init.snk_dev = HAL_DMA_ID_SRAM;
  hdma->init.snk_mode = DMA_MODE_WAIT;
  hdma->init.snk_burst = DMA_XFER_BURST_1;
  hdma->init.snk_addr_mode = DMA_ADDR_LINEAR_MODE;
  hdma->init.snk_data_width = width;
  hdma->init.flag = HAL_HANDLE_ALL_INIT_FLAG;

  if (hal_dma_init(hdma) != HAL_OK) {
    fprintf(stderr, "dmic: hal_dma_init failed: state=%u err=%u\n",
            (unsigned int)hal_dma_get_state(hdma),
            (unsigned int)hal_dma_get_error(hdma));
    return -EIO;
  }

  rt->dma_inited = true;

  ret = aic_dma_chan_register(&rt->rxchan.chan);
  if (ret < 0) {
    fprintf(stderr, "dmic: aic_dma_chan_register failed: %d\n", ret);
    return ret;
  }

  rt->dma_registered = true;
  return 0;
}

static int dmic_audio_prepare(struct dmic_runtime *rt,
                              const struct dmic_options *opts) {
  hal_audio_handle_init(&rt->audio);
  rt->audio.init.samplebits = AUDIO_SAMPLEBITS_16BIT;
  rt->audio.init.samplerate = (hal_audio_samplerate_e)opts->samplerate;
  rt->audio.init.channel = opts->channels == 1 ? AUDIO_CHANNEL_MONO :
                                                AUDIO_CHANNEL_STEREO;
  rt->audio.rxdma = &rt->rxchan.hal;

  if (hal_audio_init(&rt->audio) != HAL_OK) {
    fprintf(stderr, "dmic: hal_audio_init failed: state=%u err=%u\n",
            (unsigned int)hal_audio_get_state(&rt->audio),
            (unsigned int)hal_audio_get_error(&rt->audio));
    return -EIO;
  }

  rt->audio_inited = true;
  hal_audio_set_dmic_volume(&rt->audio, DMIC_DEFAULT_REG_VOLUME);

  if (hal_audio_register_callback(&rt->audio, AUDIO_CB_RX_DMA,
                                  dmic_period_callback) != HAL_OK) {
    fprintf(stderr, "dmic: register rx callback failed\n");
    return -EIO;
  }

  return 0;
}

static void dmic_cleanup(struct dmic_runtime *rt) {
  g_dmic_active = NULL;

  if (rt->started) {
    hal_audio_stop_dmic_dma(&rt->audio);
    rt->started = false;
  }

  if (rt->audio_inited) {
    hal_audio_unregister_callback(&rt->audio, AUDIO_CB_RX_DMA, NULL);
    hal_audio_deinit(&rt->audio);
    rt->audio_inited = false;
  }

  if (rt->dma_registered) {
    aic_dma_chan_unregister(&rt->rxchan.chan);
    rt->dma_registered = false;
  }

  if (rt->dma_inited) {
    hal_dma_deinit(&rt->rxchan.hal);
    rt->dma_inited = false;
  }

  if (rt->fd >= 0) {
    close(rt->fd);
    rt->fd = -1;
  }

  free(rt->buffer);
  rt->buffer = NULL;
}

static int dmic_open_output(struct dmic_runtime *rt,
                            const struct dmic_options *opts) {
  int ret;

  rt->fd = -1;
  if (opts->outfile == NULL) {
    return 0;
  }

  rt->fd = open(opts->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (rt->fd < 0) {
    ret = -errno;
    fprintf(stderr, "dmic: open %s failed: %d\n", opts->outfile, -ret);
    return ret;
  }

  if (opts->wav) {
    ret = dmic_write_wav_header(rt->fd, opts, 0);
    if (ret < 0) {
      fprintf(stderr, "dmic: write wav header failed: %d\n", -ret);
      return ret;
    }
  }

  return 0;
}

static int dmic_finish_output(struct dmic_runtime *rt,
                              const struct dmic_options *opts) {
  int ret;

  if (rt->fd < 0) {
    return 0;
  }

  if (opts->wav) {
    if (lseek(rt->fd, 0, SEEK_SET) < 0) {
      ret = -errno;
      fprintf(stderr, "dmic: seek wav header failed: %d\n", -ret);
      return ret;
    }

    ret = dmic_write_wav_header(rt->fd, opts, (uint32_t)rt->data_bytes);
    if (ret < 0) {
      fprintf(stderr, "dmic: update wav header failed: %d\n", -ret);
      return ret;
    }
  }

  return 0;
}

static int dmic_handle_period(struct dmic_runtime *rt,
                              const struct dmic_options *opts,
                              unsigned int slot) {
  uint8_t *period = rt->buffer + slot * rt->period_bytes;
  int ret;

  up_invalidate_dcache((uintptr_t)period,
                       (uintptr_t)period + rt->period_bytes);

  if (rt->fd >= 0) {
    ret = dmic_write_all(rt->fd, period, rt->period_bytes);
    if (ret < 0) {
      fprintf(stderr, "dmic: write %s failed: %d\n", opts->outfile, -ret);
      return ret;
    }
  }

  dmic_process_period(rt, period);
  rt->data_bytes += rt->period_bytes;
  rt->processed_periods++;
  return 0;
}

static int dmic_capture_wait(struct dmic_runtime *rt,
                             const struct dmic_options *opts,
                             uint32_t target_periods) {
  uint32_t done[DMIC_BUFFER_PERIODS] = {0};
  uint32_t period_events = 0;
  uint32_t idle_us = 0;

  while ((target_periods == 0 || period_events < target_periods) &&
         !g_dmic_stop) {
    bool progressed = false;

    for (unsigned int slot = 0; slot < DMIC_BUFFER_PERIODS; slot++) {
      uint32_t ready = rt->ready[slot];
      uint32_t delta = ready - done[slot];

      if (delta == 0) {
        continue;
      }

      if (delta > 1) {
        uint32_t skipped = delta - 1;
        rt->lost_periods += skipped;
        period_events += skipped;
        done[slot] = ready - 1;
      }

      if (target_periods != 0 && period_events >= target_periods) {
        break;
      }

      if (dmic_handle_period(rt, opts, slot) < 0) {
        return -EIO;
      }

      done[slot] = ready;
      period_events++;
      progressed = true;
    }

    if (progressed) {
      idle_us = 0;
      continue;
    }

    usleep(DMIC_POLL_US);
    idle_us += DMIC_POLL_US;
    if (idle_us >= DMIC_NO_DATA_TIMEOUT_US) {
      fprintf(stderr, "dmic: timeout waiting for DMA period interrupt\n");
      dmic_dump_runtime_regs(rt, "timeout");
      return -ETIMEDOUT;
    }
  }

  return 0;
}

static int dmic_validate_options(struct dmic_options *opts) {
  uint32_t frame_bytes = dmic_frame_bytes(opts->channels);

  if (!dmic_valid_rate(opts->samplerate)) {
    fprintf(stderr, "dmic: unsupported samplerate %" PRIu32 "\n",
            opts->samplerate);
    return -EINVAL;
  }

  if (opts->channels != 1 && opts->channels != 2) {
    fprintf(stderr, "dmic: channels must be 1 or 2\n");
    return -EINVAL;
  }

  if (opts->period_bytes == 0) {
    opts->period_bytes = DMIC_DEFAULT_PERIOD_BYTES;
  }

  if ((opts->period_bytes % CACHE_LINE_SIZE) != 0 ||
      (opts->period_bytes % frame_bytes) != 0) {
    fprintf(stderr,
            "dmic: period_bytes must align to %u and frame size %" PRIu32 "\n",
            CACHE_LINE_SIZE, frame_bytes);
    return -EINVAL;
  }

  if (opts->outfile != NULL && dmic_path_is_wav(opts->outfile)) {
    opts->wav = true;
  }

  return 0;
}

static int dmic_parse_args(int argc, char *argv[], struct dmic_options *opts) {
  int ch;

  opts->samplerate = DMIC_DEFAULT_RATE;
  opts->channels = DMIC_DEFAULT_CHANNELS;
  opts->seconds = DMIC_DEFAULT_SECONDS;
  opts->period_bytes = DMIC_DEFAULT_PERIOD_BYTES;
  opts->outfile = NULL;
  opts->wav = false;

  if (argc > 1 && strcmp(argv[1], "start") == 0) {
    argc--;
    argv++;
  }

  optind = 1;
  while ((ch = getopt(argc, argv, "hr:c:t:p:o:w")) != -1) {
    uint32_t value;

    switch (ch) {
    case 'h':
      dmic_usage();
      return 1;

    case 'r':
      if (dmic_parse_u32(optarg, &opts->samplerate) < 0) {
        return -EINVAL;
      }
      break;

    case 'c':
      if (dmic_parse_u32(optarg, &value) < 0 || value > UINT8_MAX) {
        return -EINVAL;
      }
      opts->channels = (uint8_t)value;
      break;

    case 't':
      if (dmic_parse_u32(optarg, &opts->seconds) < 0) {
        return -EINVAL;
      }
      break;

    case 'p':
      if (dmic_parse_u32(optarg, &opts->period_bytes) < 0) {
        return -EINVAL;
      }
      break;

    case 'o':
      opts->outfile = optarg;
      break;

    case 'w':
      opts->wav = true;
      break;

    default:
      return -EINVAL;
    }
  }

  if (optind < argc) {
    if (dmic_parse_u32(argv[optind], &opts->seconds) == 0) {
      optind++;
    }
  }

  if (optind < argc) {
    opts->outfile = argv[optind++];
  }

  if (optind < argc) {
    return -EINVAL;
  }

  if (opts->wav && opts->outfile == NULL) {
    opts->outfile = "/data/dmic.wav";
  }

  return dmic_validate_options(opts);
}

static uint32_t dmic_target_periods(const struct dmic_options *opts) {
  uint64_t target_bytes;

  if (opts->seconds == 0) {
    return 0;
  }

  target_bytes = (uint64_t)opts->samplerate * dmic_frame_bytes(opts->channels) *
                 opts->seconds;
  return (uint32_t)((target_bytes + opts->period_bytes - 1) /
                    opts->period_bytes);
}

static int dmic_start_capture(const struct dmic_options *opts) {
  struct dmic_runtime rt;
  uint32_t target_periods;
  int ret;

  memset(&rt, 0, sizeof(rt));
  rt.fd = -1;
  rt.channels = opts->channels;
  rt.period_bytes = opts->period_bytes;
  rt.buffer_bytes = opts->period_bytes * DMIC_BUFFER_PERIODS;
  rt.wav = opts->wav;
  dmic_stats_init(&rt);

  rt.buffer = memalign(CACHE_LINE_SIZE, rt.buffer_bytes);
  if (rt.buffer == NULL) {
    fprintf(stderr, "dmic: allocate %" PRIu32 " bytes failed\n",
            rt.buffer_bytes);
    return -ENOMEM;
  }

  memset(rt.buffer, 0, rt.buffer_bytes);
  up_invalidate_dcache((uintptr_t)rt.buffer,
                       (uintptr_t)rt.buffer + rt.buffer_bytes);

  ret = dmic_open_output(&rt, opts);
  if (ret < 0) {
    goto out;
  }

  ret = dmic_dma_prepare(&rt, opts);
  if (ret < 0) {
    goto out;
  }

  ret = dmic_audio_prepare(&rt, opts);
  if (ret < 0) {
    goto out;
  }

  target_periods = dmic_target_periods(opts);
  printf("dmic: start rate=%" PRIu32 " channels=%u bits=%u period=%" PRIu32
         " dma_ch=%d",
         opts->samplerate, opts->channels, DMIC_SAMPLE_BITS,
         opts->period_bytes, CONFIG_AIC_DMIC_CMD_DMA_CHANNEL);
  if (opts->seconds == 0) {
    printf(" duration=forever");
  } else {
    printf(" duration=%" PRIu32 "s", opts->seconds);
  }
  if (opts->outfile != NULL) {
    printf(" output=%s%s", opts->outfile, opts->wav ? " (wav)" : " (raw)");
  }
  printf("\n");
  fflush(stdout);

  g_dmic_active = &rt;
  if (hal_audio_receive_dmic_dma(&rt.audio, rt.buffer, rt.buffer_bytes,
                                 rt.period_bytes) != HAL_OK) {
    fprintf(stderr, "dmic: hal_audio_receive_dmic_dma failed: state=%u err=%u\n",
            (unsigned int)hal_audio_get_state(&rt.audio),
            (unsigned int)hal_audio_get_error(&rt.audio));
    g_dmic_active = NULL;
    ret = -EIO;
    goto out;
  }

  rt.started = true;
  ret = dmic_capture_wait(&rt, opts, target_periods);

  if (rt.started) {
    g_dmic_active = NULL;
    hal_audio_stop_dmic_dma(&rt.audio);
    rt.started = false;
  }

  if (ret == 0) {
    ret = dmic_finish_output(&rt, opts);
  }

  dmic_print_stats(&rt, opts);
  if (opts->outfile != NULL) {
    printf("dmic: saved %s\n", opts->outfile);
  }

out:
  dmic_cleanup(&rt);
  return ret;
}

static void dmic_print_regs(void) {
  struct dmic_runtime rt;

  memset(&rt, 0, sizeof(rt));
  rt.audio.regbase = AUDIO_BASE;
  rt.rxchan.hal.regbase = DMA_BASE;
  rt.rxchan.hal.init.channel_id = CONFIG_AIC_DMIC_CMD_DMA_CHANNEL;

  dmic_dump_runtime_regs(&rt, "regs");
}

int dmic_main(int argc, char *argv[]) {
  struct dmic_options opts;
  void (*old_sigint)(int);
  int ret;

  if (argc > 1 && strcmp(argv[1], "help") == 0) {
    dmic_usage();
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "regs") == 0) {
    dmic_print_regs();
    return 0;
  }

  ret = dmic_parse_args(argc, argv, &opts);
  if (ret > 0) {
    return 0;
  }

  if (ret < 0) {
    dmic_usage();
    return 1;
  }

  g_dmic_stop = 0;
  old_sigint = signal(SIGINT, dmic_signal_handler);
  ret = dmic_start_capture(&opts);
  signal(SIGINT, old_sigint);

  return ret < 0 ? 1 : 0;
}
