// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::Sender;

use anyhow::ensure;
use anyhow::Context;
use nix::sys::resource::setrlimit;
use nix::sys::resource::Resource;

use crate::AudioProcessor;
use crate::MultiBuffer;
use crate::Result;
use crate::Shape;

// TODO(b/268271100): Call the C version when we can build C code before Rust.
fn set_thread_priority() -> anyhow::Result<()> {
    // CRAS_SERVER_RT_THREAD_PRIORITY 12
    let p = 12;
    setrlimit(Resource::RLIMIT_RTPRIO, p, p).context("setrlimit")?;

    // SAFETY: sched_param is properly initialized.
    unsafe {
        let sched_param = libc::sched_param {
            sched_priority: p as i32,
        };
        let rc = libc::pthread_setschedparam(libc::pthread_self(), libc::SCHED_RR, &sched_param);
        ensure!(rc == 0, "pthread_setschedparam returned {rc}");
    }

    Ok(())
}

pub struct ThreadedProcessor<T: AudioProcessor> {
    phantom: PhantomData<T>,
    join_handle: Option<std::thread::JoinHandle<()>>,
    intx: Option<Sender<MultiBuffer<T::I>>>,
    outrx: Receiver<Result<MultiBuffer<T::O>>>,
    outbuf: MultiBuffer<T::O>,
    output_frame_rate: usize,
}

impl<T: AudioProcessor + Send + 'static> ThreadedProcessor<T> {
    pub fn new(mut inner: T, output_shape: Shape, delay_blocks: usize) -> Self {
        let output_frame_rate = inner.get_output_frame_rate();
        let (intx, inrx) = std::sync::mpsc::channel::<MultiBuffer<T::I>>();
        let (outtx, outrx) = std::sync::mpsc::channel();

        // Populate delay_blocks.
        for _ in 0..delay_blocks {
            outtx.send(Ok(MultiBuffer::new(output_shape))).unwrap();
        }

        let join_handle = std::thread::spawn(move || {
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
        });
        ThreadedProcessor {
            phantom: PhantomData,
            join_handle: Some(join_handle),
            intx: Some(intx),
            outrx,
            outbuf: MultiBuffer::new(Shape {
                channels: 0,
                frames: 0,
            }),
            output_frame_rate,
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

    fn get_output_frame_rate<'a>(&'a self) -> usize {
        self.output_frame_rate
    }
}

#[cfg(test)]
mod tests {
    use crate::processors::NegateAudioProcessor;
    use crate::processors::ThreadedProcessor;
    use crate::AudioProcessor;
    use crate::MultiBuffer;
    use crate::Shape;

    #[test]
    fn process() {
        let mut input1: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut input2: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![11., 22., 33., 44.], vec![55., 66., 77., 88.]]);
        let mut ap = ThreadedProcessor::new(
            NegateAudioProcessor::new(2, 4, 16000),
            Shape {
                channels: 1,
                frames: 4,
            },
            1,
        );

        let output1 = ap.process(input1.as_multi_slice()).unwrap();

        // First output should be empty due to delayed.
        assert_eq!(output1.into_raw(), [[0.; 4]]);

        let output2 = ap.process(input2.as_multi_slice()).unwrap();
        // output2 = -input1.
        assert_eq!(
            output2.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );
    }
}
