// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::Sender;

use anyhow::Context;

use crate::util::set_thread_priority;
use crate::AudioProcessor;
use crate::Format;
use crate::MultiBuffer;
use crate::Result;
use crate::Shape;

pub struct ThreadedProcessor<T: AudioProcessor> {
    phantom: PhantomData<T>,
    join_handle: Option<std::thread::JoinHandle<()>>,
    intx: Option<Sender<MultiBuffer<T::I>>>,
    outrx: Receiver<Result<MultiBuffer<T::O>>>,
    outbuf: MultiBuffer<T::O>,
    output_format: Format,
}

impl<T: AudioProcessor + Send + 'static> ThreadedProcessor<T> {
    pub fn new(mut inner: T, delay_blocks: usize) -> Self {
        let output_format = inner.get_output_format();
        let (intx, inrx) = std::sync::mpsc::channel::<MultiBuffer<T::I>>();
        let (outtx, outrx) = std::sync::mpsc::channel();

        // Populate delay_blocks.
        for _ in 0..delay_blocks {
            outtx
                .send(Ok(MultiBuffer::new(output_format.into())))
                .unwrap();
        }

        let builder = std::thread::Builder::new().name("ThreadedProcessor".into());
        let join_handle = builder
            .spawn(move || {
                // TODO(aaronyu): Figure out a better location to do this.
                if let Err(err) = set_thread_priority() {
                    log::error!("set_thread_priority: {err:#}");
                }

                for mut buf in inrx.iter() {
                    match inner.process(buf.as_multi_slice()) {
                        Err(err) => outtx.send(Err(err)).unwrap(),
                        Ok(slice) => outtx.send(Ok(MultiBuffer::from(slice))).unwrap(),
                    }
                }
            })
            .expect("cannot spawn ThreadedProcessor thread");
        ThreadedProcessor {
            phantom: PhantomData,
            join_handle: Some(join_handle),
            intx: Some(intx),
            outrx,
            outbuf: MultiBuffer::new(Shape {
                channels: 0,
                frames: 0,
            }),
            output_format,
        }
    }
}

impl<T: AudioProcessor> Drop for ThreadedProcessor<T> {
    fn drop(&mut self) {
        // Close the sender by dropping.
        self.intx = None;

        _ = self.join_handle.take().unwrap().join();
    }
}

impl<T: AudioProcessor> AudioProcessor for ThreadedProcessor<T> {
    type I = T::I;
    type O = T::O;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        self.intx
            .as_ref()
            .expect("intx")
            .send(MultiBuffer::from(input))
            .expect("intx.send");
        match self.outrx.recv().context("outrx.recv")? {
            Ok(buf) => {
                self.outbuf = buf;
                Ok(self.outbuf.as_multi_slice())
            }
            Err(err) => Err(err),
        }
    }

    fn get_output_format(&self) -> Format {
        self.output_format
    }
}

#[cfg(test)]
mod tests {
    use crate::processors::NegateAudioProcessor;
    use crate::processors::ThreadedProcessor;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn process() {
        let mut input1: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut input2: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![11., 22., 33., 44.], vec![55., 66., 77., 88.]]);
        let mut ap = ThreadedProcessor::new(
            NegateAudioProcessor::new(Format {
                channels: 2,
                block_size: 4,
                frame_rate: 48000,
            }),
            1,
        );

        let output1 = ap.process(input1.as_multi_slice()).unwrap();

        // First output should be empty due to delayed.
        assert_eq!(output1.into_raw(), [[0.; 4]; 2]);

        let output2 = ap.process(input2.as_multi_slice()).unwrap();
        // output2 = -input1.
        assert_eq!(
            output2.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );
    }
}
