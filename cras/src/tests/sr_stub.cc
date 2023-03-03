/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <stdlib.h>

extern "C" {
#include "cras/src/common/sample_buffer.h"
#include "cras/src/tests/sr_stub.h"
}

// Fake implementation of cras_sr

extern "C" {
struct cras_sr {
  int16_t fake_output_value;
  float sample_rate_scale;
  size_t num_frames_per_run;
};

struct cras_sr* cras_sr_create(const struct cras_sr_model_spec spec,
                               const size_t input_nbytes) {
  struct cras_sr* sr = (struct cras_sr*)calloc(1, sizeof(struct cras_sr));
  sr->fake_output_value = 1;
  sr->sample_rate_scale =
      (float)spec.output_sample_rate / (float)spec.input_sample_rate;
  sr->num_frames_per_run = 480;
  return sr;
}

void cras_sr_destroy(struct cras_sr* sr) {
  free(sr);
}

int cras_sr_process(struct cras_sr* sr,
                    struct byte_buffer* input_buf,
                    struct byte_buffer* output_buf) {
  struct sample_buffer in_buf =
      sample_buffer_weak_ref(input_buf, sizeof(int16_t));
  struct sample_buffer out_buf =
      sample_buffer_weak_ref(output_buf, sizeof(int16_t));
  unsigned int num_queued = sample_buf_queued(&in_buf);
  unsigned int num_avail = sample_buf_available(&out_buf);
  int num_read_bytes = 0;
  while (num_queued && num_avail) {
    unsigned int num_read = sample_buf_readable(&in_buf);
    unsigned int num_written =
        MIN(num_read * sr->sample_rate_scale, sample_buf_writable(&out_buf));
    num_read = num_written / sr->sample_rate_scale;
    if (!num_read || !num_written) {
      break;
    }

    sample_buf_increment_read(&in_buf, num_read);
    num_read_bytes += num_read * sizeof(int16_t);
    num_queued -= num_read;

    sample_buf_increment_write(&out_buf, num_written);
    num_avail -= num_written;
  }
  return num_read_bytes;
}

double cras_sr_get_frames_ratio(struct cras_sr* sr) {
  return sr->sample_rate_scale;
}

void cras_sr_set_frames_ratio(struct cras_sr* sr, double frames_ratio) {
  sr->sample_rate_scale = frames_ratio;
}

size_t cras_sr_get_num_frames_per_run(struct cras_sr* sr) {
  return sr->num_frames_per_run;
}

void cras_sr_set_num_frames_per_run(struct cras_sr* sr,
                                    size_t num_frames_per_run) {
  sr->num_frames_per_run = num_frames_per_run;
}
}
