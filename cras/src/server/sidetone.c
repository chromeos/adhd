// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/sidetone.h"

#include <syslog.h>

#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/server_stream.h"
#include "cras/src/server/stream_list.h"
#include "cras_iodev_info.h"
#include "cras_shm.h"
#include "cras_types.h"

// When sidetone is active, both input and output share the same samples and
// samples info. But when they are about to be destroyed, they cannot share
// them because when the sample is destroyed because of input, then the output
// will not have the valid sample and will cause a crash. These two variables
// are used to temporarily save the original output samples and samples info,
// so output will not accessing the invalid sample when input has been destroyed

// We only need to save 1 pair because we can't have more than 1 output
// sidetone stream at a moment.
static uint8_t* tmp_output_samples;
static struct cras_shm_info tmp_output_samples_info;

bool enable_sidetone(struct stream_list* stream_list) {
  static struct cras_audio_format srv_stream_fmt = {
      SND_PCM_FORMAT_S16_LE,
      48000,
      2,
      {0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
  int rc = server_stream_create(stream_list, SERVER_STREAM_SIDETONE_INPUT, 0,
                                &srv_stream_fmt, 0, true);
  if (rc) {
    syslog(LOG_ERR, "Failed to create input sidetone stream");
    return false;
  }
  rc = server_stream_create(stream_list, SERVER_STREAM_SIDETONE_OUTPUT, 0,
                            &srv_stream_fmt, 0, true);
  if (rc) {
    syslog(LOG_ERR,
           "Failed to create output sidetone stream. Destroying input stream");
    server_stream_destroy(stream_list, SERVER_STREAM_SIDETONE_INPUT, 0);
    return false;
  }

  return true;
}

void disable_sidetone(struct stream_list* stream_list) {
  struct cras_rstream* output = server_stream_find_by_type(
      stream_list_get(stream_list), SERVER_STREAM_SIDETONE_OUTPUT);
  // Set the samples and samples_info of the output to be different from
  // the input, to avoid accessing invalid samples after the input is destroyed
  output->shm->samples = tmp_output_samples;
  output->shm->samples_info = tmp_output_samples_info;

  server_stream_destroy(stream_list, SERVER_STREAM_SIDETONE_INPUT, 0);
  server_stream_destroy(stream_list, SERVER_STREAM_SIDETONE_OUTPUT, 0);
}

void configure_sidetone_streams(struct cras_rstream* input,
                                struct cras_rstream* output) {
  // Save the samples and samples_info so it can be destroyed later
  tmp_output_samples = output->shm->samples;
  tmp_output_samples_info = output->shm->samples_info;

  output->shm->samples = input->shm->samples;
  output->shm->samples_info = input->shm->samples_info;
  output->pair = input;
  input->pair = output;
}

bool is_sidetone_available(enum CRAS_NODE_TYPE output_node_type) {
  switch (output_node_type) {
    case CRAS_NODE_TYPE_HEADPHONE:
    case CRAS_NODE_TYPE_ALSA_LOOPBACK:
      return 1;
    default:
      return 0;
  }
}
