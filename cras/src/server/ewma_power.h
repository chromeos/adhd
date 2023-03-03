/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_EWMA_POWER_H_
#define CRAS_SRC_SERVER_EWMA_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include "cras/src/server/cras_audio_area.h"

/*
 * The exponentially weighted moving average power module used to
 * calculate the energe level in audio stream.
 */
struct ewma_power {
  // Flag to note if the first power value has set.
  bool power_set;
  // Flag to enable ewma calculation. Set to false to
  // make all calculations no-ops.
  bool enabled;
  // The power value.
  float power;
  // How many frames to sample one for EWMA calculation.
  unsigned int step_fr;
  // The sample format of audio data.
  snd_pcm_format_t fmt;
};

/*
 * Disables the ewma instance.
 */
void ewma_power_disable(struct ewma_power* ewma);

/*
 * Initializes the ewma_power object.
 * Args:
 *    ewma - The ewma_power object to initialize.
 *    fmt - The sample format of the audio data.
 *    rate - The sample rate of the audio data that the ewma object
 *        will calculate power from.
 */
void ewma_power_init(struct ewma_power* ewma,
                     snd_pcm_format_t fmt,
                     unsigned int rate);

/*
 * Feeds an audio buffer to ewma_power object to calculate the
 * latest power value.
 * Args:
 *    ewma - The ewma_power object to calculate power.
 *    buf - Pointer to the audio data.
 *    channels - Number of channels of the audio data.
 *    size - Length in frames of the audio data.
 */
void ewma_power_calculate(struct ewma_power* ewma,
                          const int16_t* buf,
                          unsigned int channels,
                          unsigned int size);

/*
 * Feeds non-interleaved audio data to ewma_power to calculate the
 * latest power value. This is similar to ewma_power_calculate but
 * accepts cras_audio_area.
 */
void ewma_power_calculate_area(struct ewma_power* ewma,
                               const int16_t* buf,
                               struct cras_audio_area* area,
                               unsigned int size);

#endif  // CRAS_SRC_SERVER_EWMA_POWER_H_
