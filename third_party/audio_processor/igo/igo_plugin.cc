// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igo_lib.h"
#include "plugin_processor.h"

struct igo_processor {
  struct plugin_processor p;
  struct plugin_processor_config config;
  float** in_buf;
  float** out_buf;
  struct IgoLibInfo* libInfo;
  struct IgoLibConfig* libCfg;
  struct IgoStreamData* inStream;
  struct IgoStreamData* outStream;
};

static enum status run(struct plugin_processor* p,
                       const struct multi_slice* input,
                       struct multi_slice* output) {
  struct igo_processor* igo_p = (struct igo_processor*)p;

  enum IgoRet ret = IgoLibUpdateStreamData(igo_p->libCfg, igo_p->inStream, NULL,
                                           igo_p->outStream);
  if (ret != IGO_RET_OK) {
    printf("IgoLibUpdateStreamData failed, ret = %u\n", ret);
    return ErrOther;
  }

  // Transfer input data
  for (size_t ch = 0; ch < input->channels; ch++) {
    memcpy(igo_p->in_buf[ch], input->data[ch],
           igo_p->config.block_size * sizeof(input->data[ch][0]));
  }

  ret = IgoLibProcess(igo_p->libCfg, igo_p->inStream, NULL, igo_p->outStream);
  if (ret != IGO_RET_OK) {
    printf("IgoLibProcess failed, ret = %u\n", ret);
    return ErrOther;
  }

  // For in-place processors, the output is the input
  *output = *input;

  // Transfer output data
  for (size_t ch = 0; ch < output->channels; ch++) {
    memcpy(output->data[ch], igo_p->out_buf[ch],
           igo_p->config.block_size * sizeof(igo_p->out_buf[ch][0]));
  }

  return StatusOk;
}

static enum status destroy(struct plugin_processor* p) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  struct igo_processor* igo_p = (struct igo_processor*)p;

  enum IgoRet ret = IgoLibDelete(igo_p->libCfg);
  if (ret != IGO_RET_OK) {
    printf("IgoLibDelete failed, ret = %u\n", ret);
    return ErrOther;
  }

  free(igo_p->inStream);
  free(igo_p->outStream);
  free(igo_p->libInfo);
  free(igo_p->libCfg);
  for (size_t ch = 0; ch < igo_p->config.channels; ch++) {
    free(igo_p->in_buf[ch]);
  }
  free(igo_p->in_buf);
  for (size_t ch = 0; ch < igo_p->config.channels; ch++) {
    free(igo_p->out_buf[ch]);
  }
  free(igo_p->out_buf);
  free(igo_p);

  return StatusOk;
}

static enum status get_output_frame_rate(struct plugin_processor* p,
                                         size_t* output_frame_rate) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  struct igo_processor* igo_p = (struct igo_processor*)p;

  *output_frame_rate = igo_p->outStream->sampling_rate;

  return StatusOk;
}

static const struct plugin_processor_ops ops = {
    .run = run,
    .destroy = destroy,
    .get_output_frame_rate = get_output_frame_rate,
};

extern "C" enum status plugin_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  struct igo_processor* igo_p =
      (struct igo_processor*)(calloc(1, sizeof(*igo_p)));
  if (!igo_p) {
    return ErrOutOfMemory;
  }

  // Check config
  // printf("block_size = %zu, frame_rate = %zu\n", config->block_size,
  // config->frame_rate);

  igo_p->config = *config;

  // Allocate in_buf
  float** in_buf = (float**)(calloc(config->channels, sizeof(*in_buf)));
  if (!in_buf) {
    return ErrOutOfMemory;
  }

  for (size_t ch = 0; ch < config->channels; ch++) {
    in_buf[ch] = (float*)(calloc(config->block_size, sizeof(**in_buf)));
    if (!in_buf) {
      return ErrOutOfMemory;
    }
  }
  igo_p->in_buf = in_buf;

  // Allocate out_buf
  float** out_buf = (float**)(calloc(config->channels, sizeof(*out_buf)));
  if (!out_buf) {
    return ErrOutOfMemory;
  }

  for (size_t ch = 0; ch < config->channels; ch++) {
    out_buf[ch] = (float*)(calloc(config->block_size, sizeof(**out_buf)));
    if (!out_buf) {
      return ErrOutOfMemory;
    }
  }
  igo_p->out_buf = out_buf;

  // Allocate libInfo
  struct IgoLibInfo* libInfo =
      (struct IgoLibInfo*)(calloc(1, sizeof(*libInfo)));
  if (!libInfo) {
    return ErrOutOfMemory;
  }
  igo_p->libInfo = libInfo;

  // Allocate libCfg
  struct IgoLibConfig* libCfg =
      (struct IgoLibConfig*)(calloc(1, sizeof(*libCfg)));
  if (!libCfg) {
    return ErrOutOfMemory;
  }
  igo_p->libCfg = libCfg;

  // Allocate inStream
  struct IgoStreamData* inStream =
      (struct IgoStreamData*)(calloc(config->channels, sizeof(*inStream)));
  if (!inStream) {
    return ErrOutOfMemory;
  }
  igo_p->inStream = inStream;

  // Allocate outStream
  struct IgoStreamData* outStream =
      (struct IgoStreamData*)(calloc(config->channels, sizeof(*outStream)));
  if (!outStream) {
    return ErrOutOfMemory;
  }
  igo_p->outStream = outStream;

  enum IgoRet ret = IgoLibGetInfo(igo_p->libInfo);
  if (ret != IGO_RET_OK) {
    printf("IgoLibGetInfo failed, ret = %u\n", ret);
    return ErrOther;
  }

  igo_p->libCfg->in_ch_num = igo_p->libInfo->max_in_ch_num;
  igo_p->libCfg->out_ch_num = igo_p->libInfo->max_out_ch_num;

  for (size_t ch = 0; ch < config->channels; ch++) {
    igo_p->inStream[ch].data = igo_p->in_buf[ch];
    igo_p->inStream[ch].data_width = IGO_DATA_FLOAT32;
    igo_p->inStream[ch].sample_num = config->block_size;
    igo_p->inStream[ch].sampling_rate = config->frame_rate;

    igo_p->outStream[ch].data = igo_p->out_buf[ch];
    igo_p->outStream[ch].data_width = IGO_DATA_FLOAT32;
    igo_p->outStream[ch].sample_num = config->block_size;
    igo_p->outStream[ch].sampling_rate = config->frame_rate;
  }

  ret = IgoLibNew(igo_p->libCfg, igo_p->inStream, NULL, igo_p->outStream);
  if (ret != IGO_RET_OK) {
    printf("IgoLibNew failed, ret = %u\n", ret);
    return ErrOther;
  }

  igo_p->p.ops = &ops;
  *out = &igo_p->p;

  return StatusOk;
}
