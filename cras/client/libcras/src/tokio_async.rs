// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of the audio_streams async interface with tokio
//!
//! libcras is actually compiled in two ways:
//! - As part of the crosvm workspace, replacing the stub version of libcras.
//!   This uses the cros_async implementation of the async interface.
//! - For the rest of ChromeOS. This uses the tokio implementation in this file.

use async_trait::async_trait;
use audio_streams::async_api::ReadAsync;
use audio_streams::async_api::ReadWriteAsync;
use audio_streams::async_api::WriteAsync;
use futures::Future;

use std::io;
use std::io::Result as IoResult;
use std::os::unix::io::RawFd;
use std::os::unix::net::UnixStream;
use std::time::Duration;

use audio_streams::{AsyncStream, AudioStreamsExecutor};

fn set_nonblocking(fd: RawFd, nonblocking: bool) -> io::Result<()> {
    let mut nonblocking = nonblocking as libc::c_int;
    // Safe because the return value is checked, and this ioctl call sets the nonblocking mode
    // and does not continue holding the file descriptor after the call.
    let ret = unsafe { libc::ioctl(fd, libc::FIONBIO, &mut nonblocking) };
    if ret < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

/// This executor is only used to implement the corresponding sync version of AudioSocket
/// using the async_::AudioSocket.
pub struct TokioExecutor {
    runtime: tokio::runtime::Runtime,
}

impl TokioExecutor {
    /// Creates a Tokio implementation of the AudioStreamsExecutor using a "current thread"
    /// tokio runtime.
    pub fn new() -> IoResult<TokioExecutor> {
        Ok(TokioExecutor {
            runtime: tokio::runtime::Builder::new_current_thread()
                .enable_io()
                .enable_time()
                .build()?,
        })
    }

    /// Run the executor and block the current thread until the future is completed.
    pub fn run_until<F: Future>(&self, future: F) -> F::Output {
        self.runtime.block_on(future)
    }
}

#[async_trait(?Send)]
impl AudioStreamsExecutor for TokioExecutor {
    #[cfg(unix)]
    fn async_unix_stream(&self, f: UnixStream) -> IoResult<AsyncStream> {
        f.set_nonblocking(true)?;
        let _guard = self.runtime.enter();
        Ok(Box::new(TokioAsyncStream {
            stream: tokio::net::UnixStream::from_std(f)?,
        }))
    }

    /// Returns a future that resolves after the specified time.
    async fn delay(&self, dur: Duration) -> IoResult<()> {
        tokio::time::sleep(dur).await;
        Ok(())
    }

    // Returns a future that resolves after the provided descriptor is readable.
    #[cfg(unix)]
    async fn wait_fd_readable(&self, fd: RawFd) -> IoResult<()> {
        set_nonblocking(fd, true)?;
        let _guard = self.runtime.enter();
        let async_fd = tokio::io::unix::AsyncFd::new(fd)?;
        async_fd.readable().await?.retain_ready();
        Ok(())
    }
}

struct TokioAsyncStream {
    stream: tokio::net::UnixStream,
}

#[async_trait(?Send)]
impl ReadAsync for TokioAsyncStream {
    async fn read_to_vec<'a>(
        &'a self,
        file_offset: Option<u64>,
        mut vec: Vec<u8>,
    ) -> IoResult<(usize, Vec<u8>)> {
        // Tokio async API does not support file offsets. These are not used by libcras
        // and should be dropped from the audio_streams::Read/WriteAsync APIs.
        assert!(file_offset.is_none());

        loop {
            self.stream.readable().await?;

            // Try to read data, this may still fail with `WouldBlock`
            // if the readiness event is a false positive.
            match self.stream.try_read(vec.as_mut_slice()) {
                Ok(n) => return Ok((n, vec)),
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    continue;
                }
                Err(e) => {
                    return Err(e);
                }
            }
        }
    }
}

#[async_trait(?Send)]
impl WriteAsync for TokioAsyncStream {
    async fn write_from_vec<'a>(
        &'a self,
        file_offset: Option<u64>,
        vec: Vec<u8>,
    ) -> IoResult<(usize, Vec<u8>)> {
        // Tokio async API does not support file offsets. These are not used by libcras
        // and should be dropped from the audio_streams::Read/WriteAsync APIs.
        assert!(file_offset.is_none());

        loop {
            self.stream.writable().await?;

            // Try to write data, this may still fail with `WouldBlock`
            // if the readiness event is a false positive.
            match self.stream.try_write(&vec) {
                Ok(n) => return Ok((n, vec)),
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    continue;
                }
                Err(e) => {
                    return Err(e);
                }
            }
        }
    }
}

impl ReadWriteAsync for TokioAsyncStream {}
