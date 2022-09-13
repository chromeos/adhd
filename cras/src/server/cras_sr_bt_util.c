/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "cras_dlc.h"
#include "cras_featured.h"
#include "cras_sr_bt_util.h"

int cras_sr_bt_can_be_enabled()
{
	return get_hfp_mic_sr_feature_enabled() &&
	       cras_dlc_sr_bt_is_available();
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(enum cras_sr_bt_model model)
{
	const char *dlc_root = cras_dlc_sr_bt_get_root();
	struct cras_sr_model_spec spec = {};
	switch (model) {
	case SR_BT_NBS: {
		snprintf(spec.model_path, CRAS_SR_MODEL_PATH_CAPACITY, "%s/%s",
			 dlc_root, "btnb.tflite");
		spec.num_frames_per_run = 480;
		spec.num_channels = 1;
		spec.input_sample_rate = 8000;
		spec.output_sample_rate = 24000;
		break;
	};
	case SR_BT_WBS: {
		snprintf(spec.model_path, CRAS_SR_MODEL_PATH_CAPACITY, "%s/%s",
			 dlc_root, "btwb.tflite");
		spec.num_frames_per_run = 480;
		spec.num_channels = 1;
		spec.input_sample_rate = 16000;
		spec.output_sample_rate = 24000;
		break;
	}
	default:
		assert(0 && "unknown model type.");
	}
	return spec;
}