// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::IoSlice;
use std::ops::ControlFlow;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use std::os::fd::OwnedFd;

use anyhow::bail;
use anyhow::Context;
use zerocopy::AsBytes;

use super::messages::send;
use super::messages::send_str;
use super::messages::ResponseOp;
use crate::processors::peer::messages::recv;
use crate::processors::peer::messages::Init;
use crate::processors::peer::messages::RequestOp;
use crate::processors::peer::messages::INIT_MAX_SIZE;
use crate::AudioProcessor;
use crate::Format;
use crate::MultiBuffer;
use crate::MultiSlice;
use crate::ProcessorVec;

pub(super) struct Worker<'a> {
    fd: BorrowedFd<'a>,
    pipeline: ProcessorVec,
    input_format: Format,
    output_format: Format,
    input_buffer: MultiBuffer<f32>,
}

enum Response<'a> {
    AudioOutput(MultiSlice<'a, f32>),
    Stop,
}

impl<'a> Worker<'a> {
    fn new(fd: BorrowedFd<'a>) -> anyhow::Result<Self> {
        let mut buf = vec![0u8; INIT_MAX_SIZE];
        let (request_op, payload_len) = recv::<RequestOp>(fd.as_fd(), &mut buf).context("init")?;
        if request_op != RequestOp::Init {
            bail!("unexpected request op {request_op:?} during init");
        }
        let config: Init =
            serde_json::from_slice(&buf[..payload_len]).context("serde_json::from_slice")?;

        let pipeline = crate::config::PipelineBuilder::new(config.input_format)
            .build(config.config)
            .context("build_pipeline")?;
        let output_format = pipeline.get_output_format();

        send_str(
            fd,
            ResponseOp::Ok,
            &serde_json::to_string(&output_format)
                .context("serde_json::to_string(output_format)")?,
        )
        .context("send response")?;

        Ok(Self {
            fd,
            pipeline,
            input_format: config.input_format,
            output_format,
            input_buffer: MultiBuffer::new(config.input_format.into()),
        })
    }

    fn process_one_command<'b, 'c>(
        fd: BorrowedFd<'b>,
        pipeline: &'c mut ProcessorVec,
        input_buffer: &'c mut MultiBuffer<f32>,
    ) -> anyhow::Result<Response<'c>> {
        let (request_op, payload_len) =
            recv::<RequestOp>(fd.as_fd(), input_buffer.as_bytes_mut()).context("recv")?;

        match request_op {
            RequestOp::Init => bail!("unexpected request op {request_op:?} after init"),
            RequestOp::Process => {
                if payload_len != input_buffer.as_bytes().len() {
                    bail!("unexpected audio payload len {payload_len}");
                }
                let output = pipeline
                    .process(input_buffer.as_multi_slice())
                    .context("pipeline.process")?;

                Ok(Response::AudioOutput(output))
            }
            RequestOp::Stop => Ok(Response::Stop),
        }
    }

    fn handle_one_command(&mut self) -> anyhow::Result<ControlFlow<()>> {
        match Worker::process_one_command(
            self.fd.as_fd(),
            &mut self.pipeline,
            &mut self.input_buffer,
        ) {
            Ok(response) => match response {
                Response::AudioOutput(multi_slice) => {
                    send(
                        self.fd.as_fd(),
                        ResponseOp::Ok,
                        multi_slice
                            .iter()
                            .map(|slice| IoSlice::new(slice.as_bytes())),
                    )?;
                    Ok(ControlFlow::Continue(()))
                }
                Response::Stop => {
                    send(self.fd.as_fd(), ResponseOp::Ok, std::iter::empty())?;
                    Ok(ControlFlow::Break(()))
                }
            },
            Err(err) => {
                send_error_response(self.fd.as_fd(), err)?;
                Ok(ControlFlow::Break(()))
            }
        }
    }

    fn run_result(fd: OwnedFd) -> anyhow::Result<()> {
        let mut worker = match Worker::new(fd.as_fd()).context("new") {
            Ok(worker) => worker,
            Err(err) => {
                send_error_response(fd.as_fd(), err)?;
                return Ok(());
            }
        };
        loop {
            match worker.handle_one_command().context("handle_one_command")? {
                ControlFlow::Continue(()) => continue,
                ControlFlow::Break(()) => break,
            }
        }
        Ok(())
    }

    pub(super) fn run(fd: OwnedFd) {
        if let Err(err) = Worker::run_result(fd) {
            log::error!("{err:#}");
        }
    }
}

fn send_error_response(fd: BorrowedFd, err: anyhow::Error) -> anyhow::Result<()> {
    log::error!("error: {err:#}");
    send(
        fd,
        ResponseOp::Error,
        std::iter::once(IoSlice::new(format!("{err:#}").as_ref())),
    )
    .context("error sending error response")
}

#[cfg(test)]
mod tests {
    use std::io::IoSlice;
    use std::os::fd::AsFd;

    use zerocopy::AsBytes;

    use super::Worker;
    use crate::config;
    use crate::processors::peer::messages::create_socketpair;
    use crate::processors::peer::messages::recv_slice;
    use crate::processors::peer::messages::send;
    use crate::processors::peer::messages::send_str;
    use crate::processors::peer::messages::Init;
    use crate::processors::peer::messages::RequestOp;
    use crate::processors::peer::messages::ResponseOp;
    use crate::Format;
    use crate::MultiBuffer;

    fn assert_payload_contains(payload: &[u8], expected: &str) {
        assert!(
            String::from_utf8_lossy(payload).contains(expected),
            "{:?} does not contain {expected:?}",
            String::from_utf8_lossy(payload)
        );
    }

    #[test]
    fn init_fail_invalid_config() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let worker_thread = std::thread::spawn(move || {
            Worker::run_result(worker_fd).unwrap();
        });
        send(host_fd.as_fd(), RequestOp::Init, std::iter::empty()).unwrap();

        let mut buf = vec![0u8; 1024];
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();

        assert_eq!(response_op, ResponseOp::Error);
        assert_payload_contains(payload, "new: serde_json");

        worker_thread.join().unwrap();
    }

    #[test]
    fn init_fail_bad_op() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let worker_thread = std::thread::spawn(move || {
            Worker::run_result(worker_fd).unwrap();
        });
        send(host_fd.as_fd(), RequestOp::Process, std::iter::empty()).unwrap();

        let mut buf = vec![0u8; 1024];
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();

        assert_eq!(response_op, ResponseOp::Error);
        assert_payload_contains(payload, "new: unexpected request op");

        worker_thread.join().unwrap();
    }

    #[test]
    fn init_resample() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let worker_thread = std::thread::spawn(move || {
            Worker::run_result(worker_fd).unwrap();
        });
        let config = Init {
            input_format: Format {
                channels: 2,
                block_size: 3,
                frame_rate: 24000,
            },
            config: config::Processor::Resample {
                output_frame_rate: 48000,
            },
        };
        send(
            host_fd.as_fd(),
            RequestOp::Init,
            std::iter::once(IoSlice::new(
                serde_json::ser::to_string(&config).unwrap().as_ref(),
            )),
        )
        .unwrap();

        let mut buf = vec![0u8; 1024];
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();

        assert_eq!(response_op, ResponseOp::Ok);
        let output_format: Format = serde_json::de::from_slice(payload).unwrap();
        assert_eq!(
            output_format,
            Format {
                frame_rate: 48000,
                block_size: 2 * 3,
                channels: 2
            }
        );

        send_str(host_fd.as_fd(), RequestOp::Stop, "").unwrap();
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();
        assert_eq!(
            response_op,
            ResponseOp::Ok,
            "{:?}",
            String::from_utf8_lossy(payload)
        );

        worker_thread.join().unwrap();
    }

    #[test]
    fn unexpected_double_init() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let worker_thread = std::thread::spawn(move || {
            Worker::run_result(worker_fd).unwrap();
        });
        let config = Init {
            input_format: Format {
                channels: 2,
                block_size: 3,
                frame_rate: 24000,
            },
            config: config::Processor::Negate,
        };
        send(
            host_fd.as_fd(),
            RequestOp::Init,
            std::iter::once(IoSlice::new(
                serde_json::ser::to_string(&config).unwrap().as_ref(),
            )),
        )
        .unwrap();

        let mut buf = vec![0u8; 1024];
        let (response_op, _payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();
        assert_eq!(response_op, ResponseOp::Ok);

        send_str(host_fd.as_fd(), RequestOp::Init, "").unwrap();
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();
        assert_eq!(response_op, ResponseOp::Error);
        assert_payload_contains(payload, "unexpected request op Init after init");

        worker_thread.join().unwrap();
    }

    #[test]
    fn process_negate_once() {
        let (host_fd, worker_fd) = create_socketpair().unwrap();
        let worker_thread = std::thread::spawn(move || {
            Worker::run_result(worker_fd).unwrap();
        });
        let config = Init {
            input_format: Format {
                channels: 2,
                block_size: 3,
                frame_rate: 24000,
            },
            config: config::Processor::Negate,
        };
        send(
            host_fd.as_fd(),
            RequestOp::Init,
            std::iter::once(IoSlice::new(
                serde_json::ser::to_string(&config).unwrap().as_ref(),
            )),
        )
        .unwrap();

        let mut buf = vec![0u8; 1024];
        let (response_op, payload) = recv_slice::<ResponseOp>(host_fd.as_fd(), &mut buf).unwrap();

        assert_eq!(response_op, ResponseOp::Ok);
        let output_format: Format = serde_json::de::from_slice(payload).unwrap();
        assert_eq!(
            output_format,
            Format {
                frame_rate: 24000,
                block_size: 3,
                channels: 2
            }
        );

        let mut audio_buf = MultiBuffer::from(vec![vec![1f32, 2., 3.], vec![4., 5., 6.]]);
        send(
            host_fd.as_fd(),
            RequestOp::Process,
            std::iter::once(IoSlice::new(audio_buf.as_bytes())),
        )
        .unwrap();
        let (response_op, payload) =
            recv_slice::<ResponseOp>(host_fd.as_fd(), audio_buf.as_bytes_mut()).unwrap();
        assert_eq!(
            response_op,
            ResponseOp::Ok,
            "{:?}",
            String::from_utf8_lossy(payload)
        );
        assert_eq!(
            audio_buf.to_vecs(),
            vec![vec![-1., -2., -3.], vec![-4., -5., -6.]]
        );

        send_str(host_fd.as_fd(), RequestOp::Stop, "").unwrap();

        worker_thread.join().unwrap();
    }
}
