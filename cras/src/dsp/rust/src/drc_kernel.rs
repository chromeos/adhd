// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use itertools::multizip;
use itertools::Itertools;

use crate::drc_math::{self};

pub const DRC_NUM_CHANNELS: usize = 2;
const MAX_PRE_DELAY_FRAMES: usize = 1024;
const MAX_PRE_DELAY_FRAMES_MASK: usize = MAX_PRE_DELAY_FRAMES - 1;
const DEFAULT_PRE_DELAY_FRAMES: usize = 256;
const DIVISION_FRAMES: usize = 32;
const DIVISION_FRAMES_MASK: usize = DIVISION_FRAMES - 1;

const UNINITIALIZED_VALUE: f32 = -1.;

#[derive(Default, Clone, Copy)]
#[allow(non_snake_case)]
#[repr(C)]
pub struct DrcKernelParam {
    pub enabled: bool,

    /// Amount of input change in dB required for 1 dB of output change.
    /// This applies to the portion of the curve above knee_threshold
    /// (see below).
    ///
    pub ratio: f32,
    pub slope: f32, // Inverse ratio

    // The input to output change below the threshold is 1:1.
    pub linear_threshold: f32,
    pub db_threshold: f32,

    /// db_knee is the number of dB above the threshold before we enter the
    /// "ratio" portion of the curve.  The portion between db_threshold and
    /// (db_threshold + db_knee) is the "soft knee" portion of the curve
    /// which transitions smoothly from the linear portion to the ratio
    /// portion. knee_threshold is db_to_linear(db_threshold + db_knee).
    ///
    pub db_knee: f32,
    pub knee_threshold: f32,
    pub ratio_base: f32,

    /// Internal parameter for the knee portion of the curve.
    pub K: f32,

    /// The release frames coefficients
    pub kA: f32,
    pub kB: f32,
    pub kC: f32,
    pub kD: f32,
    pub kE: f32,

    /// Calculated parameters
    pub main_linear_gain: f32,
    pub attack_frames: f32,
    pub sat_release_frames_inv_neg: f32,
    pub sat_release_rate_at_neg_two_db: f32,
    pub knee_alpha: f32,
    pub knee_beta: f32,
}

#[derive(Default)]
pub struct DrcKernel {
    sample_rate: f32,

    /// The detector_average is the target gain obtained by looking at the
    /// future samples in the lookahead buffer and applying the compression
    /// curve on them. compressor_gain is the gain applied to the current
    /// samples. compressor_gain moves towards detector_average with the
    /// speed envelope_rate which is calculated once for each division (32
    /// frames).
    detector_average: f32,
    compressor_gain: f32,
    processed: i32,

    /// Lookahead section.
    last_pre_delay_frames: usize,
    pre_delay_buffers: [Vec<f32>; DRC_NUM_CHANNELS],
    pre_delay_read_index: usize,
    pre_delay_write_index: usize,

    max_attack_compression_diff_db: f32,

    /// The public parameters for DrcKernel
    pub param: DrcKernelParam,

    /// envelope for the current division
    envelope_rate: f32,
    scaled_desired_gain: f32,
}

impl DrcKernel {
    pub fn new(sample_rate: f32) -> Self {
        Self {
            sample_rate: sample_rate,
            detector_average: 0.,
            compressor_gain: 1.,
            processed: 0,
            last_pre_delay_frames: DEFAULT_PRE_DELAY_FRAMES,
            pre_delay_buffers: std::array::from_fn(|_i| vec![0.; MAX_PRE_DELAY_FRAMES]),
            pre_delay_read_index: 0,
            pre_delay_write_index: DEFAULT_PRE_DELAY_FRAMES,
            max_attack_compression_diff_db: -f32::INFINITY,
            param: DrcKernelParam {
                enabled: false,
                ratio: UNINITIALIZED_VALUE,
                slope: UNINITIALIZED_VALUE,
                linear_threshold: UNINITIALIZED_VALUE,
                db_threshold: UNINITIALIZED_VALUE,
                db_knee: UNINITIALIZED_VALUE,
                knee_threshold: UNINITIALIZED_VALUE,
                ratio_base: UNINITIALIZED_VALUE,
                K: UNINITIALIZED_VALUE,
                kA: 0.,
                kB: 0.,
                kC: 0.,
                kD: 0.,
                kE: 0.,
                main_linear_gain: 0.,
                attack_frames: 0.,
                sat_release_frames_inv_neg: 0.,
                sat_release_rate_at_neg_two_db: 0.,
                knee_alpha: 0.,
                knee_beta: 0.,
            },
            envelope_rate: 0.,
            scaled_desired_gain: 0.,
        }
    }

    /// Sets the pre-delay (lookahead) buffer size
    pub fn set_pre_delay_time(&mut self, pre_delay_time: f32) {
        /* Re-configure look-ahead section pre-delay if delay time has
         * changed. */
        let mut pre_delay_frames: usize = (pre_delay_time * self.sample_rate) as usize;
        pre_delay_frames = pre_delay_frames.min(MAX_PRE_DELAY_FRAMES - 1);

        /* Make pre_delay_frames multiplies of DIVISION_FRAMES. This way we
         * won't split a division of samples into two blocks of memory, so it is
         * easier to process. This may make the actual delay time slightly less
         * than the specified value, but the difference is less than 1ms. */
        pre_delay_frames &= !DIVISION_FRAMES_MASK;

        /* We need at least one division buffer, so the incoming data won't
         * overwrite the output data */
        pre_delay_frames = pre_delay_frames.max(DIVISION_FRAMES);

        if self.last_pre_delay_frames != pre_delay_frames {
            self.last_pre_delay_frames = pre_delay_frames;
            for buffer in self.pre_delay_buffers.iter_mut() {
                buffer.fill(0.);
            }

            self.pre_delay_read_index = 0;
            self.pre_delay_write_index = pre_delay_frames as usize;
        }
    }

    /// Exponential curve for the knee.  It is 1st derivative matched at
    /// self.linear_threshold and asymptotically approaches the value
    /// self.linear_threshold + 1 / k.
    ///
    /// This is used only when calculating the static curve, not used when actually
    /// compress the input data (knee_curveK below is used instead).
    ///
    fn knee_curve(&self, x: f32, k: f32) -> f32 {
        // Linear up to threshold.
        if x < self.param.linear_threshold {
            return x;
        }
        self.param.linear_threshold
            + (1. - drc_math::knee_expf(-k * (x - self.param.linear_threshold))) / k
    }

    /// Approximate 1st derivative with input and output expressed in dB.  This slope
    /// is equal to the inverse of the compression "ratio".  In other words, a
    /// compression ratio of 20 would be a slope of 1/20.
    ///
    fn slope_at(&self, x: f32, k: f32) -> f32 {
        if x < self.param.linear_threshold {
            return 1.;
        }

        let x2: f32 = x * 1.001;

        let x_db: f32 = drc_math::linear_to_decibels(x);
        let x2_db: f32 = drc_math::linear_to_decibels(x2);

        let y_db: f32 = drc_math::linear_to_decibels(self.knee_curve(x, k));
        let y2_db: f32 = drc_math::linear_to_decibels(self.knee_curve(x2, k));

        let m = (y2_db - y_db) / (x2_db - x_db);

        return m;
    }

    fn k_at_slope(&self, desired_slope: f32) -> f32 {
        let x_db: f32 = self.param.db_threshold + self.param.db_knee;
        let x: f32 = drc_math::decibels_to_linear(x_db);

        // Approximate k given initial values.
        let mut min_k: f32 = 0.1;
        let mut max_k: f32 = 10000.;
        let mut k: f32 = 5.;

        for _i in 0..15 {
            /* A high value for k will more quickly asymptotically approach
             * a slope of 0. */
            let slope: f32 = self.slope_at(x, k);

            if slope < desired_slope {
                // k is too high.
                max_k = k;
            } else {
                // k is too low.
                min_k = k;
            }

            // Re-calculate based on geometric mean.
            k = (min_k * max_k).sqrt();
        }

        k
    }

    fn update_static_curve_parameters(&mut self, db_threshold: f32, db_knee: f32, ratio: f32) {
        if db_threshold != self.param.db_threshold
            || db_knee != self.param.db_knee
            || ratio != self.param.ratio
        {
            // Threshold and knee.
            self.param.db_threshold = db_threshold;
            self.param.linear_threshold = drc_math::decibels_to_linear(db_threshold);
            self.param.db_knee = db_knee;

            // Compute knee parameters.
            self.param.ratio = ratio;
            self.param.slope = 1. / self.param.ratio;

            let k: f32 = self.k_at_slope(1. / self.param.ratio);
            self.param.K = k;
            // See knee_curve_k() for details
            self.param.knee_alpha = self.param.linear_threshold + 1. / k;
            self.param.knee_beta = -(k * self.param.linear_threshold).exp() / k;

            self.param.knee_threshold = drc_math::decibels_to_linear(db_threshold + db_knee);
            // See volume_gain() for details
            let y0: f32 = self.knee_curve(self.param.knee_threshold, k);
            self.param.ratio_base = y0 * self.param.knee_threshold.powf(-self.param.slope);
        }
    }

    /// This is the knee part of the compression curve. Returns the output level
    /// given the input level x.
    fn knee_curve_k(&self, x: f32) -> f32 {
        /* The formula in knee_curve_k is self.linear_threshold +
         * (1 - expf(-k * (x - self.linear_threshold))) / k
         * which simplifies to (alpha + beta * expf(gamma))
         * where alpha = self.linear_threshold + 1 / k
         *	 beta = -expf(k * self.linear_threshold) / k
         *	 gamma = -k * x
         */
        self.param.knee_alpha + self.param.knee_beta * drc_math::knee_expf(-self.param.K * x)
    }

    /// Full compression curve with constant ratio after knee. Returns the ratio of
    /// output and input signal.
    fn volume_gain(&self, x: f32) -> f32 {
        if x < self.param.knee_threshold {
            if x < self.param.linear_threshold {
                return 1.;
            }
            self.knee_curve_k(x) / x
        } else {
            /* Constant ratio after knee.
             * log(y/y0) = s * log(x/x0)
             * => y = y0 * (x/x0)^s
             * => y = [y0 * (1/x0)^s] * x^s
             * => y = self.ratio_base * x^s
             * => y/x = self.ratio_base * x^(s - 1)
             * => y/x = self.ratio_base * e^(log(x) * (s - 1))
             */
            self.param.ratio_base * drc_math::knee_expf(x.ln() * (self.param.slope - 1.))
        }
    }

    pub fn set_parameters(
        &mut self,
        db_threshold: f32,
        db_knee: f32,
        ratio: f32,
        mut attack_time: f32,
        release_time: f32,
        pre_delay_time: f32,
        db_post_gain: f32,
        release_zone1: f32,
        release_zone2: f32,
        release_zone3: f32,
        release_zone4: f32,
    ) {
        let sample_rate = self.sample_rate;

        self.update_static_curve_parameters(db_threshold, db_knee, ratio);

        // Makeup gain.
        let full_range_gain: f32 = self.volume_gain(1.);
        let mut full_range_makeup_gain: f32 = 1. / full_range_gain;

        // Empirical/perceptual tuning.
        full_range_makeup_gain = full_range_makeup_gain.powf(0.6);

        self.param.main_linear_gain =
            drc_math::decibels_to_linear(db_post_gain) * full_range_makeup_gain;

        // Attack parameters.
        attack_time = attack_time.max(0.001);
        self.param.attack_frames = attack_time * sample_rate;

        // Release parameters.
        let release_frames: f32 = sample_rate * release_time;

        // Detector release time.
        let sat_release_time: f32 = 0.0025;
        let sat_release_frames: f32 = sat_release_time * sample_rate;
        self.param.sat_release_frames_inv_neg = -1. / sat_release_frames;
        self.param.sat_release_rate_at_neg_two_db =
            drc_math::decibels_to_linear(-2. * self.param.sat_release_frames_inv_neg) - 1.;

        /* Create a smooth function which passes through four points.
         * Polynomial of the form y = a + b*x + c*x^2 + d*x^3 + e*x^4
         */
        let y1: f32 = release_frames * release_zone1;
        let y2: f32 = release_frames * release_zone2;
        let y3: f32 = release_frames * release_zone3;
        let y4: f32 = release_frames * release_zone4;

        /* All of these coefficients were derived for 4th order polynomial curve
         * fitting where the y values match the evenly spaced x values as
         * follows: (y1 : x == 0, y2 : x == 1, y3 : x == 2, y4 : x == 3)
         */
        self.param.kA = 0.9999999999999998 * y1 + 1.8432219684323923e-16 * y2
            - 1.9373394351676423e-16 * y3
            + 8.824516011816245e-18 * y4;
        self.param.kB = -1.5788320352845888 * y1 + 2.3305837032074286 * y2
            - 0.9141194204840429 * y3
            + 0.1623677525612032 * y4;
        self.param.kC = 0.5334142869106424 * y1 - 1.272736789213631 * y2 + 0.9258856042207512 * y3
            - 0.18656310191776226 * y4;
        self.param.kD = 0.08783463138207234 * y1 - 0.1694162967925622 * y2
            + 0.08588057951595272 * y3
            - 0.00429891410546283 * y4;
        self.param.kE = -0.042416883008123074 * y1 + 0.1115693827987602 * y2
            - 0.09764676325265872 * y3
            + 0.028494263462021576 * y4;

        /* x ranges from 0 -> 3     0   1   2   3
         *                        -15 -10  -5   0db
         *
         * y calculates adaptive release frames depending on the amount of
         * compression.
         */
        self.set_pre_delay_time(pre_delay_time);
    }

    pub fn set_enabled(&mut self, enabled: bool) {
        self.param.enabled = enabled;
    }

    /// Updates the envelope_rate used for the next division
    fn update_envelope(&mut self) {
        let k_a: f32 = self.param.kA;
        let k_b: f32 = self.param.kB;
        let k_c: f32 = self.param.kC;
        let k_d: f32 = self.param.kD;
        let k_e: f32 = self.param.kE;
        let attack_frames: f32 = self.param.attack_frames;

        // Calculate desired gain
        let desired_gain: f32 = self.detector_average;

        // Pre-warp so we get desired_gain after sin() warp below.
        let scaled_desired_gain: f32 = drc_math::warp_asinf(desired_gain);

        // Deal with envelopes

        /* envelope_rate is the rate we slew from current compressor level to
         * the desired level.  The exact rate depends on if we're attacking or
         * releasing and by how much.
         */
        let envelope_rate: f32;

        let is_releasing = scaled_desired_gain > self.compressor_gain;

        /* compression_diff_db is the difference between current compression
         * level and the desired level. */
        let mut compression_diff_db: f32 = match is_releasing {
            true => -1.,
            false => 1.,
        };
        if scaled_desired_gain != 0. {
            compression_diff_db =
                drc_math::linear_to_decibels(self.compressor_gain / scaled_desired_gain);
        }

        if is_releasing {
            // Release mode - compression_diff_db should be negative dB
            self.max_attack_compression_diff_db = -f32::INFINITY;

            // Fix gremlins.
            if drc_math::isbadf(compression_diff_db) {
                compression_diff_db = -1.;
            }

            /* Adaptive release - higher compression (lower
             * compression_diff_db) releases faster. Contain within range:
             * -12 -> 0 then scale to go from 0 -> 3
             */
            let mut x: f32 = compression_diff_db;
            x = x.max(-12.);
            x = x.min(0.);
            x = 0.25 * (x + 12.);

            /* Compute adaptive release curve using 4th order polynomial.
             * Normal values for the polynomial coefficients would create a
             * monotonically increasing function.
             */
            let x2: f32 = x * x;
            let x3: f32 = x2 * x;
            let x4: f32 = x2 * x2;
            let release_frames: f32 = k_a + k_b * x + k_c * x2 + k_d * x3 + k_e * x4;
            const K_SPACING_DB: f32 = 5.;
            let db_per_frame: f32 = K_SPACING_DB / release_frames;
            envelope_rate = drc_math::decibels_to_linear(db_per_frame);
        } else {
            // Attack mode - compression_diff_db should be positive dB

            // Fix gremlins.
            if drc_math::isbadf(compression_diff_db) {
                compression_diff_db = 1.;
            }

            /* As long as we're still in attack mode, use a rate based off
             * the largest compression_diff_db we've encountered so far.
             */
            self.max_attack_compression_diff_db =
                self.max_attack_compression_diff_db.max(compression_diff_db);

            let eff_atten_diff_db: f32 = self.max_attack_compression_diff_db.max(0.5);

            let x: f32 = 0.25 / eff_atten_diff_db;
            envelope_rate = 1. - x.powf(1. / attack_frames);
        }

        self.envelope_rate = envelope_rate;
        self.scaled_desired_gain = scaled_desired_gain;
    }

    /// For a division of frames, take the absolute values of left channel and right
    /// channel, store the maximum of them in output.
    fn max_abs_division(output: &mut [f32], data0: &[f32], data1: &[f32]) {
        for (output_i, data0_i, data1_i) in multizip((output, data0, data1)) {
            *output_i = (*data0_i).abs().max((*data1_i).abs());
        }
    }

    fn update_detector_average(&mut self) {
        let mut abs_input_array = [0.; DIVISION_FRAMES];
        let sat_release_frames_inv_neg: f32 = self.param.sat_release_frames_inv_neg;
        let sat_release_rate_at_neg_two_db: f32 = self.param.sat_release_rate_at_neg_two_db;
        let mut detector_average: f32 = self.detector_average;

        // Calculate the start index of the last input division
        let div_start: usize = match self.pre_delay_write_index {
            0 => MAX_PRE_DELAY_FRAMES - DIVISION_FRAMES,
            _ => self.pre_delay_write_index - DIVISION_FRAMES,
        };

        // The max abs value across all channels for this frame
        Self::max_abs_division(
            &mut abs_input_array,
            &self.pre_delay_buffers[0][div_start..(div_start + DIVISION_FRAMES)],
            &self.pre_delay_buffers[1][div_start..(div_start + DIVISION_FRAMES)],
        );

        for abs_input in abs_input_array.iter_mut() {
            // Compute compression amount from un-delayed signal
            /* Calculate shaped power on undelayed input.  Put through
             * shaping curve. This is linear up to the threshold, then
             * enters a "knee" portion followed by the "ratio" portion. The
             * transition from the threshold to the knee is smooth (1st
             * derivative matched). The transition from the knee to the
             * ratio portion is smooth (1st derivative matched).
             */
            let gain: f32 = self.volume_gain(*abs_input);
            let is_release: bool = gain > detector_average;
            if is_release {
                if gain > drc_math::NEG_TWO_DB as f32 {
                    detector_average += (gain - detector_average) * sat_release_rate_at_neg_two_db;
                } else {
                    let gain_db: f32 = drc_math::linear_to_decibels(gain);
                    let db_per_frame: f32 = gain_db * sat_release_frames_inv_neg;
                    let sat_release_rate: f32 = drc_math::decibels_to_linear(db_per_frame) - 1.;
                    detector_average += (gain - detector_average) * sat_release_rate;
                }
            } else {
                detector_average = gain;
            }

            // Fix gremlins.
            if drc_math::isbadf(detector_average) {
                detector_average = 1.;
            } else {
                detector_average = detector_average.min(1.);
            }
        }

        self.detector_average = detector_average;
    }

    /// Calculate compress_gain from the envelope and apply total_gain to compress
    /// the next output division.
    fn compress_output(&mut self) {
        let main_linear_gain: f32 = self.param.main_linear_gain;
        let envelope_rate: f32 = self.envelope_rate;
        let scaled_desired_gain: f32 = self.scaled_desired_gain;
        let compressor_gain: f32 = self.compressor_gain;
        let div_start: usize = self.pre_delay_read_index;

        let (left, right) = self.pre_delay_buffers.split_at_mut(1);

        // Exponential approach to desired gain.
        if envelope_rate < 1. {
            // Attack - reduce gain to desired.
            let c: f32 = compressor_gain - scaled_desired_gain;
            let base: f32 = scaled_desired_gain;
            let r: f32 = 1. - envelope_rate;
            let mut x: [f32; 4] = [c * r, c * r * r, c * r * r * r, c * r * r * r * r];
            let r4: f32 = r * r * r * r;
            for (i, chunk) in std::iter::zip(
                &mut left[0][div_start..(div_start + DIVISION_FRAMES)],
                &mut right[0][div_start..(div_start + DIVISION_FRAMES)],
            )
            .chunks(4)
            .into_iter()
            .enumerate()
            {
                if i != 0 {
                    for x_j in x.iter_mut() {
                        *x_j *= r4;
                    }
                }
                for ((iter_left, iter_right), x_j) in std::iter::zip(chunk, x) {
                    /* Warp pre-compression gain to smooth out sharp
                     * exponential transition points.
                     */
                    let post_warp_compressor_gain: f32 = drc_math::warp_sinf(x_j + base);

                    // Calculate total gain using main gain.
                    let total_gain: f32 = main_linear_gain * post_warp_compressor_gain;

                    // Apply final gain.
                    *iter_left *= total_gain;
                    *iter_right *= total_gain;
                }
            }

            self.compressor_gain = x[3] + base;
        } else {
            // Release - exponentially increase gain to 1.0
            let c: f32 = compressor_gain;
            let r: f32 = envelope_rate;
            let mut x: [f32; 4] = [c * r, c * r * r, c * r * r * r, c * r * r * r * r];
            let r4: f32 = r * r * r * r;
            for (i, chunk) in std::iter::zip(
                &mut left[0][div_start..(div_start + DIVISION_FRAMES)],
                &mut right[0][div_start..(div_start + DIVISION_FRAMES)],
            )
            .chunks(4)
            .into_iter()
            .enumerate()
            {
                if i != 0 {
                    for x_j in x.iter_mut() {
                        *x_j *= r4;
                    }
                }
                for ((iter_left, iter_right), x_j) in std::iter::zip(chunk, x) {
                    /* Warp pre-compression gain to smooth out sharp
                     * exponential transition points.
                     */
                    let post_warp_compressor_gain: f32 = drc_math::warp_sinf(x_j);

                    // Calculate total gain using main gain.
                    let total_gain: f32 = main_linear_gain * post_warp_compressor_gain;

                    // Apply final gain.
                    *iter_left *= total_gain;
                    *iter_right *= total_gain;
                }
            }

            self.compressor_gain = x[3];
        }
    }

    /// After one complete division of samples have been received (and one division
    /// of samples have been output), we calculate shaped power average
    /// (detector_average) from the input division, update envelope parameters from
    /// detector_average, then prepare the next output division by applying the
    /// envelope to compress the samples.
    ///
    fn process_one_division(&mut self) {
        self.update_detector_average();
        self.update_envelope();
        self.compress_output();
    }

    /// Copy the input data to the pre-delay buffer, and copy the output data back to
    /// the input buffer
    fn copy_fragment(
        &mut self,
        data_channels: &mut [&mut [f32]; DRC_NUM_CHANNELS],
        frame_index: usize,
        frames_to_process: usize,
    ) {
        let write_index: usize = self.pre_delay_write_index;
        let read_index: usize = self.pre_delay_read_index;

        for i in 0..DRC_NUM_CHANNELS {
            self.pre_delay_buffers[i][write_index..(write_index + frames_to_process)]
                .copy_from_slice(&data_channels[i][frame_index..(frame_index + frames_to_process)]);
            data_channels[i][frame_index..(frame_index + frames_to_process)].copy_from_slice(
                &self.pre_delay_buffers[i][read_index..(read_index + frames_to_process)],
            );
        }

        self.pre_delay_write_index = (write_index + frames_to_process) & MAX_PRE_DELAY_FRAMES_MASK;
        self.pre_delay_read_index = (read_index + frames_to_process) & MAX_PRE_DELAY_FRAMES_MASK;
    }

    /// Delay the input sample only and don't do other processing. This is used when
    /// the kernel is disabled. We want to do this to match the processing delay in
    /// kernels of other bands.
    ///
    fn process_delay_only(
        &mut self,
        data_channels: &mut [&mut [f32]; DRC_NUM_CHANNELS],
        count: usize,
    ) {
        let mut read_index: usize = self.pre_delay_read_index;
        let mut write_index: usize = self.pre_delay_write_index;
        let mut i: usize = 0;

        while i < count {
            let small: usize = read_index.min(write_index);
            let large: usize = read_index.max(write_index);
            /* chunk is the minimum of readable samples in contiguous
             * buffer, writable samples in contiguous buffer, and the
             * available input samples. */
            let mut chunk: usize = (large - small).min(MAX_PRE_DELAY_FRAMES - large);
            chunk = chunk.min(count - i);
            for j in 0..DRC_NUM_CHANNELS {
                self.pre_delay_buffers[j][write_index..(write_index + chunk)]
                    .copy_from_slice(&data_channels[j][i..(i + chunk)]);
                data_channels[j][i..(i + chunk)]
                    .copy_from_slice(&self.pre_delay_buffers[j][read_index..(read_index + chunk)]);
            }
            read_index = (read_index + chunk) & MAX_PRE_DELAY_FRAMES_MASK;
            write_index = (write_index + chunk) & MAX_PRE_DELAY_FRAMES_MASK;
            i += chunk;
        }

        self.pre_delay_read_index = read_index;
        self.pre_delay_write_index = write_index;
    }

    pub fn process(&mut self, data_channels: &mut [&mut [f32]; DRC_NUM_CHANNELS], count: usize) {
        let mut i: usize = 0;
        if !self.param.enabled {
            self.process_delay_only(data_channels, count);
            return;
        }

        if self.processed == 0 {
            self.update_envelope();
            self.compress_output();
            self.processed = 1;
        }

        let mut offset: usize = self.pre_delay_write_index & DIVISION_FRAMES_MASK;
        while i < count {
            let fragment = (DIVISION_FRAMES - offset).min(count - i);
            self.copy_fragment(data_channels, i, fragment);
            i += fragment;
            offset = (offset + fragment) & DIVISION_FRAMES_MASK;

            // Process the input division (32 frames).
            if offset == 0 {
                self.process_one_division();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::drc_kernel::DIVISION_FRAMES;
    use crate::drc_kernel::MAX_PRE_DELAY_FRAMES;

    #[test]
    fn drc_kernel_constant_test() {
        assert!(
            (DIVISION_FRAMES) != 0 && (((DIVISION_FRAMES) & ((DIVISION_FRAMES) - 1)) == 0),
            "expression is not a power of 2"
        );
        assert!((DIVISION_FRAMES % 4 == 0), "not a multiple of 4");
        assert!(
            (MAX_PRE_DELAY_FRAMES) != 0
                && (((MAX_PRE_DELAY_FRAMES) & ((MAX_PRE_DELAY_FRAMES) - 1)) == 0),
            "expression is not a power of 2"
        );
    }
}
