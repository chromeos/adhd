/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/dsp/eq2.h"

#include <errno.h>
#include <stdlib.h>

#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/rust/dsp.h"
#include "user/eq.h"

int eq2_convert_channel_response(const struct eq2* eq2,
                                 int32_t* bq_cfg,
                                 int channel) {
  float accumulated_gain = 1.0;
  int ret;

  for (int i = 0; i < eq2_len(eq2, channel); i++) {
    const struct biquad* bq = eq2_get_bq(eq2, channel, i);

    /* For i = 0..(n-2), accumulated_gain is kept accumulating in loop.
     * For i = n-1, the last biquad element, accumulated_gain is dumped to the
     * converted blob by calling biquad_convert_blob() with dump_gain = 1.
     * To prevent the sample saturation on each node across the series of biquad
     * as the channel response intermediate nodes, considering that DSP EQ is
     * the fixed-point design.
     */
    ret = biquad_convert_blob(bq, bq_cfg, &accumulated_gain,
                              (i == eq2_len(eq2, channel) - 1) /* dump_gain */);
    if (ret < 0) {
      return ret;
    }
    bq_cfg += SOF_EQ_IIR_NBIQUAD;
  }
  return 0;
}

int eq2_convert_params_to_blob(const struct eq2* eq2,
                               uint32_t** config,
                               size_t* config_size) {
  const size_t biquad_size = sizeof(struct sof_eq_iir_biquad);
  const size_t eq_iir_hdr_size = sizeof(struct sof_eq_iir_header);
  const size_t eq_cfg_hdr_size = sizeof(struct sof_eq_iir_config);

  if (!eq2) {
    return -ENOENT;
  }

  if (eq2_len(eq2, 0) <= 0 || eq2_len(eq2, 1) <= 0) {
    return -ENODATA;
  }

  size_t response_config_size[EQ2_NUM_CHANNELS] = {
      eq_iir_hdr_size + eq2_len(eq2, 0) * biquad_size, /* response of ch-0 */
      eq_iir_hdr_size + eq2_len(eq2, 1) * biquad_size  /* response of ch-1 */
  };

  size_t size =
      eq_cfg_hdr_size +                     /* sof_eq_iir_config header */
      EQ2_NUM_CHANNELS * sizeof(uint32_t) + /* assign_response[channels] */
      response_config_size[0] +             /* 1st response config data */
      response_config_size[1];              /* 2nd response config data */

  struct sof_eq_iir_config* eq_config =
      (struct sof_eq_iir_config*)calloc(1, size);
  if (!eq_config) {
    return -ENOMEM;
  }

  /* Fill sof_eq_iir_config header. */
  eq_config->size = size;
  eq_config->channels_in_config = EQ2_NUM_CHANNELS;
  eq_config->number_of_responses = EQ2_NUM_CHANNELS;

  /* Fill assign_response[channels]. */
  eq_config->data[0] = 0; /* assign response-0 to ch-0 */
  eq_config->data[1] = 1; /* assign response-1 to ch-1 */

  /* Fill config data per response. */
  struct sof_eq_iir_header* eq_hdr =
      (struct sof_eq_iir_header*)(&eq_config->data[2]);
  int ret;
  for (int channel = 0; channel < EQ2_NUM_CHANNELS; channel++) {
    /* Fill the header information. */
    eq_hdr->num_sections = eq2_len(eq2, channel);
    eq_hdr->num_sections_in_series = eq2_len(eq2, channel);

    /* Fill sof_eq_iir_biquad for biquads in one channel. */
    ret = eq2_convert_channel_response(eq2, eq_hdr->biquads, channel);
    if (ret < 0) {
      free(eq_config);
      return ret;
    }

    /* Move the address to the next sof_eq_iir_header element. */
    eq_hdr = (struct sof_eq_iir_header*)((uint8_t*)eq_hdr +
                                         response_config_size[channel]);
  }

  *config = (uint32_t*)eq_config;
  *config_size = size;
  return 0;
}
