/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_audio_format.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

/* Table for allowed alternatives when doing channel re-mapping.
 * When channel_alt[X][Y] is non-zero, it's allowed to map channel
 * from X to Y. */
static const int channel_alt[CRAS_CH_MAX][CRAS_CH_MAX] = {
    // FL FR RL RR FC LFE SL SR RC FLC FRC
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // FL
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // FR
    {0, 0, 0, 0, 0, 0, +1, 0, 0, 0, +0},  // RL
    {0, 0, 0, 0, 0, 0, +0, 1, 0, 0, +0},  // RR
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // FC
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // LFE
    {0, 0, 1, 0, 0, 0, +0, 0, 0, 0, +0},  // SL
    {0, 0, 0, 1, 0, 0, +0, 0, 0, 0, +0},  // SR
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // RC
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // FLC
    {0, 0, 0, 0, 0, 0, +0, 0, 0, 0, +0},  // FRC
};

// Create an audio format structure.
struct cras_audio_format* cras_audio_format_create(snd_pcm_format_t format,
                                                   size_t frame_rate,
                                                   size_t num_channels) {
  struct cras_audio_format* fmt;
  unsigned i;

  fmt = (struct cras_audio_format*)calloc(1, sizeof(*fmt));
  if (fmt == NULL) {
    return fmt;
  }

  fmt->format = format;
  fmt->frame_rate = frame_rate;
  fmt->num_channels = num_channels;

  /* Set a default working channel layout according to num_channels.
   * Initialize all other channel position to -1(not set)
   */
  for (i = 0; i < CRAS_CH_MAX; i++) {
    fmt->channel_layout[i] = (i < num_channels) ? i : -1;
  }

  return fmt;
}

int cras_audio_format_set_channel_layout(struct cras_audio_format* format,
                                         const int8_t layout[CRAS_CH_MAX]) {
  int i;

  /* Check that the max channel index should not exceed the
   * channel count set in format.
   */
  for (i = 0; i < CRAS_CH_MAX; i++) {
    if (layout[i] < -1 || layout[i] >= (int)format->num_channels) {
      return -EINVAL;
    }
  }

  for (i = 0; i < CRAS_CH_MAX; i++) {
    format->channel_layout[i] = layout[i];
  }

  return 0;
}

// Verifies if all channel_layout[i] are in [-1, fmt->num_channels).
bool cras_audio_format_valid(const struct cras_audio_format* fmt) {
  int i;
  for (i = 0; i < CRAS_CH_MAX; i++) {
    if (fmt->channel_layout[i] < -1 ||
        fmt->channel_layout[i] >= (int)fmt->num_channels) {
      return false;
    }
  }
  return true;
}

// Destroy an audio format struct created with cras_audio_format_crate.
void cras_audio_format_destroy(struct cras_audio_format* fmt) {
  free(fmt);
}

float** cras_channel_conv_matrix_alloc(size_t in_ch, size_t out_ch) {
  size_t i;
  float** p;
  p = (float**)calloc(out_ch, sizeof(*p));
  if (p == NULL) {
    return NULL;
  }
  for (i = 0; i < out_ch; i++) {
    p[i] = (float*)calloc(in_ch, sizeof(*p[i]));
    if (p[i] == NULL) {
      goto alloc_err;
    }
  }
  return p;

alloc_err:
  if (p) {
    cras_channel_conv_matrix_destroy(p, out_ch);
  }
  return NULL;
}

void cras_channel_conv_matrix_destroy(float** p, size_t out_ch) {
  size_t i;
  for (i = 0; i < out_ch; i++) {
    free(p[i]);
  }
  free(p);
}

float** cras_channel_conv_matrix_create(const struct cras_audio_format* in,
                                        const struct cras_audio_format* out) {
  int i;
  float** mtx;

  for (i = 0; i < CRAS_CH_MAX; i++) {
    if (in->channel_layout[i] >= (int)in->num_channels ||
        out->channel_layout[i] >= (int)out->num_channels) {
      syslog(LOG_ERR,
             "Fail to create conversion matrix "
             "due to invalid channel layout");
      return NULL;
    }
  }

  mtx = cras_channel_conv_matrix_alloc(in->num_channels, out->num_channels);

  /* For the in/out format pair which has the same set of channels
   * in use, create a permutation matrix for them.
   */
  for (i = 0; i < CRAS_CH_MAX; i++) {
    if (in->channel_layout[i] == -1 && out->channel_layout[i] == -1) {
      continue;
    }
    if (in->channel_layout[i] != -1 && out->channel_layout[i] != -1) {
      mtx[out->channel_layout[i]][in->channel_layout[i]] = 1;
    } else if (in->channel_layout[i] != -1) {
      /* When the same channel does not appear at output
       * channel layout. Look up for allowed channel
       * alternatives.
       */
      int alt;
      for (alt = 0; alt < CRAS_CH_MAX; alt++) {
        if (channel_alt[i][alt] && in->channel_layout[alt] == -1 &&
            out->channel_layout[alt] != -1) {
          mtx[out->channel_layout[alt]][in->channel_layout[i]] = 1;
          break;
        }
      }
      /* Drop the channel (except for CRAS_CH_FC)
       * if no allowed channel alternatives
       * can be found for it. CRAS_CH_FC can
       * not be dropped as it usually contains the dialog
       * content which is critical.
       */
      if (alt == CRAS_CH_MAX) {
        if (i == CRAS_CH_FC) {
          goto fail;
        } else {
          syslog(LOG_ERR,
                 "No allowed channel alternatives. Dropped channel %d.", i);
          continue;
        }
      }
    }
  }

  return mtx;
fail:
  cras_channel_conv_matrix_destroy(mtx, out->num_channels);
  return NULL;
}

size_t cras_audio_format_get_least_num_channels(
    const struct cras_audio_format* fmt) {
  size_t max_channel = 0;
  for (int i = 0; i < CRAS_CH_MAX; i++) {
    if (fmt->channel_layout[i] + 1 > max_channel) {
      max_channel = fmt->channel_layout[i] + 1;
    }
  }
  return max_channel;
}
