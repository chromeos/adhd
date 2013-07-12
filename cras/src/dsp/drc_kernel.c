/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Copyright (C) 2011 Google Inc. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.WEBKIT file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drc_math.h"
#include "drc_kernel.h"

#define MAX_PRE_DELAY_FRAMES 1024
#define MAX_PRE_DELAY_FRAMES_MASK (MAX_PRE_DELAY_FRAMES - 1)
#define DEFAULT_PRE_DELAY_FRAMES 256
#define DIVISION_FRAMES 32
#define DIVISION_FRAMES_MASK (DIVISION_FRAMES - 1)

#define assert_on_compile(e) ((void)sizeof(char[1 - 2 * !(e)]))
#define assert_on_compile_is_power_of_2(n) \
	assert_on_compile((n) != 0 && (((n) & ((n) - 1)) == 0))

const float uninitialized_value = -1;
static int drc_math_initialized;

void dk_init(struct drc_kernel *dk, float sample_rate)
{
	int i;

	if (!drc_math_initialized) {
		drc_math_initialized = 1;
		drc_math_init();
	}

	dk->sample_rate = sample_rate;
	dk->detector_average = 0;
	dk->compressor_gain = 1;
	dk->enabled = 0;
	dk->processed = 0;
	dk->last_pre_delay_frames = DEFAULT_PRE_DELAY_FRAMES;
	dk->pre_delay_read_index = 0;
	dk->pre_delay_write_index = DEFAULT_PRE_DELAY_FRAMES;
	dk->max_attack_compression_diff_db = -INFINITY;
	dk->ratio = uninitialized_value;
	dk->slope = uninitialized_value;
	dk->linear_threshold = uninitialized_value;
	dk->db_threshold = uninitialized_value;
	dk->db_knee = uninitialized_value;
	dk->knee_threshold = uninitialized_value;
	dk->ratio_base = uninitialized_value;
	dk->K = uninitialized_value;

	/* Allocate predelay buffers */
	assert_on_compile_is_power_of_2(MAX_PRE_DELAY_FRAMES);
	assert_on_compile_is_power_of_2(DIVISION_FRAMES);
	for (i = 0; i < DRC_NUM_CHANNELS; i++) {
		size_t size = sizeof(float) * MAX_PRE_DELAY_FRAMES;
		dk->pre_delay_buffers[i] = (float *)calloc(1, size);
	}
}

void dk_free(struct drc_kernel *dk)
{
	int i;
	for (i = 0; i < DRC_NUM_CHANNELS; ++i)
		free(dk->pre_delay_buffers[i]);
}

/* Sets the pre-delay (lookahead) buffer size */
static void set_pre_delay_time(struct drc_kernel *dk, float pre_delay_time)
{
	int i;
	/* Re-configure look-ahead section pre-delay if delay time has
	 * changed. */
	unsigned pre_delay_frames = pre_delay_time * dk->sample_rate;
	pre_delay_frames = min(pre_delay_frames, MAX_PRE_DELAY_FRAMES - 1);

	/* Make pre_delay_frames multiplies of DIVISION_FRAMES. This way we
	 * won't split a division of samples into two blocks of memory, so it is
	 * easier to process. This may make the actual delay time slightly less
	 * than the specified value, but the difference is less than 1ms. */
	pre_delay_frames &= ~DIVISION_FRAMES_MASK;

	/* We need at least one division buffer, so the incoming data won't
	 * overwrite the output data */
	pre_delay_frames = max(pre_delay_frames, DIVISION_FRAMES);

	if (dk->last_pre_delay_frames != pre_delay_frames) {
		dk->last_pre_delay_frames = pre_delay_frames;
		for (i = 0; i < DRC_NUM_CHANNELS; ++i) {
			size_t size = sizeof(float) * MAX_PRE_DELAY_FRAMES;
			memset(dk->pre_delay_buffers[i], 0, size);
		}

		dk->pre_delay_read_index = 0;
		dk->pre_delay_write_index = pre_delay_frames;
	}
}

/* Exponential curve for the knee.  It is 1st derivative matched at
 * dk->linear_threshold and asymptotically approaches the value
 * dk->linear_threshold + 1 / k.
 *
 * This is used only when calculating the static curve, not used when actually
 * compress the input data (knee_curveK below is used instead).
 */
static float knee_curve(struct drc_kernel *dk, float x, float k)
{
	/* Linear up to threshold. */
	if (x < dk->linear_threshold)
		return x;

	return dk->linear_threshold +
		(1 - knee_expf(-k * (x - dk->linear_threshold))) / k;
}

/* Approximate 1st derivative with input and output expressed in dB.  This slope
 * is equal to the inverse of the compression "ratio".  In other words, a
 * compression ratio of 20 would be a slope of 1/20.
 */
static float slope_at(struct drc_kernel *dk, float x, float k)
{
	if (x < dk->linear_threshold)
		return 1;

	float x2 = x * 1.001;

	float x_db = linear_to_decibels(x);
	float x2Db = linear_to_decibels(x2);

	float y_db = linear_to_decibels(knee_curve(dk, x, k));
	float y2Db = linear_to_decibels(knee_curve(dk, x2, k));

	float m = (y2Db - y_db) / (x2Db - x_db);

	return m;
}

static float k_at_slope(struct drc_kernel *dk, float desired_slope)
{
	float x_db = dk->db_threshold + dk->db_knee;
	float x = decibels_to_linear(x_db);

	/* Approximate k given initial values. */
	float minK = 0.1;
	float maxK = 10000;
	float k = 5;
	int i;

	for (i = 0; i < 15; ++i) {
		/* A high value for k will more quickly asymptotically approach
		 * a slope of 0. */
		float slope = slope_at(dk, x, k);

		if (slope < desired_slope) {
			/* k is too high. */
			maxK = k;
		} else {
			/* k is too low. */
			minK = k;
		}

		/* Re-calculate based on geometric mean. */
		k = sqrtf(minK * maxK);
	}

	return k;
}

static void update_static_curve_parameters(struct drc_kernel *dk,
					   float db_threshold,
					   float db_knee, float ratio)
{
	if (db_threshold != dk->db_threshold || db_knee != dk->db_knee ||
	    ratio != dk->ratio) {
		/* Threshold and knee. */
		dk->db_threshold = db_threshold;
		dk->linear_threshold = decibels_to_linear(db_threshold);
		dk->db_knee = db_knee;

		/* Compute knee parameters. */
		dk->ratio = ratio;
		dk->slope = 1 / dk->ratio;

		float k = k_at_slope(dk, 1 / dk->ratio);
		dk->K = k;
		/* See knee_curveK() for details */
		dk->knee_alpha = dk->linear_threshold + 1 / k;
		dk->knee_beta = -expf(k * dk->linear_threshold) / k;

		dk->knee_threshold = decibels_to_linear(db_threshold + db_knee);
		/* See volume_gain() for details */
		float y0 = knee_curve(dk, dk->knee_threshold, k);
		dk->ratio_base = y0 * powf(dk->knee_threshold, -dk->slope);
	}
}

/* This is the knee part of the compression curve. Returns the output level
 * given the input level x. */
static float knee_curveK(struct drc_kernel *dk, float x)
{
	/* The formula in knee_curveK is dk->linear_threshold +
	 * (1 - expf(-k * (x - dk->linear_threshold))) / k
	 * which simplifies to (alpha + beta * expf(gamma))
	 * where alpha = dk->linear_threshold + 1 / k
	 *	 beta = -expf(k * dk->linear_threshold) / k
	 *	 gamma = -k * x
	 */
	return dk->knee_alpha + dk->knee_beta * knee_expf(-dk->K * x);
}

/* Full compression curve with constant ratio after knee. Returns the ratio of
 * output and input signal. */
static float volume_gain(struct drc_kernel *dk, float x)
{
	float y;

	if (x < dk->knee_threshold) {
		if (x < dk->linear_threshold)
			return 1;
		y = knee_curveK(dk, x) / x;
	} else {
		/* Constant ratio after knee.
		 * log(y/y0) = s * log(x/x0)
		 * => y = y0 * (x/x0)^s
		 * => y = [y0 * (1/x0)^s] * x^s
		 * => y = dk->ratio_base * x^s
		 * => y/x = dk->ratio_base * x^(s - 1)
		 */
		y = dk->ratio_base * powf(x, dk->slope - 1);
	}

	return y;
}

void dk_set_parameters(struct drc_kernel *dk,
		       float db_threshold,
		       float db_knee,
		       float ratio,
		       float attack_time,
		       float release_time,
		       float pre_delay_time,
		       float db_post_gain,
		       float releaseZone1,
		       float releaseZone2,
		       float releaseZone3,
		       float releaseZone4)
{
	float sample_rate = dk->sample_rate;

	update_static_curve_parameters(dk, db_threshold, db_knee, ratio);

	/* Makeup gain. */
	float full_range_gain = volume_gain(dk, 1);
	float full_range_makeup_gain = 1 / full_range_gain;

	/* Empirical/perceptual tuning. */
	full_range_makeup_gain = powf(full_range_makeup_gain, 0.6f);

	dk->master_linear_gain = decibels_to_linear(db_post_gain) *
		full_range_makeup_gain;

	/* Attack parameters. */
	attack_time = max(0.001f, attack_time);
	dk->attack_frames = attack_time * sample_rate;

	/* Release parameters. */
	float release_frames = sample_rate * release_time;

	/* Detector release time. */
	float sat_release_time = 0.0025f;
	float sat_release_frames = sat_release_time * sample_rate;
	dk->sat_release_frames_inv_neg = -1 / sat_release_frames;

	/* Create a smooth function which passes through four points.
	 * Polynomial of the form y = a + b*x + c*x^2 + d*x^3 + e*x^4
	 */
	float y1 = release_frames * releaseZone1;
	float y2 = release_frames * releaseZone2;
	float y3 = release_frames * releaseZone3;
	float y4 = release_frames * releaseZone4;

	/* All of these coefficients were derived for 4th order polynomial curve
	 * fitting where the y values match the evenly spaced x values as
	 * follows: (y1 : x == 0, y2 : x == 1, y3 : x == 2, y4 : x == 3)
	 */
	dk->kA = 0.9999999999999998f*y1 + 1.8432219684323923e-16f*y2
		- 1.9373394351676423e-16f*y3 + 8.824516011816245e-18f*y4;
	dk->kB = -1.5788320352845888f*y1 + 2.3305837032074286f*y2
		- 0.9141194204840429f*y3 + 0.1623677525612032f*y4;
	dk->kC = 0.5334142869106424f*y1 - 1.272736789213631f*y2
		+ 0.9258856042207512f*y3 - 0.18656310191776226f*y4;
	dk->kD = 0.08783463138207234f*y1 - 0.1694162967925622f*y2
		+ 0.08588057951595272f*y3 - 0.00429891410546283f*y4;
	dk->kE = -0.042416883008123074f*y1 + 0.1115693827987602f*y2
		- 0.09764676325265872f*y3 + 0.028494263462021576f*y4;

	/* x ranges from 0 -> 3	      0	   1	2   3
	 *			     -15  -10  -5   0db
	 *
	 * y calculates adaptive release frames depending on the amount of
	 * compression.
	 */
	set_pre_delay_time(dk, pre_delay_time);
}

void dk_set_enabled(struct drc_kernel *dk, int enabled)
{
	dk->enabled = enabled;
}

/* Updates the envelope_rate used for the next division */
static void dk_update_envelope(struct drc_kernel *dk)
{
	const float kA = dk->kA;
	const float kB = dk->kB;
	const float kC = dk->kC;
	const float kD = dk->kD;
	const float kE = dk->kE;
	const float attack_frames = dk->attack_frames;

	/* Calculate desired gain */
	float desired_gain = dk->detector_average;

	/* Pre-warp so we get desired_gain after sin() warp below. */
	float scaled_desired_gain = warp_asinf(desired_gain);

	/* Deal with envelopes */

	/* envelope_rate is the rate we slew from current compressor level to
	 * the desired level.  The exact rate depends on if we're attacking or
	 * releasing and by how much.
	 */
	float envelope_rate;

	int is_releasing = scaled_desired_gain > dk->compressor_gain;

	/* compression_diff_db is the difference between current compression
	 * level and the desired level. */
	float compression_diff_db = linear_to_decibels(
		dk->compressor_gain / scaled_desired_gain);

	if (is_releasing) {
		/* Release mode - compression_diff_db should be negative dB */
		dk->max_attack_compression_diff_db = -INFINITY;

		/* Fix gremlins. */
		if (isbadf(compression_diff_db))
			compression_diff_db = -1;

		/* Adaptive release - higher compression (lower
		 * compression_diff_db) releases faster. Contain within range:
		 * -12 -> 0 then scale to go from 0 -> 3
		 */
		float x = compression_diff_db;
		x = max(-12.0f, x);
		x = min(0.0f, x);
		x = 0.25f * (x + 12);

		/* Compute adaptive release curve using 4th order polynomial.
		 * Normal values for the polynomial coefficients would create a
		 * monotonically increasing function.
		 */
		float x2 = x * x;
		float x3 = x2 * x;
		float x4 = x2 * x2;
		float release_frames = kA + kB * x + kC * x2 + kD * x3 +
			kE * x4;

#define kSpacingDb 5
		float db_per_frame = kSpacingDb / release_frames;
		envelope_rate = decibels_to_linear(db_per_frame);
	} else {
		/* Attack mode - compression_diff_db should be positive dB */

		/* Fix gremlins. */
		if (isbadf(compression_diff_db))
			compression_diff_db = 1;

		/* As long as we're still in attack mode, use a rate based off
		 * the largest compression_diff_db we've encountered so far.
		 */
		dk->max_attack_compression_diff_db = max(
			dk->max_attack_compression_diff_db,
			compression_diff_db);

		float eff_atten_diff_db =
			max(0.5f, dk->max_attack_compression_diff_db);

		float x = 0.25f / eff_atten_diff_db;
		envelope_rate = 1 - powf(x, 1 / attack_frames);
	}

	dk->envelope_rate = envelope_rate;
	dk->scaled_desired_gain = scaled_desired_gain;
}

/* Update detector_average from the last input division. */
static void dk_update_detector_average(struct drc_kernel *dk)
{
	const float sat_release_frames_inv_neg = dk->sat_release_frames_inv_neg;
	float detector_average = dk->detector_average;
	int div_start, i;

	/* Calculate the start index of the last input division */
	if (dk->pre_delay_write_index == 0) {
		div_start = MAX_PRE_DELAY_FRAMES - DIVISION_FRAMES;
	} else {
		div_start = dk->pre_delay_write_index - DIVISION_FRAMES;
	}

	for (i = 0; i < DIVISION_FRAMES; i++) {
		/* the max abs value across all channels for this frame */
		float abs_input = 0;
		int j;
		/* Predelay signal, computing compression amount from un-delayed
		 * version.
		 */
		for (j = 0; j < DRC_NUM_CHANNELS; ++j) {
			float undelayed_source =
				dk->pre_delay_buffers[j][div_start + i];
			float abs_undelayed_source = fabsf(undelayed_source);
			if (abs_input < abs_undelayed_source)
				abs_input = abs_undelayed_source;
		}

		/* Calculate shaped power on undelayed input.  Put through
		 * shaping curve. This is linear up to the threshold, then
		 * enters a "knee" portion followed by the "ratio" portion. The
		 * transition from the threshold to the knee is smooth (1st
		 * derivative matched). The transition from the knee to the
		 * ratio portion is smooth (1st derivative matched).
		 */
		float gain = volume_gain(dk, abs_input);
		int is_release = (gain > detector_average);
		if (is_release) {
			float gain_db, db_per_frame, sat_release_rate;
			gain_db = linear_to_decibels(min(gain, NEG_TWO_DB));
			db_per_frame = gain_db * sat_release_frames_inv_neg;
			sat_release_rate = decibels_to_linear(db_per_frame) - 1;
			detector_average += (gain - detector_average) *
				sat_release_rate;
		} else {
			detector_average = gain;
		}

		/* Fix gremlins. */
		if (isbadf(detector_average))
			detector_average = 1.0f;
		else
			detector_average = min(detector_average, 1.0f);
	}

	dk->detector_average = detector_average;
}

/* Calculate compress_gain from the envelope and apply total_gain to compress
 * the next output division. */
static void dk_compress_output(struct drc_kernel *dk)
{
	const float master_linear_gain = dk->master_linear_gain;
	const float envelope_rate = dk->envelope_rate;
	const float scaled_desired_gain = dk->scaled_desired_gain;
	float compressor_gain = dk->compressor_gain;
	int div_start = dk->pre_delay_read_index;
	int i, j;

	for (i = 0; i < DIVISION_FRAMES; i++) {
		/* Exponential approach to desired gain. */
		if (envelope_rate < 1) {
			/* Attack - reduce gain to desired. */
			compressor_gain += (scaled_desired_gain -
					    compressor_gain) * envelope_rate;
		} else {
			/* Release - exponentially increase gain to 1.0 */
			compressor_gain *= envelope_rate;
			compressor_gain = min(1.0f, compressor_gain);
		}

		/* Warp pre-compression gain to smooth out sharp exponential
		 * transition points.
		 */
		float post_warp_compressor_gain = warp_sinf(compressor_gain);

		/* Calculate total gain using master gain. */
		float total_gain = master_linear_gain *
			post_warp_compressor_gain;

		/* Apply final gain. */
		for (j = 0; j < DRC_NUM_CHANNELS; ++j)
			dk->pre_delay_buffers[j][div_start + i] *= total_gain;
	}

	dk->compressor_gain = compressor_gain;
}

/* After one complete divison of samples have been received (and one divison of
 * samples have been output), we calculate shaped power average
 * (detector_average) from the input division, update envelope parameters from
 * detector_average, then prepare the next output division by applying the
 * envelope to compress the samples.
 */
static void dk_process_one_division(struct drc_kernel *dk)
{
	dk_update_detector_average(dk);
	dk_update_envelope(dk);
	dk_compress_output(dk);
}

/* Copy the input data to the pre-delay buffer, and copy the output data back to
 * the input buffer */
static void dk_copy_fragment(struct drc_kernel *dk, float *data_channels[],
			     unsigned frame_index, int frames_to_process)
{
	int write_index = dk->pre_delay_write_index;
	int read_index = dk->pre_delay_read_index;
	int j;

	for (j = 0; j < DRC_NUM_CHANNELS; ++j) {
		memcpy(&dk->pre_delay_buffers[j][write_index],
		       &data_channels[j][frame_index],
		       frames_to_process * sizeof(float));
		memcpy(&data_channels[j][frame_index],
		       &dk->pre_delay_buffers[j][read_index],
		       frames_to_process * sizeof(float));
	}

	dk->pre_delay_write_index = (write_index + frames_to_process) &
		MAX_PRE_DELAY_FRAMES_MASK;
	dk->pre_delay_read_index = (read_index + frames_to_process) &
		MAX_PRE_DELAY_FRAMES_MASK;
}

/* Delay the input sample only and don't do other processing. This is used when
 * the kernel is disabled. We want to do this to match the processing delay in
 * kernels of other bands.
 */
static void dk_process_delay_only(struct drc_kernel *dk, float *data_channels[],
				  unsigned count)
{
	int read_index = dk->pre_delay_read_index;
	int write_index = dk->pre_delay_write_index;
	int i = 0;

	while (i < count) {
		int j;
		int small = min(read_index, write_index);
		int large = max(read_index, write_index);
		/* chunk is the minimum of readable samples in contiguous
		 * buffer, writable samples in contiguous buffer, and the
		 * available input samples. */
		int chunk = min(large - small, MAX_PRE_DELAY_FRAMES - large);
		chunk = min(chunk, count - i);
		for (j = 0; j < DRC_NUM_CHANNELS; ++j) {
			memcpy(&dk->pre_delay_buffers[j][write_index],
			       &data_channels[j][i],
			       chunk * sizeof(float));
			memcpy(&data_channels[j][i],
			       &dk->pre_delay_buffers[j][read_index],
			       chunk * sizeof(float));
		}
		read_index = (read_index + chunk) & MAX_PRE_DELAY_FRAMES_MASK;
		write_index = (write_index + chunk) & MAX_PRE_DELAY_FRAMES_MASK;
		i += chunk;
	}

	dk->pre_delay_read_index = read_index;
	dk->pre_delay_write_index = write_index;
}

void dk_process(struct drc_kernel *dk, float *data_channels[], unsigned count)
{
	int i = 0;
	int fragment;

	if (!dk->enabled) {
		dk_process_delay_only(dk, data_channels, count);
		return;
	}

	if (!dk->processed) {
		dk_update_envelope(dk);
		dk_compress_output(dk);
		dk->processed = 1;
	}

	int offset = dk->pre_delay_write_index & DIVISION_FRAMES_MASK;
	while (i < count) {
		fragment = min(DIVISION_FRAMES - offset, count - i);
		dk_copy_fragment(dk, data_channels, i, fragment);
		i += fragment;
		offset = (offset + fragment) & DIVISION_FRAMES_MASK;

		/* Process the input division (32 frames). */
		if (offset == 0)
			dk_process_one_division(dk);
	}
}
