/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_AUDIO_CODEC_H_
#define CRAS_SRC_COMMON_CRAS_AUDIO_CODEC_H_

#include <stddef.h>

// A audio codec that transforms audio between different formats.
struct cras_audio_codec {
  // Function to decode audio samples. Returns the number of decoded
  // bytes of input buffer, number of decoded bytes of output buffer
  // will be filled in count.
  int (*decode)(struct cras_audio_codec* codec,
                const void* input,
                size_t input_len,
                void* output,
                size_t output_len,
                size_t* count);
  // Function to encode audio samples. Returns the number of encoded
  // bytes of input buffer, number of encoded bytes of output buffer
  // will be filled in count.
  int (*encode)(struct cras_audio_codec* codec,
                const void* input,
                size_t intput_len,
                void* output,
                size_t output_len,
                size_t* count);
  // Private data for specific use.
  void* priv_data;
};

#endif  // COMMON_CRAS_SRC_COMMON_CRAS_AUDIO_CODEC_H_
