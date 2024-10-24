/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/linear_resampler.h"

#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

#include "cras/common/check.h"

// A linear resampler.
struct linear_resampler {
  // The number of channels in once frames.
  unsigned int num_channels;
  // The size of one frame in bytes.
  unsigned int format_bytes;
  // The byte-width of the format;
  unsigned int format_width;
  // The accumulated offset for resampled src data.
  unsigned int src_offset;
  // The accumulated offset for resampled dst data.
  unsigned int dst_offset;
  // The numerator of the rate factor used for SRC.
  unsigned int to_times_100;
  // The denominator of the rate factor used for SRC.
  unsigned int from_times_100;
  // The rate factor used for linear resample.
  float f;
};

struct linear_resampler* linear_resampler_create(unsigned int num_channels,
                                                 unsigned int format_bytes,
                                                 float src_rate,
                                                 float dst_rate) {
  struct linear_resampler* lr;

  lr = (struct linear_resampler*)calloc(1, sizeof(*lr));
  if (!lr) {
    return NULL;
  }
  lr->num_channels = num_channels;
  lr->format_bytes = format_bytes;
  lr->format_width = format_bytes / num_channels;
  // Only support 16 and 32 bits for now.
  if (lr->format_width != 2 && lr->format_width != 4) {
    syslog(LOG_WARNING,
           "The format byte-width %u is not supported by the linear resampler",
           lr->format_width);
    free(lr);
    return NULL;
  }

  linear_resampler_set_rates(lr, src_rate, dst_rate);

  return lr;
}

void linear_resampler_destroy(struct linear_resampler* lr) {
  if (lr) {
    free(lr);
  }
}

void linear_resampler_set_rates(struct linear_resampler* lr,
                                float from,
                                float to) {
  lr->f = (float)to / from;
  lr->to_times_100 = to * 100;
  lr->from_times_100 = from * 100;
  lr->src_offset = 0;
  lr->dst_offset = 0;
}

/* Assuming the linear resampler transforms X frames of input buffer into
 * Y frames of output buffer. The resample method requires the last output
 * buffer at Y-1 be interpolated from input buffer in range (X-d, X-1) as
 * illustrated.
 *    Input Index:    ...      X-1 <--floor--|   X
 *    Output Index:   ... Y-1   |--ceiling-> Y
 *
 * That said, the calculation between input and output frames is based on
 * equations X-1 = floor(Y/f) and Y = ceil((X-1)*f).  Note that in any case
 * when the resampled frames number isn't sufficient to consume the first
 * buffer at input or output offset(index 0), always count as one buffer
 * used so the input/output offset can always increment.
 */
unsigned int linear_resampler_out_frames_to_in(struct linear_resampler* lr,
                                               unsigned int frames) {
  float in_frames;
  if (frames == 0) {
    return 0;
  }

  in_frames = (float)(lr->dst_offset + frames) / lr->f;
  if ((in_frames > lr->src_offset)) {
    return 1 + (unsigned int)(in_frames - lr->src_offset);
  } else {
    return 1;
  }
}

unsigned int linear_resampler_in_frames_to_out(struct linear_resampler* lr,
                                               unsigned int frames) {
  float out_frames;
  if (frames == 0) {
    return 0;
  }

  out_frames = lr->f * (lr->src_offset + frames - 1);
  if (out_frames > lr->dst_offset) {
    return 1 + (unsigned int)(out_frames - lr->dst_offset);
  } else {
    return 1;
  }
}

int linear_resampler_needed(struct linear_resampler* lr) {
  return lr->from_times_100 != lr->to_times_100;
}

unsigned int linear_resampler_resample(struct linear_resampler* lr,
                                       uint8_t* src,
                                       unsigned int* src_frames,
                                       uint8_t* dst,
                                       unsigned dst_frames) {
  int ch;
  unsigned int src_idx = 0;
  unsigned int dst_idx = 0;
  double src_pos;
  int16_t *in, *out;
  int32_t *in32, *out32;

  /* Check for corner cases so that we can assume both src_idx and
   * dst_idx are valid with value 0 in the loop below. */
  if (dst_frames == 0 || *src_frames == 0) {
    *src_frames = 0;
    return 0;
  }

  for (dst_idx = 0; dst_idx <= dst_frames; dst_idx++) {
    src_pos = (double)(lr->dst_offset + dst_idx) / lr->f;
    if (src_pos > lr->src_offset) {
      src_pos -= lr->src_offset;
    } else {
      src_pos = 0;
    }
    src_idx = (unsigned int)src_pos;

    if (src_pos > *src_frames - 1 || dst_idx >= dst_frames) {
      if (src_pos > *src_frames - 1) {
        src_idx = *src_frames - 1;
      }
      /* When this loop stops, dst_idx is always at the last
       * used index incremented by 1. */
      break;
    }

    if (lr->format_width == 2) {  // 16bits
      in = (int16_t*)(src + src_idx * lr->format_bytes);
      out = (int16_t*)(dst + dst_idx * lr->format_bytes);
      /* Don't do linear interpolcation if src_pos falls on the
       * last index. */
      if (src_idx == *src_frames - 1) {
        for (ch = 0; ch < lr->num_channels; ch++) {
          out[ch] = in[ch];
        }
      } else {
        for (ch = 0; ch < lr->num_channels; ch++) {
          out[ch] = in[ch] +
                    (src_pos - src_idx) * (in[lr->num_channels + ch] - in[ch]);
        }
      }
    } else if (lr->format_width == 4) {  // 24 or 32bits
      in32 = (int32_t*)(src + src_idx * lr->format_bytes);
      out32 = (int32_t*)(dst + dst_idx * lr->format_bytes);
      CRAS_CHECK(((uintptr_t)in32) % _Alignof(int32_t) == 0);
      CRAS_CHECK(((uintptr_t)out32) % _Alignof(int32_t) == 0);
      /* Don't do linear interpolcation if src_pos falls on the
       * last index. */
      if (src_idx == *src_frames - 1) {
        for (ch = 0; ch < lr->num_channels; ch++) {
          out32[ch] = in32[ch];
        }
      } else {
        for (ch = 0; ch < lr->num_channels; ch++) {
          out32[ch] = in32[ch] + (src_pos - src_idx) *
                                     (in32[lr->num_channels + ch] - in32[ch]);
        }
      }
    }
  }

  *src_frames = src_idx + 1;

  lr->src_offset += *src_frames;
  lr->dst_offset += dst_idx;
  while ((lr->src_offset > lr->from_times_100) &&
         (lr->dst_offset > lr->to_times_100)) {
    lr->src_offset -= lr->from_times_100;
    lr->dst_offset -= lr->to_times_100;
  }

  return dst_idx;
}
