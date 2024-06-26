// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::fd::OwnedFd;
use std::thread::JoinHandle;

use anyhow::Context;

use super::create_socketpair;
use super::BlockingSeqPacketProcessor;
use super::Worker;
use crate::config::Processor;
use crate::AudioProcessor;
use crate::Format;

/// WorkerHandle holds the resources used by a PeerProcessor worker.
pub trait WorkerHandle {}

/// A factory for creating PeerProcessor workers, represented by `WorkerHandle`s.
pub trait WorkerFactory {
    fn create(&self, worker_fd: OwnedFd) -> anyhow::Result<Box<dyn WorkerHandle>>;
}

/// Like BlockingSeqPacketProcessor but also manages the worker's lifetime.
pub struct ManagedBlockingSeqPacketProcessor {
    proxy: BlockingSeqPacketProcessor,
    // Held to manage the the worker's lifetime.
    _worker_handle: Box<dyn WorkerHandle>,
}

impl ManagedBlockingSeqPacketProcessor {
    pub fn new(
        worker_factory: impl WorkerFactory,
        input_format: Format,
        config: Processor,
    ) -> anyhow::Result<Self> {
        let (host_fd, worker_fd) = create_socketpair().context("create_socketpair")?;
        let worker_handle = worker_factory
            .create(worker_fd)
            .context("worker_factory.create")?;
        let proxy = BlockingSeqPacketProcessor::new(host_fd, input_format, config)
            .context("BlockingSeqPacketProcessor::new")?;
        Ok(Self {
            proxy,
            _worker_handle: worker_handle,
        })
    }
}

impl AudioProcessor for ManagedBlockingSeqPacketProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        self.proxy.process(input)
    }

    fn get_output_format(&self) -> Format {
        self.proxy.get_output_format()
    }
}

pub struct ThreadedWorkerFactory;

impl WorkerFactory for ThreadedWorkerFactory {
    fn create(&self, worker_fd: OwnedFd) -> anyhow::Result<Box<dyn WorkerHandle>> {
        Ok(Box::new(ThreadedWorkerHandle {
            join_handle: Some(std::thread::spawn(|| {
                Worker::run(worker_fd);
            })),
        }))
    }
}

struct ThreadedWorkerHandle {
    join_handle: Option<JoinHandle<()>>,
}

impl WorkerHandle for ThreadedWorkerHandle {}

impl Drop for ThreadedWorkerHandle {
    fn drop(&mut self) {
        let _ = self.join_handle.take().unwrap().join();
    }
}

#[cfg(test)]
mod tests {
    use super::ManagedBlockingSeqPacketProcessor;
    use super::ThreadedWorkerFactory;
    use crate::config;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn process_negate() {
        let mut processor = ManagedBlockingSeqPacketProcessor::new(
            ThreadedWorkerFactory,
            Format {
                channels: 2,
                block_size: 4,
                frame_rate: 48000,
            },
            config::Processor::Negate,
        )
        .unwrap();
        let mut input = MultiBuffer::from(vec![vec![1f32, 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let output = processor.process(input.as_multi_slice()).unwrap();
        assert_eq!(
            output.into_raw(),
            [[-1f32, -2., -3., -4.], [-5., -6., -7., -8.]]
        );
        assert_eq!(
            processor.get_output_format(),
            Format {
                channels: 2,
                block_size: 4,
                frame_rate: 48000,
            }
        );
    }
}
