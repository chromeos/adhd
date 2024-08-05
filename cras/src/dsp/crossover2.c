/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/dsp/crossover2.h"

#include <errno.h>
#include <string.h>

#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/dsp_helpers.h"
#include "cras/src/dsp/rust/dsp.h"
#include "user/eq.h"

static void crossover2_convert_lr42(const struct lr42* lr4,
                                    struct sof_eq_iir_biquad* cfg) {
  /* In SOF, the biquad filter is implemented similar to the form below:
   * https://en.wikipedia.org/wiki/Digital_biquad_filter#Transposed_direct_form_2
   * The coefficient a1 and a2 are applied in a negative way, which should be
   * handled on blob conversion.
   */
  cfg->a2 = float_to_qint32(lr4->a2 * (-1), 30); /* Q2.30 */
  cfg->a1 = float_to_qint32(lr4->a1 * (-1), 30); /* Q2.30 */

  cfg->b2 = float_to_qint32(lr4->b2, 30); /* Q2.30 */
  cfg->b1 = float_to_qint32(lr4->b1, 30); /* Q2.30 */
  cfg->b0 = float_to_qint32(lr4->b0, 30); /* Q2.30 */
  cfg->output_shift = 0;
  cfg->output_gain = 1 << 14; /* Q2.14 (last 16 bits are redundant) */
}

int crossover2_convert_params_to_blob(const struct crossover2* xo2,
                                      int32_t* xo2_cfg) {
  if (!xo2_cfg || !xo2) {
    return -EINVAL;
  }

  struct sof_eq_iir_biquad* xo2_biquad = (struct sof_eq_iir_biquad*)xo2_cfg;

  /* crossover2 is designed for 3-band splitter, which contains 3 pairs of
   * lowpass and highpass filters. The blob needs to be converted in the order
   * that LP0, HP0, LP1, HP1, LP2, HP2.
   */
  for (int comp = 0; comp < CROSSOVER2_NUM_LR4_PAIRS; comp++) {
    crossover2_convert_lr42(&xo2->lp[comp], xo2_biquad++); /* LP[comp] */
    crossover2_convert_lr42(&xo2->hp[comp], xo2_biquad++); /* HP[comp] */
  }

  return 0;
}
