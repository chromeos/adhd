/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdio.h>

#include "cras_sr_bt_util.h"

int cras_sr_bt_can_be_enabled()
{
	/*
	 * TODO(b/216075565): Adds checking for featured and dlc states.
	 */
	return 0;
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(enum cras_sr_bt_model model)
{
	/*
	 * TODO(b/216075565): Gets the dlc_root by calling the dlc function.
	 */
	const char *dlc_root = "";
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
