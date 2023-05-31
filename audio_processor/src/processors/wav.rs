// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use dasp_sample::ToSample;

use crate::AudioProcessor;
use crate::Error;
use crate::MultiBuffer;
use crate::MultiSlice;
use crate::Result;
use crate::Sample;
use crate::Shape;

#[derive(Debug, PartialEq)]
enum Type {
    I8,
    I16,
    I24,
    I32,
    F32,
}

/// Get the storage [Type] for the wav spec.
fn storage_type(spec: hound::WavSpec) -> Type {
    use hound::SampleFormat::Float;
    use hound::SampleFormat::Int;
    use Type::*;

    match (spec.sample_format, spec.bits_per_sample) {
        (Float, 32) => F32,
        (Int, 8) => I8,
        (Int, 16) => I16,
        (Int, 24) => I24,
        (Int, 32) => I32,
        _ => panic!(
            "invalid bits per sample {} for {:?}",
            spec.bits_per_sample, spec.sample_format
        ),
    }
}

/// `WavSource` is an [`AudioProcessor`] which reads audio from the
/// [`hound::WavReader`]. It outputs up to `block_size` frames each iteration.
/// If there are not enough audio samples remaining in the file it outputs
/// the remaining samples.
///
/// The output format of `WavSource` is always `f32`, regardless of the
/// format of [`hound::WavReader`].
///
/// The `input` passed to `WavSource` is ignored.
pub struct WavSource<R> {
    spec: hound::WavSpec,
    reader: hound::WavReader<R>,
    block_size: usize,
    block: MultiBuffer<f32>,

    storage_type: Type,
}

impl<R> WavSource<R>
where
    R: std::io::Read,
{
    /// Create a `WavSource` from the `reader`. The created `WavSource` reads
    /// `block_size` frames from the `reader` in each [`AudioProcessor::process()`] call.
    pub fn new(reader: hound::WavReader<R>, block_size: usize) -> Self {
        let spec = reader.spec();
        let channels = spec.channels as usize;
        Self {
            spec,
            reader,
            block_size,
            block: MultiBuffer::new(Shape {
                channels,
                frames: block_size,
            }),
            storage_type: storage_type(spec),
        }
    }

    /// Take `self.block_size` samples from `self.reader`.
    /// The result is stored in `self.block`.
    /// `T` is the storage type used to acquire samples from [hound].
    /// `U` is the conversion type used to convert from the type stored
    /// in the wave file to `f32`.
    fn take<T, U>(&mut self) -> std::result::Result<usize, hound::Error>
    where
        T: Sample + hound::Sample,
        U: dasp_sample::Sample + From<T> + ToSample<f32>,
    {
        let channels = self.spec.channels as usize;
        let mut nread = 0;
        for (i, s) in self
            .reader
            .samples::<T>()
            .take(self.block_size * channels)
            .enumerate()
        {
            self.block.data[i % channels][i / channels] = U::from(s?).to_sample::<f32>();
            nread += 1
        }

        Ok(nread / channels)
    }
}

impl<R> AudioProcessor for WavSource<R>
where
    R: std::io::Read,
{
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        _input: MultiSlice<'a, Self::I>,
    ) -> Result<MultiSlice<'a, Self::O>> {
        use Type::*;
        let result = match self.storage_type {
            I8 => self.take::<i8, i8>(),
            I16 => self.take::<i16, i16>(),
            I24 => self.take::<i32, dasp_sample::I24>(),
            I32 => self.take::<i32, i32>(),
            F32 => self.take::<f32, f32>(),
        }
        .expect("error reading wav file");

        Ok(self.block.as_multi_slice().into_indexes(0..result))
    }
}

/// `WavSink` is an [`AudioProcessor`] which writes audio samples to the
/// [`hound::WavWriter`].
///
/// In each [`AudioProcessor::process()`] call, the input is returned unmodified.
///
/// Only writing of `f32` samples is supported.
pub struct WavSink<W>
where
    W: std::io::Write + std::io::Seek,
{
    writer: hound::WavWriter<W>,
}

impl<W> WavSink<W>
where
    W: std::io::Write + std::io::Seek,
{
    /// Create a new `WavSink` which writes the audio frames passed to it
    /// to the `writer`.
    pub fn new(writer: hound::WavWriter<W>) -> WavSink<W> {
        Self { writer }
    }
}

impl<W> AudioProcessor for WavSink<W>
where
    W: std::io::Write + std::io::Seek,
{
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: MultiSlice<'a, Self::I>,
    ) -> Result<MultiSlice<'a, Self::O>> {
        for i in 0..input.min_len() {
            for ch in input.iter() {
                self.writer.write_sample(ch[i]).map_err(Error::Wav)?
            }
        }

        Ok(input)
    }
}

#[cfg(test)]
mod tests {
    use super::WavSink;
    use super::WavSource;
    use crate::AudioProcessor;
    use crate::Error;
    use crate::MultiBuffer;
    use crate::Shape;

    fn spec(sample_format: hound::SampleFormat, bits_per_sample: u16) -> hound::WavSpec {
        hound::WavSpec {
            channels: 2,
            sample_rate: 48000,
            bits_per_sample,
            sample_format,
        }
    }

    #[test]
    fn storage_type() {
        use hound::SampleFormat::*;

        use super::storage_type;
        use super::Type::*;

        assert_eq!(storage_type(spec(Float, 32)), F32);
        assert_eq!(storage_type(spec(Int, 32)), I32);
        assert_eq!(storage_type(spec(Int, 24)), I24);
        assert_eq!(storage_type(spec(Int, 16)), I16);
        assert_eq!(storage_type(spec(Int, 8)), I8);
    }

    #[test]
    #[should_panic(expected = "invalid bits per sample 23 for Int")]
    fn storage_type_invalid_int23() {
        use hound::SampleFormat::*;

        use super::storage_type;
        storage_type(spec(Int, 23));
    }

    #[test]
    #[should_panic(expected = "invalid bits per sample 23 for Float")]
    fn storage_type_invalid_float23() {
        use hound::SampleFormat::*;

        use super::storage_type;
        storage_type(spec(Float, 23));
    }

    #[test]
    fn wav_roundtrip() {
        let spec = hound::WavSpec {
            bits_per_sample: 32,
            channels: 2,
            sample_rate: 48000,
            sample_format: hound::SampleFormat::Float,
        };

        let mut buf = Vec::new();

        let mut sink =
            WavSink::new(hound::WavWriter::new(std::io::Cursor::new(&mut buf), spec).unwrap());

        // Content to write to the wav file
        // Data in channel-0: 1, 2, 5, 6, 7
        // Data in channel-1: 3, 4, 8, 9, 10
        let mut data1 = MultiBuffer::from(vec![vec![1f32, 2.], vec![3., 4.]]);
        let mut data2 = MultiBuffer::from(vec![vec![5f32, 6., 7.], vec![8., 9., 10.]]);

        sink.process(data1.as_multi_slice()).unwrap();
        sink.process(data2.as_multi_slice()).unwrap();

        // Flush the buffer
        drop(sink);

        let mut source = WavSource::new(
            hound::WavReader::new(std::io::Cursor::new(&buf)).unwrap(),
            3,
        );
        let mut empty = MultiBuffer::<f32>::new(Shape {
            channels: 2,
            frames: 0,
        });

        // Return a complete block as specified.
        assert_eq!(
            source.process(empty.as_multi_slice()).unwrap().into_raw(),
            [[1f32, 2., 5.], [3., 4., 8.]]
        );

        // Not enough for a block. Return remaining.
        assert_eq!(
            source.process(empty.as_multi_slice()).unwrap().into_raw(),
            [[6f32, 7.], [9., 10.]]
        );

        // Wav file exhausted, return empty slices.
        assert_eq!(
            source.process(empty.as_multi_slice()).unwrap().into_raw(),
            [[], []]
        );
        assert_eq!(
            source.process(empty.as_multi_slice()).unwrap().into_raw(),
            [[], []]
        );
    }

    struct ErrorReader<'a, T: std::io::Read> {
        trigger: &'a std::cell::Cell<bool>,
        inner: T,
    }

    impl<'a, T: std::io::Read> std::io::Read for ErrorReader<'a, T> {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            if self.trigger.get() {
                Err(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    "custom error for testing",
                ))
            } else {
                self.inner.read(buf)
            }
        }
    }

    #[test]
    #[should_panic(expected = "error reading wav file")]
    fn source_error() {
        let mut buf = Vec::new();

        // Prepare in-memory wave file
        let mut wav_writer = hound::WavWriter::new(
            std::io::Cursor::new(&mut buf),
            hound::WavSpec {
                channels: 2,
                sample_rate: 48000,
                bits_per_sample: 32,
                sample_format: hound::SampleFormat::Float,
            },
        )
        .unwrap();
        wav_writer.write_sample(0.).unwrap();
        wav_writer.write_sample(0.).unwrap();
        drop(wav_writer); // flush the samples

        let trigger = std::cell::Cell::new(false);
        let mut reader = ErrorReader {
            trigger: &trigger,
            inner: std::io::Cursor::new(&buf),
        };
        let mut source = WavSource::new(hound::WavReader::new(&mut reader).unwrap(), 3);

        let mut empty = MultiBuffer::<f32>::new(Shape {
            channels: 2,
            frames: 0,
        });
        trigger.set(true);
        source.process(empty.as_multi_slice()).unwrap();
    }

    struct ErrorWriter<'a, T: std::io::Write + std::io::Seek> {
        trigger: &'a std::cell::Cell<bool>,
        inner: T,
    }

    impl<'a, T: std::io::Write + std::io::Seek> std::io::Write for ErrorWriter<'a, T> {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            if self.trigger.get() {
                Err(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    "custom error for testing",
                ))
            } else {
                self.inner.write(buf)
            }
        }

        fn flush(&mut self) -> std::io::Result<()> {
            self.inner.flush()
        }
    }

    impl<'a, T: std::io::Write + std::io::Seek> std::io::Seek for ErrorWriter<'a, T> {
        fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
            self.inner.seek(pos)
        }
    }

    #[test]
    fn sink_error() {
        let trigger = std::cell::Cell::new(false);
        let mut sink = WavSink::new(
            hound::WavWriter::new(
                ErrorWriter {
                    trigger: &trigger,
                    inner: std::io::Cursor::new(Vec::new()),
                },
                hound::WavSpec {
                    channels: 2,
                    sample_rate: 48000,
                    bits_per_sample: 32,
                    sample_format: hound::SampleFormat::Float,
                },
            )
            .unwrap(),
        );

        let mut data = MultiBuffer::from(vec![vec![1f32], vec![2.]]);
        trigger.set(true);

        let result = sink.process(data.as_multi_slice());
        assert!(matches!(result.unwrap_err(), Error::Wav(_)));
    }
}
