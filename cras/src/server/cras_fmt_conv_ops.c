/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_fmt_conv_ops.h"

#include <endian.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "cras_audio_format.h"

#define MAX(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })
#define MIN(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })

/*
 * Add and clip.
 */
static int16_t s16_add_and_clip(int16_t a, int16_t b) {
  int32_t sum;

  a = htole16(a);
  b = htole16(b);
  sum = (int32_t)a + (int32_t)b;
  sum = MAX(sum, SHRT_MIN);
  sum = MIN(sum, SHRT_MAX);
  return (int16_t)le16toh(sum);
}

/*
 * Format converter.
 */
void convert_u8_to_s16le(const uint8_t* in, size_t in_samples, uint8_t* out) {
  size_t i;
  uint16_t* _out = (uint16_t*)out;

  for (i = 0; i < in_samples; i++, in++, _out++) {
    *_out = (uint16_t)((int16_t)*in - 0x80) << 8;
  }
}

void convert_s243le_to_s16le(const uint8_t* in,
                             size_t in_samples,
                             uint8_t* out) {
  /* find how to calculate in and out size, implement the conversion
   * between S24_3LE and S16 */

  size_t i;
  int8_t* _in = (int8_t*)in;
  uint16_t* _out = (uint16_t*)out;

  for (i = 0; i < in_samples; i++, _in += 3, _out++) {
    memcpy(_out, _in + 1, 2);
  }
}

void convert_s24le_to_s16le(const uint8_t* in,
                            size_t in_samples,
                            uint8_t* out) {
  size_t i;
  int32_t* _in = (int32_t*)in;
  uint16_t* _out = (uint16_t*)out;

  for (i = 0; i < in_samples; i++, _in++, _out++) {
    *_out = (int16_t)((*_in & 0x00ffffff) >> 8);
  }
}

void convert_s32le_to_s16le(const uint8_t* in,
                            size_t in_samples,
                            uint8_t* out) {
  size_t i;
  int32_t* _in = (int32_t*)in;
  uint16_t* _out = (uint16_t*)out;

  for (i = 0; i < in_samples; i++, _in++, _out++) {
    *_out = (int16_t)(*_in >> 16);
  }
}

void convert_s16le_to_u8(const uint8_t* in, size_t in_samples, uint8_t* out) {
  size_t i;
  int16_t* _in = (int16_t*)in;

  for (i = 0; i < in_samples; i++, _in++, out++) {
    *out = (uint8_t)(*_in >> 8) + 128;
  }
}

void convert_s16le_to_s243le(const uint8_t* in,
                             size_t in_samples,
                             uint8_t* out) {
  size_t i;
  int16_t* _in = (int16_t*)in;
  uint8_t* _out = (uint8_t*)out;

  for (i = 0; i < in_samples; i++, _in++, _out += 3) {
    *_out = 0;
    memcpy(_out + 1, _in, 2);
  }
}

void convert_s16le_to_s24le(const uint8_t* in,
                            size_t in_samples,
                            uint8_t* out) {
  size_t i;
  int16_t* _in = (int16_t*)in;
  uint32_t* _out = (uint32_t*)out;

  for (i = 0; i < in_samples; i++, _in++, _out++) {
    *_out = ((uint32_t)(int32_t)*_in << 8);
  }
}

void convert_s16le_to_s32le(const uint8_t* in,
                            size_t in_samples,
                            uint8_t* out) {
  size_t i;
  int16_t* _in = (int16_t*)in;
  uint32_t* _out = (uint32_t*)out;

  for (i = 0; i < in_samples; i++, _in++, _out++) {
    *_out = ((uint32_t)(int32_t)*_in << 16);
  }
}

void convert_s16le_to_f32le(const int16_t* in, size_t in_samples, float* out) {
  for (int i = 0; i < in_samples; ++i) {
    out[i] = (float)in[i] / 32768.f;
  }
}

void convert_f32le_to_s16le(const float* in, size_t in_samples, int16_t* out) {
  for (int i = 0; i < in_samples; ++i) {
    out[i] = (int16_t)MAX(MIN(in[i] * 32768.f, INT16_MAX), INT16_MIN);
  }
}

/*
 * Channel converter: mono to stereo.
 */
size_t s16_mono_to_stereo(const uint8_t* _in, size_t in_frames, uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  for (i = 0; i < in_frames; i++) {
    out[2 * i] = in[i];
    out[2 * i + 1] = in[i];
  }
  return in_frames;
}

/*
 * Channel converter: stereo to mono.
 */
size_t s16_stereo_to_mono(const uint8_t* _in, size_t in_frames, uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  for (i = 0; i < in_frames; i++) {
    out[i] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
  }
  return in_frames;
}

/*
 * Channel converter: mono to 5 channels.
 *
 * Fit the left/right of input to the front left/right of output respectively
 * and fill others with zero.
 */
size_t s16_mono_to_5(size_t left,
                     size_t right,
                     const uint8_t* _in,
                     size_t in_frames,
                     uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 5 * in_frames);

  if (left == -1 || right == -1) {
    /*
     * Select the first two channels to convert to as the
     * default behavior.
     */
    left = 0;
    right = 1;
  }
  for (i = 0; i < in_frames; i++) {
    out[5 * i + left] = in[i];
    out[5 * i + right] = in[i];
  }

  return in_frames;
}

/*
 * Channel converter: mono to 5.1 surround.
 *
 * Fit mono to front center of the output, or split to front left/right
 * if front center is missing from the output channel layout.
 */
size_t s16_mono_to_51(size_t left,
                      size_t right,
                      size_t center,
                      const uint8_t* _in,
                      size_t in_frames,
                      uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 6 * in_frames);

  if (center != -1) {
    for (i = 0; i < in_frames; i++) {
      out[6 * i + center] = in[i];
    }
  } else if (left != -1 && right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[6 * i + right] = in[i] / 2;
      out[6 * i + left] = in[i] / 2;
    }
  } else {
    /* Select the first channel to convert to as the
     * default behavior.
     */
    for (i = 0; i < in_frames; i++) {
      out[6 * i] = in[i];
    }
  }

  return in_frames;
}

/*
 * Channel converter: stereo to 5 channels.
 *
 * Fit the left/right of input to the front left/right of output respectively
 * and fill others with zero.
 */
size_t s16_stereo_to_5(size_t left,
                       size_t right,
                       const uint8_t* _in,
                       size_t in_frames,
                       uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 5 * in_frames);

  if (left == -1 || right == -1) {
    /*
     * Select the first two channels to convert to as the
     * default behavior.
     */
    left = 0;
    right = 1;
  }
  for (i = 0; i < in_frames; i++) {
    out[5 * i + left] = in[2 * i];
    out[5 * i + right] = in[2 * i + 1];
  }

  return in_frames;
}

/*
 * Channel converter: stereo to 5.1 surround.
 *
 * Fit the left/right of input to the front left/right of output respectively
 * and fill others with zero. If any of the front left/right is missed from
 * the output channel layout, mix to front center.
 */
size_t s16_stereo_to_51(size_t left,
                        size_t right,
                        size_t center,
                        const uint8_t* _in,
                        size_t in_frames,
                        uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 6 * in_frames);

  if (left != -1 && right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[6 * i + left] = in[2 * i];
      out[6 * i + right] = in[2 * i + 1];
    }
  } else if (center != -1) {
    for (i = 0; i < in_frames; i++) {
      out[6 * i + center] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
    }
  } else {
    /* Select the first two channels to convert to as the
     * default behavior.
     */
    for (i = 0; i < in_frames; i++) {
      out[6 * i] = in[2 * i];
      out[6 * i + 1] = in[2 * i + 1];
    }
  }

  return in_frames;
}

/*
 * Channel converter: quad to 5.1 surround.
 *
 * Fit the front left/right of input to the front left/right of output
 * and rear left/right of input to the rear left/right of output
 * respectively and fill others with zero.
 */
size_t s16_quad_to_51(size_t front_left,
                      size_t front_right,
                      size_t rear_left,
                      size_t rear_right,
                      const uint8_t* _in,
                      size_t in_frames,
                      uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 6 * in_frames);

  if (front_left != -1 && front_right != -1 && rear_left != -1 &&
      rear_right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[6 * i + front_left] = in[4 * i];
      out[6 * i + front_right] = in[4 * i + 1];
      out[6 * i + rear_left] = in[4 * i + 2];
      out[6 * i + rear_right] = in[4 * i + 3];
    }
  } else {
    /* Use default 5.1 channel mapping for the conversion.
     */
    for (i = 0; i < in_frames; i++) {
      out[6 * i] = in[4 * i];
      out[6 * i + 1] = in[4 * i + 1];
      out[6 * i + 4] = in[4 * i + 2];
      out[6 * i + 5] = in[4 * i + 3];
    }
  }

  return in_frames;
}

/*
 * Channel converter: mono to 7.1 surround.
 *
 * Fit mono to front center of the output, or split to front left/right
 * if front center is missing from the output channel layout.
 */
size_t s16_mono_to_71(size_t left,
                      size_t right,
                      size_t center,
                      const uint8_t* _in,
                      size_t in_frames,
                      uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 8 * in_frames);

  if (center != -1) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + center] = in[i];
    }
  } else if (left != -1 && right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + right] = in[i] / 2;
      out[8 * i + left] = in[i] / 2;
    }
  } else {
    /* Select the first channel to convert to as the
     * default behavior.
     */
    for (i = 0; i < in_frames; i++) {
      out[8 * i] = in[i];
    }
  }

  return in_frames;
}

/*
 * Channel converter: stereo to 7.1 surround.
 *
 * Fit the left/right of input to the front left/right of output respectively
 * and fill others with zero. If any of the front left/right is missed from
 * the output channel layout, mix to front center.
 */
size_t s16_stereo_to_71(size_t left,
                        size_t right,
                        size_t center,
                        const uint8_t* _in,
                        size_t in_frames,
                        uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 8 * in_frames);

  if (left != -1 && right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + left] = in[2 * i];
      out[8 * i + right] = in[2 * i + 1];
    }
  } else if (center != -1) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + center] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
    }
  } else {
    /* Select the first two channels to convert to as the
     * default behavior.
     */
    for (i = 0; i < in_frames; i++) {
      out[8 * i] = in[2 * i];
      out[8 * i + 1] = in[2 * i + 1];
    }
  }

  return in_frames;
}

/*
 * Channel converter: quad to 7.1 surround.
 *
 * Fit the front left/right of input to the front left/right of output
 * and rear left/right of input to the rear left/right of output
 * respectively and fill others with zero.
 */
size_t s16_quad_to_71(size_t front_left,
                      size_t front_right,
                      size_t rear_left,
                      size_t rear_right,
                      const uint8_t* _in,
                      size_t in_frames,
                      uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 8 * in_frames);

  if (front_left != -1 && front_right != -1 && rear_left != -1 &&
      rear_right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + front_left] = in[4 * i];
      out[8 * i + front_right] = in[4 * i + 1];
      out[8 * i + rear_left] = in[4 * i + 2];
      out[8 * i + rear_right] = in[4 * i + 3];
    }
  } else {
    /* Use default 7.1 channel mapping for the conversion.
     */
    for (i = 0; i < in_frames; i++) {
      out[8 * i] = in[4 * i];
      out[8 * i + 1] = in[4 * i + 1];
      out[8 * i + 4] = in[4 * i + 2];
      out[8 * i + 5] = in[4 * i + 3];
    }
  }

  return in_frames;
}

/*
 * Channel converter: 5.1 to 7.1 surround.
 *
 * Fit the FL, FR, FC, LFE, RL/SL, RR/SR channels and fill others with zero.
 * If any of those is missed from the output channel layout, use
 * default 5.1 mapping.
 */
size_t s16_51_to_71(const struct cras_audio_format* in_fmt,
                    const struct cras_audio_format* out_fmt,
                    const uint8_t* _in,
                    size_t in_frames,
                    uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 8 * in_frames);

  size_t fl_51 = in_fmt->channel_layout[CRAS_CH_FL];
  size_t fr_51 = in_fmt->channel_layout[CRAS_CH_FR];
  size_t fc_51 = in_fmt->channel_layout[CRAS_CH_FC];
  size_t lfe_51 = in_fmt->channel_layout[CRAS_CH_LFE];
  size_t rl_51 = in_fmt->channel_layout[CRAS_CH_RL];
  size_t rr_51 = in_fmt->channel_layout[CRAS_CH_RR];
  size_t sl_51 = in_fmt->channel_layout[CRAS_CH_SL];
  size_t sr_51 = in_fmt->channel_layout[CRAS_CH_SR];

  size_t fl_71 = out_fmt->channel_layout[CRAS_CH_FL];
  size_t fr_71 = out_fmt->channel_layout[CRAS_CH_FR];
  size_t fc_71 = out_fmt->channel_layout[CRAS_CH_FC];
  size_t lfe_71 = out_fmt->channel_layout[CRAS_CH_LFE];
  size_t rl_71 = out_fmt->channel_layout[CRAS_CH_RL];
  size_t rr_71 = out_fmt->channel_layout[CRAS_CH_RR];
  size_t sl_71 = out_fmt->channel_layout[CRAS_CH_SL];
  size_t sr_71 = out_fmt->channel_layout[CRAS_CH_SR];

  if (fl_51 != -1 && fr_51 != -1 && fc_51 != -1 && lfe_51 != -1 &&
      fl_71 != -1 && fr_71 != -1 && fc_71 != -1 && lfe_71 != -1 &&
      ((rl_51 != -1 && rl_71 != -1) || (sl_51 != -1 && sl_71 != -1)) &&
      ((rr_51 != -1 && rr_71 != -1) || (sr_51 != -1 && sr_71 != -1))) {
    for (i = 0; i < in_frames; i++) {
      out[8 * i + fl_71] = in[6 * i + fl_51];
      out[8 * i + fr_71] = in[6 * i + fr_51];
      out[8 * i + fc_71] = in[6 * i + fc_51];
      out[8 * i + lfe_71] = in[6 * i + lfe_51];
      if (rl_51 != -1 && rl_71 != -1) {
        out[8 * i + rl_71] = in[6 * i + rl_51];
      }
      if (rr_51 != -1 && rr_71 != -1) {
        out[8 * i + rr_71] = in[6 * i + rr_51];
      }
      if (sl_51 != -1 && sl_71 != -1) {
        out[8 * i + sl_71] = in[6 * i + sl_51];
      }
      if (sr_51 != -1 && sr_71 != -1) {
        out[8 * i + sr_71] = in[6 * i + sr_51];
      }
    }
  } else {
    /* Use default 7.1 channel mapping for the conversion.
     */
    for (i = 0; i < in_frames; i++) {
      out[8 * i] = in[6 * i];
      out[8 * i + 1] = in[6 * i + 1];
      out[8 * i + 2] = in[6 * i + 2];
      out[8 * i + 3] = in[6 * i + 3];
      out[8 * i + 4] = in[6 * i + 4];
      out[8 * i + 5] = in[6 * i + 5];
    }
  }

  return in_frames;
}

/*
 * Channel converter: 5.1 surround to stereo.
 *
 * The out buffer can have room for just stereo samples. This convert function
 * is used as the default behavior when channel layout is not set from the
 * client side.
 */
size_t s16_51_to_stereo(const uint8_t* _in, size_t in_frames, uint8_t* _out) {
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;
  static const unsigned int left_idx = 0;
  static const unsigned int right_idx = 1;
  static const unsigned int center_idx = 2;
  // static const unsigned int lfe_idx = 3;
  // static const unsigned int left_surround_idx = 4;
  // static const unsigned int right_surround_idx = 5;

  size_t i;
  int16_t half_center;
  /* Use the normalized_factor from the left channel = 1 / (|1| + |0.707|)
   * to prevent mixing overflow.
   */
  const float normalized_factor = 0.585;
  for (i = 0; i < in_frames; i++) {
    half_center = in[6 * i + center_idx] * 0.707 * normalized_factor;
    out[2 * i + left_idx] =
        in[6 * i + left_idx] * normalized_factor + half_center;
    out[2 * i + right_idx] =
        in[6 * i + right_idx] * normalized_factor + half_center;
  }
  return in_frames;
}

/*
 * Channel converter: 5.1 surround to quad (front L/R, rear L/R).
 *
 * The out buffer can have room for just quad samples. This convert function
 * is used as the default behavior when channel layout is not set from the
 * client side.
 */
size_t s16_51_to_quad(const uint8_t* _in, size_t in_frames, uint8_t* _out) {
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;
  static const unsigned int l_quad = 0;
  static const unsigned int r_quad = 1;
  static const unsigned int rl_quad = 2;
  static const unsigned int rr_quad = 3;

  static const unsigned int l_51 = 0;
  static const unsigned int r_51 = 1;
  static const unsigned int center_51 = 2;
  static const unsigned int lfe_51 = 3;
  static const unsigned int rl_51 = 4;
  static const unsigned int rr_51 = 5;

  /* Use normalized_factor from the left channel = 1 / (|1| + |0.707| + |0.5|)
   * to prevent overflow. */
  const float normalized_factor = 0.453;
  size_t i;
  for (i = 0; i < in_frames; i++) {
    int16_t half_center;
    int16_t lfe;

    half_center = in[6 * i + center_51] * 0.707 * normalized_factor;
    lfe = in[6 * i + lfe_51] * 0.5 * normalized_factor;
    out[4 * i + l_quad] =
        normalized_factor * in[6 * i + l_51] + half_center + lfe;
    out[4 * i + r_quad] =
        normalized_factor * in[6 * i + r_51] + half_center + lfe;
    out[4 * i + rl_quad] = normalized_factor * in[6 * i + rl_51] + lfe;
    out[4 * i + rr_quad] = normalized_factor * in[6 * i + rr_51] + lfe;
  }
  return in_frames;
}

/*
 * Channel converter: stereo to quad (front L/R, rear L/R).
 *
 * Fit left/right of input to the front left/right of output respectively
 * and fill others with zero.
 */
size_t s16_stereo_to_quad(size_t front_left,
                          size_t front_right,
                          const uint8_t* _in,
                          size_t in_frames,
                          uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  memset(out, 0, sizeof(*out) * 4 * in_frames);
  if (front_left != -1 && front_right != -1) {
    for (i = 0; i < in_frames; i++) {
      out[4 * i + front_left] = in[2 * i];
      out[4 * i + front_right] = in[2 * i + 1];
    }
  } else {
    /* Select the first two channels to convert to as the
     * default behavior.
     */
    for (i = 0; i < in_frames; i++) {
      out[4 * i] = in[2 * i];
      out[4 * i + 1] = in[2 * i + 1];
    }
  }

  return in_frames;
}

/*
 * Channel converter: quad (front L/R, rear L/R) to stereo.
 */
size_t s16_quad_to_stereo(size_t front_left,
                          size_t front_right,
                          size_t rear_left,
                          size_t rear_right,
                          const uint8_t* _in,
                          size_t in_frames,
                          uint8_t* _out) {
  size_t i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  if (front_left == -1 || front_right == -1 || rear_left == -1 ||
      rear_right == -1) {
    front_left = 0;
    front_right = 1;
    rear_left = 2;
    rear_right = 3;
  }

  for (i = 0; i < in_frames; i++) {
    out[2 * i] =
        s16_add_and_clip(in[4 * i + front_left], in[4 * i + rear_left] / 4);
    out[2 * i + 1] =
        s16_add_and_clip(in[4 * i + front_right], in[4 * i + rear_right] / 4);
  }
  return in_frames;
}

/*
 * Channel converter: N channels to M channels.
 *
 * The out buffer must have room for M channel. This convert function is used
 * as the default behavior when channel layout is not set from the client side.
 */
size_t s16_default_all_to_all(struct cras_audio_format* out_fmt,
                              size_t num_in_ch,
                              size_t num_out_ch,
                              const uint8_t* _in,
                              size_t in_frames,
                              uint8_t* _out) {
  unsigned int in_ch, out_ch, i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;
  int32_t sum;

  for (i = 0; i < in_frames; i++) {
    sum = 0;
    for (in_ch = 0; in_ch < num_in_ch; in_ch++) {
      sum += (int32_t)in[in_ch + i * num_in_ch];
    }
    /*
     * 1. Divide `int32_t` by `size_t` without an explicit
     *    conversion will generate corrupted results.
     * 2. After the division, `sum` should be in the range of
     *    int16_t. No clipping is needed.
     */
    sum /= (int32_t)num_in_ch;
    for (out_ch = 0; out_ch < num_out_ch; out_ch++) {
      out[out_ch + i * num_out_ch] = (int16_t)sum;
    }
  }
  return in_frames;
}

/*
 * Copies the input channels across output channels. Drops input channels that
 * don't fit. Ignores output channels greater than the number of input channels.
 */
size_t s16_some_to_some(const struct cras_audio_format* out_fmt,
                        const size_t num_in_ch,
                        const size_t num_out_ch,
                        const uint8_t* _in,
                        const size_t frame_count,
                        uint8_t* _out) {
  unsigned int i;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;
  const size_t num_copy_ch = MIN(num_in_ch, num_out_ch);

  memset(out, 0, frame_count * cras_get_format_bytes(out_fmt));
  for (i = 0; i < frame_count; i++, out += num_out_ch, in += num_in_ch) {
    memcpy(out, in, num_copy_ch * sizeof(int16_t));
  }

  return frame_count;
}

/*
 * Multiplies buffer vector with coefficient vector.
 */
int16_t s16_multiply_buf_with_coef(float* coef,
                                   const int16_t* buf,
                                   size_t size) {
  int32_t sum = 0;
  int i;

  for (i = 0; i < size; i++) {
    sum += coef[i] * buf[i];
  }
  sum = MAX(sum, -0x8000);
  sum = MIN(sum, 0x7fff);
  return (int16_t)sum;
}

/*
 * Channel layout converter.
 *
 * Converts channels based on the channel conversion coefficient matrix.
 */
size_t s16_convert_channels(float** ch_conv_mtx,
                            size_t num_in_ch,
                            size_t num_out_ch,
                            const uint8_t* _in,
                            size_t in_frames,
                            uint8_t* _out) {
  unsigned i, fr;
  unsigned in_idx = 0;
  unsigned out_idx = 0;
  const int16_t* in = (const int16_t*)_in;
  int16_t* out = (int16_t*)_out;

  for (fr = 0; fr < in_frames; fr++) {
    for (i = 0; i < num_out_ch; i++) {
      out[out_idx + i] =
          s16_multiply_buf_with_coef(ch_conv_mtx[i], &in[in_idx], num_in_ch);
    }
    in_idx += num_in_ch;
    out_idx += num_out_ch;
  }

  return in_frames;
}
