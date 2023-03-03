/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_sr.h"

#include <speex/speex_resampler.h>
#include <stdlib.h>
#include <syslog.h>

#include "cras/src/common/sample_buffer.h"
#include "cras/src/dsp/am.h"
#include "cras/src/server/cras_fmt_conv_ops.h"
#include "cras_util.h"

/* The context for running the SR.
 *
 * Example workflow:
 *   The `internal` buffer is always full of
 *     1. unprocessed samples from resampled samples, and
 *     2. processed samples from unprocessed samples.
 *
 *   In the beginning, we fill in zeros as processed samples:
 *   |rw|        processed          |
 *
 *   When moving some samples to output buf
 *   (cras_sr_processed_to_output):
 *   |w|  empty     |r|  processed  | -> | output |
 *
 *   The same number of samples will be propagated from `resampled` to
 * `internal`. (cras_sr_resampled_to_unprocessed) | unprocessed  |rw| processed
 * | <- | resampled |
 *
 *   When moving some samples to output buf again
 *   (cras_sr_processed_to_output):
 *   |r| unprocessed  |w|  empty    | -> | output |
 *
 *   Again, the samples in `resampled` are propagated to `internal`.
 *   (cras_sr_resampled_to_unprocessed)
 *   |rw|       unprocessed         | <- | resampled |
 *
 *   Once the `internal` is full of unprocessed samples, the model will be
 *   invoked to process the samples:
 *   (cras_sr_unprocessed_to_processed)
 *   |rw|       unprocessed         |
 *   |rw|        processed          |
 */
struct cras_sr {
  // the state of the speex resampler.
  SpeexResamplerState* speex_state;
  // the audio model context.
  struct am_context* am;
  // the buffer that stores the resampled samples.
  struct sample_buffer resampled;
  // the buffer that stores the unprocessed and processed samples.
  struct sample_buffer internal;
  // The ratio of output sample rate to input sample rate.
  double frames_ratio;
  // The number of frames needed to invoke the tflite model.
  size_t num_frames_per_run;
};

struct cras_sr* cras_sr_create(const struct cras_sr_model_spec spec,
                               const size_t input_nbytes) {
  assert(input_nbytes % sizeof(int16_t) == 0 &&
         "input buffer size must be multiple of sizeof(int16_t).");
  struct cras_sr* sr = NULL;

  sr = calloc(1, sizeof(struct cras_sr));
  if (!sr) {
    goto sr_create_fail;
  }

  int rc = 0;
  sr->speex_state =
      speex_resampler_init(1, spec.input_sample_rate, spec.output_sample_rate,
                           SPEEX_RESAMPLER_QUALITY_DEFAULT, &rc);
  if (!sr->speex_state) {
    syslog(LOG_ERR, "init speex resampler failed.");
    goto sr_create_fail;
  }

  sr->am = am_new(spec.model_path);
  if (!sr->am) {
    syslog(LOG_ERR, "am_new failed.");
    goto sr_create_fail;
  }

  const unsigned int resampled_buf_size = cras_frames_at_rate(
      spec.input_sample_rate, input_nbytes / sizeof(int16_t),
      spec.output_sample_rate);
  if (sample_buffer_init(resampled_buf_size, sizeof(int16_t), &sr->resampled)) {
    syslog(LOG_ERR, "sample_buffer_init failed.");
    goto sr_create_fail;
  }

  if (sample_buffer_init(spec.num_frames_per_run, sizeof(float),
                         &sr->internal)) {
    syslog(LOG_ERR, "sample_buffer_init failed.");
    goto sr_create_fail;
  }
  // Fills in the padded zeros.
  sample_buf_increment_write(&sr->internal, spec.num_frames_per_run);

  sr->frames_ratio = (double)spec.output_sample_rate / spec.input_sample_rate;
  sr->num_frames_per_run = spec.num_frames_per_run;

  return sr;

sr_create_fail:
  cras_sr_destroy(sr);
  return NULL;
}

void cras_sr_destroy(struct cras_sr* sr) {
  if (!sr) {
    return;
  }

  if (sr->speex_state) {
    speex_resampler_destroy(sr->speex_state);
  }

  if (sr->am) {
    am_free(sr->am);
  }

  sample_buffer_cleanup(&sr->resampled);

  sample_buffer_cleanup(&sr->internal);

  free(sr);
}

// Consumes input_buf and writes samples to resampled buf
static unsigned int cras_sr_speex_process(struct cras_sr* sr,
                                          struct sample_buffer* input_buf) {
  // If resampled is still not empty, do nothing.
  if (sample_buf_queued(&sr->resampled)) {
    return 0;
  }

  // Uses the whole buf space from start.
  // Because we allocated the resampled buf with size derived from
  // input_buf size and resampling ratio, the output space is always
  // sufficient.
  sample_buf_reset(&sr->resampled);

  const unsigned int num_consumed_samples = sample_buf_queued(input_buf);
  unsigned int num_queued_inputs = num_consumed_samples;
  while (num_queued_inputs > 0) {
    const unsigned int num_inputs = sample_buf_readable(input_buf);

    unsigned int num_outputs = sample_buf_writable(&sr->resampled);

    unsigned int num_inputs_used = num_inputs;
    speex_resampler_process_int(
        sr->speex_state, 0, (int16_t*)sample_buf_read_pointer(input_buf),
        &num_inputs_used, (int16_t*)sample_buf_write_pointer(&sr->resampled),
        &num_outputs);

    // All inputs should be consumed because of the large enough
    // output space.
    if (num_inputs != num_inputs_used) {
      syslog(LOG_ERR,
             "All inputs should be consumed, "
             "got consumed (%d) < all (%d).",
             num_inputs_used, num_inputs);
    }

    sample_buf_increment_read(input_buf, num_inputs);
    sample_buf_increment_write(&sr->resampled, num_outputs);

    num_queued_inputs -= num_inputs;
  }
  return num_consumed_samples;
}

static unsigned int cras_sr_get_num_propagated(
    struct cras_sr* sr,
    const struct sample_buffer* output_buf,
    unsigned int num_need_propagated) {
  // bounded by output_buf writable and model_buf readable
  // num_propagated will be always > 0, because
  // 1. output_buf available > 0 (checked by the caller.)
  // 2. (internal buf is always full).
  return MIN(MIN(num_need_propagated, sample_buf_writable(output_buf)),
             sample_buf_readable(&sr->internal));
}

static void cras_sr_processed_to_output(struct cras_sr* sr,
                                        struct sample_buffer* output_buf,
                                        const unsigned int num_propagated) {
  convert_f32le_to_s16le((float*)sample_buf_read_pointer(&sr->internal),
                         num_propagated,
                         (int16_t*)sample_buf_write_pointer(output_buf));
  sample_buf_increment_write(output_buf, num_propagated);
  sample_buf_increment_read(&sr->internal, num_propagated);
}

static void cras_sr_resampled_to_unprocessed(
    struct cras_sr* sr,
    const unsigned int num_propagated) {
  convert_s16le_to_f32le((int16_t*)sample_buf_read_pointer(&sr->resampled),
                         num_propagated,
                         (float*)sample_buf_write_pointer(&sr->internal));
  sample_buf_increment_read(&sr->resampled, num_propagated);
  sample_buf_increment_write(&sr->internal, num_propagated);
}

static void cras_sr_unprocessed_to_processed(struct cras_sr* sr) {
  unsigned int num_readable = 0;
  float* buf =
      (float*)sample_buf_read_pointer_size(&sr->internal, &num_readable);
  // am_process reads and writes to the buf.
  // If some error occurs, the original data in the buf
  // should be still usable.
  if (am_process(sr->am, (const float*)buf, num_readable, buf, num_readable)) {
    syslog(LOG_WARNING, "am_process failed.");
  }

  sample_buf_increment_read(&sr->internal, num_readable);
  sample_buf_increment_write(&sr->internal, num_readable);
}

// Propagates the samples to output_buf
static void cras_sr_propagate(struct cras_sr* sr,
                              struct sample_buffer* output_buf) {
  // bounded by output buf available size
  unsigned int num_need_propagated =
      MIN(sample_buf_queued(&sr->resampled), sample_buf_available(output_buf));

  while (num_need_propagated > 0) {
    const unsigned int num_propagated =
        cras_sr_get_num_propagated(sr, output_buf, num_need_propagated);

    cras_sr_processed_to_output(sr, output_buf, num_propagated);

    cras_sr_resampled_to_unprocessed(sr, num_propagated);

    if (sample_buf_full_with_zero_read_index(&sr->internal)) {
      cras_sr_unprocessed_to_processed(sr);
    }

    num_need_propagated -= num_propagated;
  }
}

int cras_sr_process(struct cras_sr* sr,
                    struct byte_buffer* input_buf,
                    struct byte_buffer* output_buf) {
  struct sample_buffer input_sample_buf =
      sample_buffer_weak_ref(input_buf, sizeof(int16_t));
  struct sample_buffer output_sample_buf =
      sample_buffer_weak_ref(output_buf, sizeof(int16_t));

  // propagates previous results: sr->resampled -> .. -> output_sample_buf
  cras_sr_propagate(sr, &output_sample_buf);

  // input_sample_buf -> sr->resampled
  unsigned int num_read_inputs = cras_sr_speex_process(sr, &input_sample_buf);

  // propagates sr->resampled -> .. -> output_sample_buf
  cras_sr_propagate(sr, &output_sample_buf);

  return (int)(num_read_inputs * sizeof(int16_t));
}

double cras_sr_get_frames_ratio(struct cras_sr* sr) {
  return sr->frames_ratio;
}

size_t cras_sr_get_num_frames_per_run(struct cras_sr* sr) {
  return sr->num_frames_per_run;
}
