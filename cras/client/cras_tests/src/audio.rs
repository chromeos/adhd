// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error;
use std::fmt;
use std::fs::File;
use std::io;
use std::io::BufReader;
use std::io::BufWriter;
use std::io::Read;
use std::io::Write;
use std::mem::MaybeUninit;
use std::os::raw::c_int;
use std::path::Path;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

use audio_streams::SampleFormat;
use audio_streams::StreamSource;
use cras_sys::gen::CRAS_SPECIAL_DEVICE;
use either::Either;
use hound::WavReader;
use hound::WavSpec;
use hound::WavWriter;
use libcras::BoxError;
use libcras::CrasClient;
use libcras::CrasClientType;
use libcras::CrasNodeType;
use libcras::CrasStreamEffect;
use nix::sys::signal::sigaction;
use nix::sys::signal::SaFlags;
use nix::sys::signal::SigAction;
use nix::sys::signal::SigHandler;
use nix::sys::signal::SigSet;
use nix::sys::signal::Signal;

use crate::arguments::AudioOptions;
use crate::arguments::FileType;
use crate::arguments::LoopbackType;
use crate::arguments::SampleFormatArg;

#[derive(Debug)]
pub enum Error {
    CreateStream(BoxError),
    FetchStream(BoxError),
    InvalidWavFile(hound::Error),
    Io(io::Error),
    Libcras(libcras::Error),
    NoLoopbackNode(CrasNodeType),
    OpenFile(hound::Error),
    SampleBits(u16),
    SysUtil(nix::Error),
    ParseArgs(crate::arguments::Error),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            CreateStream(e) => write!(f, "Failed to create stream: {}", e),
            FetchStream(e) => write!(f, "Failed to fetch buffer from stream: {}", e),
            InvalidWavFile(e) => write!(f, "Could not open file as WAV file: {}", e),
            Io(e) => write!(f, "IO Error: {}", e),
            Libcras(e) => write!(f, "Libcras Error: {}", e),
            NoLoopbackNode(typ) => write!(f, "No loopback node found with type {:?}", typ),
            OpenFile(e) => write!(f, "Could not open WAV file for writing: {}", e),
            SampleBits(bits) => write!(
                f,
                "Sample size {} is not supported, only 8, 16, 24, and 32 bit samples are supported",
                bits
            ),
            SysUtil(e) => write!(f, "SysUtil Error: {}", e),
            ParseArgs(e) => write!(f, "{}", e),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

static INTERRUPTED: AtomicBool = AtomicBool::new(false);

extern "C" fn sigint_handler(_: c_int) {
    // Check if we've already received one SIGINT. If we have, the program may
    // be misbehaving and not terminating, so to be safe we'll forcefully exit.
    if INTERRUPTED.load(Ordering::Acquire) {
        std::process::exit(1);
    }
    INTERRUPTED.store(true, Ordering::Release);
}

fn add_sigint_handler() -> Result<()> {
    // SAFETY: sigint_handler only touches an AtomicBool.
    unsafe {
        sigaction(
            Signal::SIGINT,
            &SigAction::new(
                SigHandler::Handler(sigint_handler),
                SaFlags::SA_RESTART,
                SigSet::empty(),
            ),
        )
    }
    .map_err(Error::SysUtil)?;
    Ok(())
}

fn set_priority_to_realtime() {
    const AUDIO_THREAD_RTPRIO: u16 = 10;
    if set_rt_prio_limit(AUDIO_THREAD_RTPRIO as u64).is_err()
        || set_rt_round_robin(AUDIO_THREAD_RTPRIO as i32).is_err()
    {
        println!("Attempt to use real-time priority failed, running with default scheduler.");
    }
}

fn channel_string(num_channels: usize) -> String {
    match num_channels {
        1 => "Mono".to_string(),
        2 => "Stereo".to_string(),
        _ => format!("{} Channels", num_channels),
    }
}

struct WavSource {
    wav_reader: WavReader<BufReader<File>>,
    format: SampleFormat,
    num_channels: usize,
    frame_rate: u32,
}

impl WavSource {
    fn try_new(opts: &AudioOptions) -> Result<Self> {
        let wav_reader = WavReader::open(&opts.file_name).map_err(Error::InvalidWavFile)?;
        let spec = wav_reader.spec();
        let format = match (spec.sample_format, spec.bits_per_sample) {
            (hound::SampleFormat::Int, 8) => SampleFormat::U8,
            (hound::SampleFormat::Int, 16) => SampleFormat::S16LE,
            (hound::SampleFormat::Int, 24) => SampleFormat::S24LE,
            (hound::SampleFormat::Int, 32) => SampleFormat::S32LE,
            // Use S32_LE as the output type for f32.
            (hound::SampleFormat::Float, 32) => SampleFormat::S32LE,
            (_, bits) => return Err(Error::SampleBits(bits)),
        };

        if let Some(formatopt) = opts.format {
            if formatopt.to_sample_format() != format {
                eprintln!("Warning: format changed to {:?}", format);
            }
        }

        let num_channels = spec.channels as usize;
        if opts.num_channels.is_some() && Some(num_channels) != opts.num_channels {
            eprintln!("Warning: number of channels changed to {}", num_channels);
        }

        let frame_rate = spec.sample_rate;
        if opts.frame_rate.is_some() && Some(frame_rate) != opts.frame_rate {
            eprintln!("Warning: frame rate changed to {}", frame_rate);
        }

        Ok(Self {
            wav_reader,
            format,
            num_channels,
            frame_rate,
        })
    }

    fn format(&self) -> SampleFormat {
        self.format
    }

    fn num_channels(&self) -> usize {
        self.num_channels
    }

    fn frame_rate(&self) -> u32 {
        self.frame_rate
    }
}

impl Read for WavSource {
    fn read(&mut self, mut buf: &mut [u8]) -> io::Result<usize> {
        let frame_size = self.format.sample_bytes() * self.num_channels;
        let read_len = buf.len() - buf.len() % frame_size;
        let num_samples = read_len / self.format.sample_bytes();

        let samples = match self.wav_reader.spec().sample_format {
            hound::SampleFormat::Int => Either::Left(self.wav_reader.samples::<i32>()),
            hound::SampleFormat::Float => Either::Right(
                self.wav_reader
                    .samples::<f32>()
                    .map(|s| s.map(dasp_sample::conv::f32::to_i32)),
            ),
        };

        let mut read = 0;
        for s in samples.take(num_samples) {
            match s {
                Ok(sample) => {
                    let result = match self.format {
                        SampleFormat::U8 => buf.write_all(&((sample + 128) as u8).to_le_bytes()),
                        SampleFormat::S16LE => buf.write_all(&(sample as i16).to_le_bytes()),
                        SampleFormat::S24LE | SampleFormat::S32LE => {
                            buf.write_all(&sample.to_le_bytes())
                        }
                    };

                    match result {
                        Ok(()) => read += self.format.sample_bytes(),
                        Err(_) => return Ok(read),
                    };
                }
                Err(_) => return Ok(read),
            };
        }
        Ok(read)
    }
}

pub fn playback(opts: AudioOptions) -> Result<()> {
    let num_channels;
    let frame_rate;
    let format;
    let file_type = opts.file_type().map_err(Error::ParseArgs)?;
    assert!(opts.effects.is_none(), "playback effects not supported");

    let mut sample_source: Box<dyn Read> = match file_type {
        FileType::Wav => {
            let wav_source = WavSource::try_new(&opts)?;
            num_channels = wav_source.num_channels();
            frame_rate = wav_source.frame_rate();
            format = wav_source.format();
            Box::new(wav_source)
        }
        FileType::Raw => {
            num_channels = opts.num_channels.unwrap_or(2);
            frame_rate = opts.frame_rate.unwrap_or(48000);
            format = opts
                .format
                .unwrap_or(SampleFormatArg::S16LE)
                .to_sample_format();
            Box::new(BufReader::new(
                File::open(&opts.file_name).map_err(Error::Io)?,
            ))
        }
    };

    let mut duration_frames = opts
        .duration_sec
        .map(|duration_sec| duration_sec * (frame_rate as usize));

    println!(
        "Playing {} '{}' : {}, Rate {} Hz, {}",
        file_type,
        opts.file_name.display(),
        format,
        frame_rate,
        channel_string(num_channels),
    );

    let mut cras_client = CrasClient::new().map_err(Error::Libcras)?;
    cras_client.set_client_type(CrasClientType::CRAS_CLIENT_TYPE_TEST);
    let (_control, mut stream) = cras_client
        .new_playback_stream(
            num_channels,
            format,
            frame_rate,
            opts.buffer_size.unwrap_or(256),
        )
        .map_err(Error::CreateStream)?;
    set_priority_to_realtime();

    add_sigint_handler()?;
    while !INTERRUPTED.load(Ordering::Acquire) {
        let mut buffer = stream.next_playback_buffer().map_err(Error::FetchStream)?;

        let frame_size = num_channels * format.sample_bytes();
        let frames = buffer.frame_capacity();

        let mut chunk = (&mut sample_source).take((frames * frame_size) as u64);
        let transferred = io::copy(&mut chunk, &mut buffer).map_err(Error::Io)?;
        let transferred_frames = (transferred / frame_size as u64) as usize;

        buffer.commit();

        // if duration is specified
        if let Some(frames) = duration_frames {
            let remaining_frames = frames.saturating_sub(transferred_frames);
            if remaining_frames == 0 {
                break;
            }
            duration_frames = Some(remaining_frames);
        }

        if transferred == 0 {
            break;
        }
    }
    // Stream and client should gracefully be closed out of this scope

    Ok(())
}

struct WavSink {
    wav_writer: WavWriter<BufWriter<File>>,
    format: SampleFormat,
}

impl WavSink {
    fn try_new<P: AsRef<Path>>(
        path: P,
        num_channels: usize,
        format: SampleFormat,
        frame_rate: u32,
    ) -> Result<Self> {
        let spec = WavSpec {
            channels: num_channels as u16,
            sample_rate: frame_rate,
            bits_per_sample: (format.sample_bytes() * 8) as u16,
            sample_format: hound::SampleFormat::Int,
        };
        let wav_writer = WavWriter::create(path, spec).map_err(Error::OpenFile)?;
        Ok(Self { wav_writer, format })
    }
}

impl Write for WavSink {
    fn write(&mut self, samples: &[u8]) -> io::Result<usize> {
        let sample_bytes = self.format.sample_bytes();
        if samples.len() % sample_bytes != 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "u8 samples vector of length {} cannot be interpreted as {:?} samples",
                    samples.len(),
                    self.format
                ),
            ));
        }
        let num_samples = samples.len() / sample_bytes;
        match self.format {
            SampleFormat::U8 => {
                for sample in samples {
                    self.wav_writer.write_sample(*sample as i8).map_err(|e| {
                        io::Error::new(
                            io::ErrorKind::Other,
                            format!("Failed to write sample: {}", e),
                        )
                    })?;
                }
            }
            SampleFormat::S16LE => {
                // hound offers an optimized i16 writer, so special case here.
                let mut writer = self.wav_writer.get_i16_writer(num_samples as u32);
                for i in 0..num_samples {
                    let sample = i16::from_le_bytes([
                        samples[sample_bytes * i],
                        samples[sample_bytes * i + 1],
                    ]);
                    writer.write_sample(sample);
                }
                // I16Writer buffers internally and must be explicitly flushed to write
                // samples to the backing writer. Flush is not called automatically
                // on drop.
                // The flush method only writes data from the i16_writer to the underlying
                // WavWriter, it does not actually guarantee a flush to disk.
                writer.flush().map_err(|e| {
                    io::Error::new(
                        io::ErrorKind::Other,
                        format!("Failed to flush SampleWriter: {}", e),
                    )
                })?;
            }
            SampleFormat::S24LE | SampleFormat::S32LE => {
                for i in 0..num_samples {
                    let mut sample = i32::from_le_bytes([
                        samples[sample_bytes * i],
                        samples[sample_bytes * i + 1],
                        samples[sample_bytes * i + 2],
                        samples[sample_bytes * i + 3],
                    ]);

                    // Upsample to 32 bit since CRAS doesn't support S24_3LE.
                    // Our wav encoder/decoder, hound, does have support for
                    // S24_LE, but it hasn't released a new version since the
                    // support was added. If getting that support is an issue,
                    // push upstream to cut a new a release.
                    if self.format == SampleFormat::S24LE {
                        sample <<= 8;
                    }

                    self.wav_writer.write_sample(sample).map_err(|e| {
                        io::Error::new(
                            io::ErrorKind::Other,
                            format!("Failed to write sample: {}", e),
                        )
                    })?;
                }
            }
        }

        Ok(samples.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        self.wav_writer.flush().map_err(|e| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("Failed to flush WavWriter: {}", e),
            )
        })
    }
}

pub fn capture(opts: AudioOptions) -> Result<()> {
    let num_channels = opts.num_channels.unwrap_or(2);
    let format = opts
        .format
        .unwrap_or(SampleFormatArg::S16LE)
        .to_sample_format();
    let frame_rate = opts.frame_rate.unwrap_or(48000);
    let buffer_size = opts.buffer_size.unwrap_or(256);
    let file_type = opts.file_type().map_err(Error::ParseArgs)?;

    let mut sample_sink: Box<dyn Write> = match file_type {
        FileType::Raw => Box::new(BufWriter::new(
            File::create(&opts.file_name).map_err(Error::Io)?,
        )),
        FileType::Wav => Box::new(WavSink::try_new(
            &opts.file_name,
            num_channels,
            format,
            frame_rate,
        )?),
    };

    let mut duration_frames = opts
        .duration_sec
        .map(|duration_sec| duration_sec * (frame_rate as usize));

    println!(
        "Recording {} '{}' : {}, Rate {} Hz, {}",
        file_type,
        opts.file_name.display(),
        format,
        frame_rate,
        channel_string(num_channels)
    );

    let mut cras_client = CrasClient::new().map_err(Error::Libcras)?;
    cras_client.set_client_type(CrasClientType::CRAS_CLIENT_TYPE_TEST);
    cras_client.enable_cras_capture();

    let pin_iodev_index = match opts.loopback_type {
        Some(loopback_type) => {
            let node_type = match loopback_type {
                LoopbackType::PreDsp => CrasNodeType::CRAS_NODE_TYPE_POST_MIX_PRE_DSP,
                LoopbackType::PostDsp => CrasNodeType::CRAS_NODE_TYPE_POST_DSP,
            };
            let loopback_node = cras_client
                .input_nodes()
                .find(|node| node.node_type == node_type)
                .ok_or(Error::NoLoopbackNode(node_type))?;
            loopback_node.iodev_index
        }
        None => CRAS_SPECIAL_DEVICE::NO_DEVICE as u32,
    };
    let (_control, mut stream) = cras_client
        .new_pinned_capture_stream(
            pin_iodev_index,
            num_channels,
            format,
            frame_rate,
            buffer_size,
            CrasStreamEffect(opts.effects.unwrap_or(0)),
        )
        .map_err(Error::CreateStream)?;

    set_priority_to_realtime();
    add_sigint_handler()?;
    while !INTERRUPTED.load(Ordering::Acquire) {
        let mut buf = stream.next_capture_buffer().map_err(Error::FetchStream)?;
        let transferred = io::copy(&mut buf, &mut sample_sink).map_err(Error::Io)?;
        let frame_size = num_channels * format.sample_bytes();
        let transferred_frames = (transferred / frame_size as u64) as usize;
        buf.commit();

        // if duration is specified
        if let Some(frames) = duration_frames {
            let remaining_frames = frames.saturating_sub(transferred_frames);
            if remaining_frames == 0 {
                break;
            }
            duration_frames = Some(remaining_frames);
        }
    }
    Ok(())
}

/// Enables real time thread priorities in the current thread up to `limit`.
pub fn set_rt_prio_limit(limit: u64) -> nix::Result<()> {
    let rt_limit_arg = libc::rlimit64 {
        rlim_cur: limit,
        rlim_max: limit,
    };
    // Safe because the kernel doesn't modify memory that is accessible to the process here.
    let res = unsafe { libc::setrlimit64(libc::RLIMIT_RTPRIO, &rt_limit_arg) };

    if res != 0 {
        Err(nix::Error::last())
    } else {
        Ok(())
    }
}

/// Sets the current thread to be scheduled using the round robin real time class with `priority`.
pub fn set_rt_round_robin(priority: i32) -> nix::Result<()> {
    // SAFETY: Safe because sched_param only contains primitive types for which zero
    // initialization is valid.
    let mut sched_param: libc::sched_param = unsafe { MaybeUninit::zeroed().assume_init() };
    sched_param.sched_priority = priority;

    // SAFETY: Safe because the kernel doesn't modify memory that is accessible to the process here.
    let res =
        unsafe { libc::pthread_setschedparam(libc::pthread_self(), libc::SCHED_RR, &sched_param) };

    if res != 0 {
        Err(nix::Error::last())
    } else {
        Ok(())
    }
}
