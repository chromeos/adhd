// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use itertools::izip;

use crate::biquad::Biquad;
use crate::crossover2::Crossover2;
use crate::drc_kernel::DrcKernel;
use crate::drc_kernel::DRC_NUM_CHANNELS;
use crate::eq2::EQ2;

/// DRC implements a flexible audio dynamics compression effect such as is
/// commonly used in musical production and game audio. It lowers the volume of
/// the loudest parts of the signal and raises the volume of the softest parts,
/// making the sound richer, fuller, and more controlled.
///
/// This is a three band stereo DRC. There are three compressor kernels, and each
/// can have its own parameters. If a kernel is disabled, it only delays the
/// signal and does not compress it.
///
/// ```text
///                   INPUT
///                     |
///                +----------+
///                | emphasis |
///                +----------+
///                     |
///               +------------+
///               | crossover  |
///               +------------+
///               /     |      \
///      (low band) (mid band) (high band)
///             /       |        \
///         +------+ +------+ +------+
///         |  drc | |  drc | |  drc |
///         |kernel| |kernel| |kernel|
///         +------+ +------+ +------+
///              \      |        /
///               \     |       /
///              +-------------+
///              |     (+)     |
///              +-------------+
///                     |
///              +------------+
///              | deemphasis |
///              +------------+
///                     |
///                   OUTPUT
/// ```
///

/// The parameters of the DRC compressor.
///
/// PARAM_THRESHOLD - The value above which the compression starts, in dB.
/// PARAM_KNEE - The value above which the knee region starts, in dB.
/// PARAM_RATIO - The input/output dB ratio after the knee region.
/// PARAM_ATTACK - The time to reduce the gain by 10dB, in seconds.
/// PARAM_RELEASE - The time to increase the gain by 10dB, in seconds.
/// PARAM_PRE_DELAY - The lookahead time for the compressor, in seconds.
/// PARAM_RELEASE_ZONE[1-4] - The adaptive release curve parameters.
/// PARAM_POST_GAIN - The static boost value in output, in dB.
/// PARAM_FILTER_STAGE_GAIN - The gain of each emphasis filter stage.
/// PARAM_FILTER_STAGE_RATIO - The frequency ratio for each emphasis filter stage
///     to the previous stage.
/// PARAM_FILTER_ANCHOR - The frequency of the first emphasis filter, in
///     normalized frequency (in [0, 1], relative to half of the sample rate).
/// PARAM_CROSSOVER_LOWER_FREQ - The lower frequency of the band, in normalized
///     frequency (in [0, 1], relative to half of the sample rate).
/// PARAM_ENABLED - 1 to enable the compressor, 0 to disable it.
///
#[derive(Clone, Copy, Debug, PartialEq)]
#[allow(non_camel_case_types)]
#[repr(C)]
pub enum DRC_PARAM {
    PARAM_THRESHOLD,
    PARAM_KNEE,
    PARAM_RATIO,
    PARAM_ATTACK,
    PARAM_RELEASE,
    PARAM_PRE_DELAY,
    PARAM_RELEASE_ZONE1,
    PARAM_RELEASE_ZONE2,
    PARAM_RELEASE_ZONE3,
    PARAM_RELEASE_ZONE4,
    PARAM_POST_GAIN,
    PARAM_FILTER_STAGE_GAIN,
    PARAM_FILTER_STAGE_RATIO,
    PARAM_FILTER_ANCHOR,
    PARAM_CROSSOVER_LOWER_FREQ,
    PARAM_ENABLED,
    PARAM_LAST,
}

impl TryFrom<u32> for DRC_PARAM {
    type Error = anyhow::Error;

    fn try_from(value: u32) -> anyhow::Result<Self> {
        match value {
            0 => Ok(DRC_PARAM::PARAM_THRESHOLD),
            1 => Ok(DRC_PARAM::PARAM_KNEE),
            2 => Ok(DRC_PARAM::PARAM_RATIO),
            3 => Ok(DRC_PARAM::PARAM_ATTACK),
            4 => Ok(DRC_PARAM::PARAM_RELEASE),
            5 => Ok(DRC_PARAM::PARAM_PRE_DELAY),
            6 => Ok(DRC_PARAM::PARAM_RELEASE_ZONE1),
            7 => Ok(DRC_PARAM::PARAM_RELEASE_ZONE2),
            8 => Ok(DRC_PARAM::PARAM_RELEASE_ZONE3),
            9 => Ok(DRC_PARAM::PARAM_RELEASE_ZONE4),
            10 => Ok(DRC_PARAM::PARAM_POST_GAIN),
            11 => Ok(DRC_PARAM::PARAM_FILTER_STAGE_GAIN),
            12 => Ok(DRC_PARAM::PARAM_FILTER_STAGE_RATIO),
            13 => Ok(DRC_PARAM::PARAM_FILTER_ANCHOR),
            14 => Ok(DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ),
            15 => Ok(DRC_PARAM::PARAM_ENABLED),
            _ => bail!("Invalid DRC_PARAM {value}"),
        }
    }
}

/// The number of compressor kernels (also the number of bands).
pub const DRC_NUM_KERNELS: usize = 3;

/// The number of stages for emphasis and deemphasis filters.
pub const DRC_EMPHASIS_NUM_STAGES: usize = 2;

/// The maximum number of frames can be passed to drc_process() call.
pub const DRC_PROCESS_MAX_FRAMES: usize = 2048;

/// The default value of PARAM_PRE_DELAY in seconds.
pub const DRC_DEFAULT_PRE_DELAY: f32 = 0.006;

/// The structure is only used for exposing necessary components of drc to
/// FFI.
///
#[repr(C)]
pub struct DRCComponent {
    /// true to disable the emphasis and deemphasis, false to enable it.
    pub emphasis_disabled: bool,

    /// parameters holds the tweakable compressor parameters.
    pub parameters: [[f32; 16]; DRC_NUM_KERNELS],

    /// The emphasis filter and deemphasis filter
    pub emphasis_eq: *const EQ2,
    pub deemphasis_eq: *const EQ2,

    /// The crossover filter
    pub xo2: *const Crossover2,

    /// The compressor kernels
    pub kernel: [*const DrcKernel; DRC_NUM_KERNELS],
}

#[derive(Default)]
pub struct DRC {
    /// sample rate in Hz
    sample_rate: f32,

    /// true to disable the emphasis and deemphasis, false to enable it.
    pub emphasis_disabled: bool,

    /// parameters holds the tweakable compressor parameters.
    pub parameters: [[f32; DRC_PARAM::PARAM_LAST as usize]; DRC_NUM_KERNELS],

    /// The emphasis filter and deemphasis filter
    pub emphasis_eq: EQ2,
    pub deemphasis_eq: EQ2,

    /// The crossover filter
    pub xo2: Crossover2,

    /// The compressor kernels
    pub kernel: [DrcKernel; DRC_NUM_KERNELS],

    /// Temporary buffer used during drc_process(). The mid and high band
    /// signal is stored in these buffers (the low band is stored in the
    /// original input buffer).
    data1: [Vec<f32>; DRC_NUM_CHANNELS],
    data2: [Vec<f32>; DRC_NUM_CHANNELS],
}

/// DRC needs the parameters to be set before initialization. So drc_new() should
/// be called first to allocated an instance, then drc_set_param() is called
/// (multiple times) to set the parameters. Finally drc_init() is called to do
/// the initialization. After that drc_process() can be used to process data. The
/// sequence is:
///
///  drc_new();
///  drc_set_param();
///  ...
///  drc_set_param();
///  drc_init();
///  drc_process();
///  ...
///  drc_process();
///  drc_free();
///
impl DRC {
    pub fn new(sample_rate: f32) -> Self {
        let mut drc = DRC::default();
        drc.sample_rate = sample_rate;
        drc.set_default_parameters();
        drc
    }

    pub fn init(&mut self) {
        self.init_data_buffer();
        self.init_emphasis_eq();
        self.init_crossover();
        self.init_kernel();
    }

    /// Allocates temporary buffers used during drc_process().
    fn init_data_buffer(&mut self) {
        for (datum1, datum2) in std::iter::zip(&mut self.data1, &mut self.data2) {
            *datum1 = vec![0_f32; DRC_PROCESS_MAX_FRAMES];
            *datum2 = vec![0_f32; DRC_PROCESS_MAX_FRAMES];
        }
    }

    pub fn set_param(&mut self, index: usize, param_id: DRC_PARAM, value: f32) {
        self.parameters[index][param_id as usize] = value;
    }

    fn get_param(&self, index: usize, param_id: DRC_PARAM) -> f32 {
        return self.parameters[index][param_id as usize];
    }

    /// Initializes parameters to default values.
    fn set_default_parameters(&mut self) {
        let nyquist: f32 = self.sample_rate / 2.;
        for param in self.parameters.iter_mut() {
            param[DRC_PARAM::PARAM_THRESHOLD as usize] = -24.; // dB
            param[DRC_PARAM::PARAM_KNEE as usize] = 30.; // dB
            param[DRC_PARAM::PARAM_RATIO as usize] = 12.; // unit-less
            param[DRC_PARAM::PARAM_ATTACK as usize] = 0.003; // seconds
            param[DRC_PARAM::PARAM_RELEASE as usize] = 0.250; // seconds
            param[DRC_PARAM::PARAM_PRE_DELAY as usize] = DRC_DEFAULT_PRE_DELAY; // seconds

            // Release zone values 0 -> 1.
            param[DRC_PARAM::PARAM_RELEASE_ZONE1 as usize] = 0.09;
            param[DRC_PARAM::PARAM_RELEASE_ZONE2 as usize] = 0.16;
            param[DRC_PARAM::PARAM_RELEASE_ZONE3 as usize] = 0.42;
            param[DRC_PARAM::PARAM_RELEASE_ZONE4 as usize] = 0.98;

            /* This is effectively a main volume on the compressed
             * signal */
            param[DRC_PARAM::PARAM_POST_GAIN as usize] = 0.; // dB
            param[DRC_PARAM::PARAM_ENABLED as usize] = 0.;
        }

        self.parameters[0][DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ as usize] = 0.;
        self.parameters[1][DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ as usize] = 200. / nyquist;
        self.parameters[2][DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ as usize] = 2000. / nyquist;

        // These parameters has only one copy
        self.parameters[0][DRC_PARAM::PARAM_FILTER_STAGE_GAIN as usize] = 4.4; // dB
        self.parameters[0][DRC_PARAM::PARAM_FILTER_STAGE_RATIO as usize] = 2.;
        self.parameters[0][DRC_PARAM::PARAM_FILTER_ANCHOR as usize] = 15000. / nyquist;
    }

    /// Finds the zero and pole for one stage of the emphasis filter
    fn emphasis_stage_roots(gain: f32, normalized_frequency: f32) -> (f32, f32) {
        let gk: f32 = 1. - gain / 20.;
        let f1: f32 = normalized_frequency * gk;
        let f2: f32 = normalized_frequency / gk;
        (
            (-f1 * std::f32::consts::PI).exp(),
            (-f2 * std::f32::consts::PI).exp(),
        )
    }

    /// Calculates the biquad coefficients for two emphasis stages.
    fn emphasis_stage_pair_biquads(
        gain: f32,
        f1: f32,
        f2: f32,
        emphasis: &mut Biquad,
        deemphasis: &mut Biquad,
    ) {
        let (z1, p1) = Self::emphasis_stage_roots(gain, f1);
        let (z2, p2) = Self::emphasis_stage_roots(gain, f2);

        let b0: f32 = 1.;
        let b1: f32 = -(z1 + z2);
        let b2: f32 = z1 * z2;
        let a0: f32 = 1.;
        let a1: f32 = -(p1 + p2);
        let a2: f32 = p1 * p2;

        // Gain compensation to make 0dB @ 0Hz
        let alpha: f32 = (a0 + a1 + a2) / (b0 + b1 + b2);

        emphasis.b0 = b0 * alpha;
        emphasis.b1 = b1 * alpha;
        emphasis.b2 = b2 * alpha;
        emphasis.a1 = a1;
        emphasis.a2 = a2;

        let beta: f32 = (b0 + b1 + b2) / (a0 + a1 + a2);

        deemphasis.b0 = a0 * beta;
        deemphasis.b1 = a1 * beta;
        deemphasis.b2 = a2 * beta;
        deemphasis.a1 = b1;
        deemphasis.a2 = b2;
    }

    /// Initializes the emphasis and deemphasis filter
    fn init_emphasis_eq(&mut self) {
        let mut e = Biquad {
            b0: 0.,
            b1: 0.,
            b2: 0.,
            a1: 0.,
            a2: 0.,
            x1: 0.,
            x2: 0.,
            y1: 0.,
            y2: 0.,
        };
        let mut d = Biquad {
            b0: 0.,
            b1: 0.,
            b2: 0.,
            a1: 0.,
            a2: 0.,
            x1: 0.,
            x2: 0.,
            y1: 0.,
            y2: 0.,
        };

        let stage_gain: f32 = self.get_param(0, DRC_PARAM::PARAM_FILTER_STAGE_GAIN);
        let stage_ratio: f32 = self.get_param(0, DRC_PARAM::PARAM_FILTER_STAGE_RATIO);
        let mut anchor_freq: f32 = self.get_param(0, DRC_PARAM::PARAM_FILTER_ANCHOR);

        self.emphasis_eq = EQ2::new();
        self.deemphasis_eq = EQ2::new();

        for _i in 0..DRC_EMPHASIS_NUM_STAGES {
            Self::emphasis_stage_pair_biquads(
                stage_gain,
                anchor_freq,
                anchor_freq / stage_ratio,
                &mut e,
                &mut d,
            );
            for j in 0..DRC_NUM_CHANNELS {
                self.emphasis_eq
                    .append_biquad_direct(j, e)
                    .expect("append_biquad_direct failed in init_emphasis_eq");
                self.deemphasis_eq
                    .append_biquad_direct(j, d)
                    .expect("append_biquad_direct failed in init_emphasis_eq");
            }
            anchor_freq /= stage_ratio * stage_ratio;
        }
    }

    /// Initializes the crossover filter
    fn init_crossover(&mut self) {
        let freq1: f32 = self.parameters[1][DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ as usize];
        let freq2: f32 = self.parameters[2][DRC_PARAM::PARAM_CROSSOVER_LOWER_FREQ as usize];

        self.xo2.init(freq1 as f64, freq2 as f64);
    }

    #[allow(non_snake_case)]
    /// Initializes the compressor kernels
    fn init_kernel(&mut self) {
        for i in 0..DRC_NUM_KERNELS {
            self.kernel[i] = DrcKernel::new(self.sample_rate);

            let db_threshold: f32 = self.get_param(i, DRC_PARAM::PARAM_THRESHOLD);
            let db_knee: f32 = self.get_param(i, DRC_PARAM::PARAM_KNEE);
            let ratio: f32 = self.get_param(i, DRC_PARAM::PARAM_RATIO);
            let attack_time: f32 = self.get_param(i, DRC_PARAM::PARAM_ATTACK);
            let release_time: f32 = self.get_param(i, DRC_PARAM::PARAM_RELEASE);
            let pre_delay_time: f32 = self.get_param(i, DRC_PARAM::PARAM_PRE_DELAY);
            let releaseZone1: f32 = self.get_param(i, DRC_PARAM::PARAM_RELEASE_ZONE1);
            let releaseZone2: f32 = self.get_param(i, DRC_PARAM::PARAM_RELEASE_ZONE2);
            let releaseZone3: f32 = self.get_param(i, DRC_PARAM::PARAM_RELEASE_ZONE3);
            let releaseZone4: f32 = self.get_param(i, DRC_PARAM::PARAM_RELEASE_ZONE4);
            let db_post_gain: f32 = self.get_param(i, DRC_PARAM::PARAM_POST_GAIN);
            let enabled: i32 = self.get_param(i, DRC_PARAM::PARAM_ENABLED) as i32;

            self.kernel[i].set_parameters(
                db_threshold,
                db_knee,
                ratio,
                attack_time,
                release_time,
                pre_delay_time,
                db_post_gain,
                releaseZone1,
                releaseZone2,
                releaseZone3,
                releaseZone4,
            );

            self.kernel[i].set_enabled(enabled != 0);
        }
    }

    fn sum3(data: &mut [f32], data1: &[f32], data2: &[f32]) {
        for (data_i, data1_i, data2_i) in izip!(data, data1, data2) {
            *data_i += (*data1_i) + (*data2_i);
        }
    }

    pub fn process(&mut self, data: &mut [&mut [f32]; DRC_NUM_CHANNELS], frames: usize) {
        let (left, right) = data.split_at_mut(1);
        // Apply pre-emphasis filter if it is not disabled.
        if !self.emphasis_disabled {
            self.emphasis_eq
                .process(&mut left[0][0..frames], &mut right[0][0..frames]);
        }

        let (data_0, data_1) = data.split_at_mut(1);
        let (data1_0, data1_1) = self.data1.split_at_mut(1);
        let (data2_0, data2_1) = self.data2.split_at_mut(1);

        // Crossover
        self.xo2.process(
            &mut data_0[0][..frames],
            &mut data_1[0][..frames],
            &mut data1_0[0][..frames],
            &mut data1_1[0][..frames],
            &mut data2_0[0][..frames],
            &mut data2_1[0][..frames],
        );

        let mut data1: [&mut [f32]; DRC_NUM_CHANNELS] =
            self.data1.each_mut().map(|vec| &mut vec[..]);
        let mut data2: [&mut [f32]; DRC_NUM_CHANNELS] =
            self.data2.each_mut().map(|vec| &mut vec[..]);

        /* Apply compression to each band of the signal. The processing is
         * performed in place.
         */
        self.kernel[0].process(data, frames);
        self.kernel[1].process(&mut data1, frames);
        self.kernel[2].process(&mut data2, frames);

        // Sum the three bands of signal
        for i in 0..DRC_NUM_CHANNELS {
            Self::sum3(data[i], data1[i], data2[i]);
        }

        // Apply de-emphasis filter if emphasis is not disabled.
        if !self.emphasis_disabled {
            let (left, right) = data.split_at_mut(1);
            self.deemphasis_eq.process(left[0], right[0]);
        }
    }
}
