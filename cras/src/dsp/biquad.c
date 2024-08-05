/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Copyright 2010 Google LLC
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.WEBKIT file.
 */

#include "cras/src/dsp/biquad.h"

#include <errno.h>
#include <math.h>

#include "cras/src/dsp/dsp_helpers.h"
#include "cras/src/dsp/rust/dsp.h"
#include "user/eq.h"

int biquad_convert_blob(const struct biquad* bq,
                        int32_t* bq_cfg,
                        float* gain_accum,
                        bool dump_gain) {
  if (!bq_cfg || !bq) {
    return -EINVAL;
  }

  struct sof_eq_iir_biquad* bq_param = (struct sof_eq_iir_biquad*)bq_cfg;

  /* In SOF, the biquad filter is implemented similar to the form below:
   * https://en.wikipedia.org/wiki/Digital_biquad_filter#Transposed_direct_form_2
   * The coefficient a1 and a2 are applied in a negative way, which should be
   * handled on blob conversion.
   */
  bq_param->a2 = float_to_qint32(bq->a2 * (-1), 30); /* a2: Q2.30 */
  bq_param->a1 = float_to_qint32(bq->a1 * (-1), 30); /* a1: Q2.30 */

  /* Adjust the absolute values of b2,b1,b0 to not greater than 1.0 to prevent
   * the sample saturation, then accumulate the gain.
   */
  float accumulated_gain = *gain_accum;
  float gain = fmaxf(fmaxf(fabsf(bq->b2), fabsf(bq->b1)), fabsf(bq->b0));
  if (gain > 1.0) {
    bq_param->b2 = float_to_qint32(bq->b2 / gain, 30); /* b2: Q2.30 */
    bq_param->b1 = float_to_qint32(bq->b1 / gain, 30); /* b1: Q2.30 */
    bq_param->b0 = float_to_qint32(bq->b0 / gain, 30); /* b0: Q2.30 */
    accumulated_gain *= gain;
  } else {
    bq_param->b2 = float_to_qint32(bq->b2, 30); /* b2: Q2.30 */
    bq_param->b1 = float_to_qint32(bq->b1, 30); /* b1: Q2.30 */
    bq_param->b0 = float_to_qint32(bq->b0, 30); /* b0: Q2.30 */
  }

  /* The value of dump_gain determines the treatment for accumulated gain.
   * If is true, dump the gain accumulated so far.
   *   - Transform the gain value into format (output_gain * 2^output_shift),
   *     and put them to the config blob (i.e. sof_eq_iir_biquad).
   * If is false, hold the accumulated gain to keep it accumulated.
   */
  if (dump_gain) {
    int32_t final_shift = 0;
    while (fabs(accumulated_gain) >= 2.0) {
      accumulated_gain /= 2.0;
      final_shift--;
    }
    bq_param->output_shift = final_shift;
    /* output_gain: Q2.14 (the last 16 bits are redundant) */
    bq_param->output_gain = float_to_qint32(accumulated_gain, 14);
    *gain_accum = 1.0; /* reset to no gain=1.0 */
  } else {
    bq_param->output_shift = 0;
    bq_param->output_gain = 1 << 14; /* 1.0 in format Q2.14 */
    *gain_accum = accumulated_gain;  /* update the accumulated gain */
  }

  return 0;
}
