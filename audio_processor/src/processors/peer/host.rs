// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::IoSlice;
use std::os::fd::AsFd;
use std::os::fd::OwnedFd;

use anyhow::anyhow;
use anyhow::bail;
use anyhow::Context;
use zerocopy::AsBytes;

use super::messages::multi_slice_from_buf;
use super::messages::new_payload_buffer;
use super::messages::recv;
use super::messages::recv_slice;
use super::messages::send;
use super::messages::send_str;
use super::messages::Init;
use super::messages::RequestOp;
use super::messages::ResponseOp;
use super::messages::CONTROL_MSG_MAX_SIZE;
use crate::config;
use crate::AudioProcessor;
use crate::Format;
use crate::MultiSlice;

pub struct BlockingSeqPacketProcessor {
    fd: OwnedFd,
    output_format: Format,
    output_buffer: Vec<f32>,
}

impl BlockingSeqPacketProcessor {
    pub fn new(
        fd: OwnedFd,
        input_format: Format,
        config: config::Processor,
    ) -> anyhow::Result<Self> {
        send_str(
            fd.as_fd(),
            RequestOp::Init,
            &serde_json::to_string(&Init {
                input_format,
                config,
            })
            .context("serialize init message")?,
        )
        .context("send init message")?;
        let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
        let (response_op, payload) =
            recv_slice::<ResponseOp>(fd.as_fd(), &mut buf).context("recv init message reply")?;
        match response_op {
            ResponseOp::Ok => {
                let output_format: Format =
                    serde_json::from_slice(payload).context("deserialize output format")?;
                Ok(Self {
                    fd,
                    output_format,
                    output_buffer: new_payload_buffer(output_format),
                })
            }
            ResponseOp::Error => bail!("error from peer: {}", String::from_utf8_lossy(payload)),
        }
    }
}

impl AudioProcessor for BlockingSeqPacketProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: MultiSlice<'a, Self::I>,
    ) -> crate::Result<MultiSlice<'a, Self::O>> {
        send(
            self.fd.as_fd(),
            RequestOp::Process,
            input.iter().map(|ch| IoSlice::new(ch.as_bytes())),
        )?;

        let buf = self.output_buffer.as_bytes_mut();
        let (response_op, payload_len) = recv::<ResponseOp>(self.fd.as_fd(), buf)?;

        match response_op {
            ResponseOp::Ok => Ok(multi_slice_from_buf(
                &mut self.output_buffer,
                payload_len,
                self.output_format.channels,
            )?),
            ResponseOp::Error => Err(anyhow!(
                "error from peer: {}",
                String::from_utf8_lossy(&self.output_buffer.as_bytes()[..payload_len])
            )
            .into()),
        }
    }

    fn get_output_format(&self) -> Format {
        self.output_format
    }
}

impl Drop for BlockingSeqPacketProcessor {
    fn drop(&mut self) {
        if let Err(e) = send(self.fd.as_fd(), RequestOp::Stop, std::iter::empty()) {
            log::error!("failed to send stop message: {e}");
        }
    }
}

#[cfg(test)]
mod tests {
    use std::io::IoSlice;
    use std::os::fd::AsFd;

    use zerocopy::AsBytes;

    use super::BlockingSeqPacketProcessor;
    use crate::config;
    use crate::processors::peer::messages::create_socketpair;
    use crate::processors::peer::messages::recv;
    use crate::processors::peer::messages::recv_slice;
    use crate::processors::peer::messages::send;
    use crate::processors::peer::messages::send_str;
    use crate::processors::peer::messages::RequestOp;
    use crate::processors::peer::messages::ResponseOp;
    use crate::processors::peer::messages::CONTROL_MSG_MAX_SIZE;
    use crate::processors::peer::worker;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn new_output_format() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let output_format = Format {
            block_size: 4096,
            channels: 2,
            frame_rate: 12345,
        };
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
                let (op, _) = recv_slice::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                assert_eq!(op, RequestOp::Init);

                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    &serde_json::to_string(&output_format).unwrap(),
                )
                .unwrap();
            });
            s.spawn(|| {
                let processor = BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        block_size: 1,
                        channels: 1,
                        frame_rate: 1,
                    },
                    config::Processor::Negate,
                )
                .unwrap();
                assert_eq!(processor.get_output_format(), output_format);
            });
        });
    }

    #[test]
    fn new_error() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
                let (op, _) = recv_slice::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                assert_eq!(op, RequestOp::Init);

                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Error,
                    "something went wrong :(",
                )
                .unwrap();
            });
            s.spawn(|| {
                let err = match BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        block_size: 1,
                        channels: 1,
                        frame_rate: 1,
                    },
                    config::Processor::Negate,
                ) {
                    Ok(_) => panic!("should fail"),
                    Err(err) => err,
                };
                assert_eq!(err.to_string(), "error from peer: something went wrong :(");
            });
        });
    }

    #[test]
    fn process_error() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let output_format = Format {
            block_size: 4096,
            channels: 2,
            frame_rate: 12345,
        };
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
                let (op, _) = recv_slice::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                assert_eq!(op, RequestOp::Init);

                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    &serde_json::to_string(&output_format).unwrap(),
                )
                .unwrap();

                recv::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Error,
                    "something went wrong :(",
                )
                .unwrap();
            });
            s.spawn(|| {
                let mut processor = BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        block_size: 1,
                        channels: 1,
                        frame_rate: 1,
                    },
                    config::Processor::Negate,
                )
                .unwrap();

                let mut input = MultiBuffer::from(vec![vec![1f32]]);
                let err = processor.process(input.as_multi_slice()).unwrap_err();
                assert!(
                    err.to_string()
                        .contains("error from peer: something went wrong :("),
                    "{err}"
                );
            });
        });
    }

    #[test]
    fn process_variable_output_block_size() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let output_format = Format {
            block_size: 4096,
            channels: 2,
            frame_rate: 12345,
        };
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
                let (op, _) = recv_slice::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                assert_eq!(op, RequestOp::Init);

                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    &serde_json::to_string(&output_format).unwrap(),
                )
                .unwrap();

                recv::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                send(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    std::iter::once(IoSlice::new([1f32, 2.].as_bytes())),
                )
                .unwrap();
            });
            s.spawn(|| {
                let mut processor = BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        block_size: 1,
                        channels: 1,
                        frame_rate: 1,
                    },
                    config::Processor::Negate,
                )
                .unwrap();

                let mut input = MultiBuffer::from(vec![vec![1f32]]);
                let output = processor.process(input.as_multi_slice()).unwrap();

                assert_eq!(output.into_raw(), [[1f32], [2.]]);
            });
        });
    }

    #[test]
    fn process_output_block_size_too_large() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let output_format = Format {
            block_size: 1,
            channels: 1,
            frame_rate: 12345,
        };
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut buf = [0u8; CONTROL_MSG_MAX_SIZE];
                let (op, _) = recv_slice::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                assert_eq!(op, RequestOp::Init);

                send_str(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    &serde_json::to_string(&output_format).unwrap(),
                )
                .unwrap();

                recv::<RequestOp>(worker_fd.as_fd(), &mut buf).unwrap();
                send(
                    worker_fd.as_fd(),
                    ResponseOp::Ok,
                    std::iter::once(IoSlice::new(&vec![0; 100000])),
                )
                .unwrap();
            });
            s.spawn(|| {
                let mut processor = BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        block_size: 1,
                        channels: 1,
                        frame_rate: 1,
                    },
                    config::Processor::Negate,
                )
                .unwrap();

                let mut input = MultiBuffer::from(vec![vec![1f32]]);
                let err = processor.process(input.as_multi_slice()).unwrap_err();
                assert!(
                    format!("{err:#}").contains("unrecoverable error: message too long"),
                    "{err:#}"
                );
            });
        });
    }

    #[test]
    fn process_negate() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        std::thread::scope(|s| {
            s.spawn(|| {
                worker::Worker::run(worker_fd);
            });
            s.spawn(|| {
                let mut processor = BlockingSeqPacketProcessor::new(
                    host_fd,
                    Format {
                        channels: 2,
                        block_size: 4,
                        frame_rate: 48000,
                    },
                    config::Processor::Negate,
                )
                .unwrap();
                let mut input =
                    MultiBuffer::from(vec![vec![1f32, 2., 3., 4.], vec![5., 6., 7., 8.]]);
                let output = processor.process(input.as_multi_slice()).unwrap();
                assert_eq!(
                    output.into_raw(),
                    [[-1f32, -2., -3., -4.], [-5., -6., -7., -8.]]
                );
            });
        });
    }
}
