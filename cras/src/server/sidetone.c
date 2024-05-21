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
  // Set shm->samples to null to avoid double munmap.
  // shm->samples_info is safe for double cras_shm_info_cleanup.
  struct cras_rstream* input = server_stream_find_by_type(
      stream_list_get(stream_list), SERVER_STREAM_SIDETONE_INPUT);
  input->shm->samples = NULL;

  server_stream_destroy(stream_list, SERVER_STREAM_SIDETONE_INPUT, 0);
  server_stream_destroy(stream_list, SERVER_STREAM_SIDETONE_OUTPUT, 0);
}

void configure_sidetone_streams(struct cras_rstream* input,
                                struct cras_rstream* output) {
  cras_audio_shm_samples_destroy(output->shm);
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
