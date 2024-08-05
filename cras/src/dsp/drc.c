/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Copyright 2011 Google LLC
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.WEBKIT file.
 */

#include "cras/src/dsp/drc.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

#include "cras/common/check.h"
#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/crossover2.h"
#include "cras/src/dsp/dsp_helpers.h"
#include "cras/src/dsp/eq2.h"
#include "cras/src/dsp/rust/dsp.h"
#include "user/drc.h"
#include "user/multiband_drc.h"

static void convert_one_band(const struct drc_kernel* drc,
                             float* param,
                             struct sof_drc_params* cfg) {
  const struct drc_kernel_param dkp = dk_get_parameter(drc);
  cfg->enabled = dkp.enabled;
  cfg->db_threshold = float_to_qint32(dkp.db_threshold, 24);         /* Q8.24 */
  cfg->db_knee = float_to_qint32(dkp.db_knee, 24);                   /* Q8.24 */
  cfg->ratio = float_to_qint32(dkp.ratio, 24);                       /* Q8.24 */
  cfg->pre_delay_time = float_to_qint32(param[PARAM_PRE_DELAY], 30); /* Q2.30 */
  cfg->linear_threshold = float_to_qint32(dkp.linear_threshold, 30); /* Q2.30 */
  cfg->slope = float_to_qint32(dkp.slope, 30);                       /* Q2.30 */
  cfg->K = float_to_qint32(dkp.K, 20);                           /* Q12.20 */
  cfg->knee_alpha = float_to_qint32(dkp.knee_alpha, 24);         /* Q8.24 */
  cfg->knee_beta = float_to_qint32(dkp.knee_beta, 24);           /* Q8.24 */
  cfg->knee_threshold = float_to_qint32(dkp.knee_threshold, 24); /* Q8.24 */
  cfg->ratio_base = float_to_qint32(dkp.ratio_base, 30);         /* Q2.30 */
  cfg->master_linear_gain =
      float_to_qint32(dkp.main_linear_gain, 24); /* Q8.24 */

  float tmp = 1.0 / dkp.attack_frames;
  cfg->one_over_attack_frames = float_to_qint32(tmp, 30); /* Q2.30 */
  cfg->sat_release_frames_inv_neg =
      float_to_qint32(dkp.sat_release_frames_inv_neg, 30); /* Q2.30 */
  cfg->sat_release_rate_at_neg_two_db =
      float_to_qint32(dkp.sat_release_rate_at_neg_two_db, 30); /* Q2.30 */
  cfg->kSpacingDb = 5;                                         /* integer */
  cfg->kA = float_to_qint32(dkp.kA, 12);                       /* Q20.12 */
  cfg->kB = float_to_qint32(dkp.kB, 12);                       /* Q20.12 */
  cfg->kC = float_to_qint32(dkp.kC, 12);                       /* Q20.12 */
  cfg->kD = float_to_qint32(dkp.kD, 12);                       /* Q20.12 */
  cfg->kE = float_to_qint32(dkp.kE, 12);                       /* Q20.12 */
}

int drc_convert_params_to_blob(struct drc* drc,
                               uint32_t** config,
                               size_t* config_size) {
  if (!drc) {
    return -EINVAL;
  }

  const size_t drc_config_hdr_size = sizeof(struct sof_multiband_drc_config);
  const size_t drc_params_size = sizeof(struct sof_drc_params);
  const size_t drc_config_size =
      drc_config_hdr_size + DRC_NUM_KERNELS * drc_params_size;

  struct sof_multiband_drc_config* drc_config;
  drc_config = (struct sof_multiband_drc_config*)calloc(1, drc_config_size);
  if (!drc_config) {
    return -ENOMEM;
  }

  struct drc_component drcc = drc_get_components(drc);

  drc_config->size = drc_config_size;
  drc_config->num_bands = DRC_NUM_KERNELS;
  drc_config->enable_emp_deemp = drcc.emphasis_disabled ? 0 : 1;

  /* CRAS DRC design is based on L/R channel symmetry. That is, audio data on
   * both channels apply to the identical response, i.e. emphasis and
   * deemphasis filter, crossover, and per-band drc. On that account, the blob
   * will only convert the response on the first channel.
   */
  int ret;
  ret = eq2_convert_channel_response(drcc.emphasis_eq,
                                     (int32_t*)drc_config->emp_coef, 0);
  if (ret < 0) {
    free(drc_config);
    return ret;
  }
  ret = eq2_convert_channel_response(drcc.deemphasis_eq,
                                     (int32_t*)drc_config->deemp_coef, 0);
  if (ret < 0) {
    free(drc_config);
    return ret;
  }

  ret = crossover2_convert_params_to_blob(drcc.xo2,
                                          (int32_t*)drc_config->crossover_coef);
  if (ret < 0) {
    free(drc_config);
    return ret;
  }

  struct sof_drc_params* drc_coef = drc_config->drc_coef;
  for (int band = 0; band < DRC_NUM_KERNELS; band++) {
    convert_one_band(drcc.kernel[band], drcc.parameters[band], drc_coef);
    drc_coef++;
  }

  *config = (uint32_t*)drc_config;
  *config_size = drc_config_size;
  return 0;
}
