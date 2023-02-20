// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use async_trait::async_trait;
use std::cmp::min;
use std::io;
use std::marker::PhantomData;
use std::mem;
use std::os::unix::net::UnixStream;
use std::time::Duration;

use audio_streams::{
    capture::{AsyncCaptureBuffer, AsyncCaptureBufferStream},
    AsyncBufferCommit, AsyncPlaybackBuffer, AsyncPlaybackBufferStream, AsyncStream,
    AudioStreamsExecutor, BoxError,
};
use cras_sys::gen::{
    audio_message, snd_pcm_format_t, CRAS_AUDIO_MESSAGE_ID, CRAS_STREAM_DIRECTION,
};
use data_model::DataInit;
use libchromeos::sys::error;

use crate::audio_socket::AudioMessage;
use crate::cras_server_socket::CrasServerSocket;
use crate::cras_shm::*;
use crate::cras_stream::Error;

pub struct AudioSocket {
    socket: AsyncStream,
}

impl AudioSocket {
    /// Creates `AudioSocket` from a `UnixStream`.
    ///
    /// # Arguments
    /// `socket` - A `UnixStream`.
    /// `ex` - An `Executor`.
    pub fn new(s: UnixStream, ex: &dyn AudioStreamsExecutor) -> io::Result<AudioSocket> {
        Ok(AudioSocket {
            socket: ex.async_unix_stream(s)?,
        })
    }

    /// Read `T` from socket asynchronously.
    ///
    /// # Returns
    /// `T`
    ///
    /// # Errors
    /// Returns io::Error if error occurs.
    pub async fn read_from_socket<T>(&self) -> io::Result<T>
    where
        T: Sized + DataInit + Default,
    {
        let mut message: T = Default::default();
        let buf = vec![0u8; mem::size_of::<T>()];
        let (count, buf) = self.socket.read_to_vec(None, buf).await?;
        if count == mem::size_of::<T>() {
            message.as_mut_slice().copy_from_slice(buf.as_slice());
            Ok(message)
        } else {
            Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "Read truncated data.",
            ))
        }
    }

    /// Read an `audio message` asynchronously.
    ///
    /// # Returns
    /// `AudioMessage` - AudioMessage enum.
    ///
    /// # Errors
    /// Returns io::Error if error occurs.
    pub async fn read_audio_message(&self) -> io::Result<AudioMessage> {
        let raw_msg: audio_message = self.read_from_socket().await?;
        Ok(AudioMessage::from(raw_msg))
    }

    /// Sends raw audio message with given AudioMessage enum.
    ///
    /// # Arguments
    /// * `msg` - enum AudioMessage, which could be `Success` with message id
    /// and frames or `Error` with error code.
    ///
    /// # Errors
    /// Returns error if `libc::write` fails.
    pub async fn send_audio_message(&self, msg: AudioMessage) -> io::Result<()> {
        let msg: audio_message = msg.into();
        let (bytes_written, _) = self
            .socket
            .write_from_vec(None, msg.as_slice().to_vec())
            .await?;
        if bytes_written < mem::size_of::<audio_message>() {
            Err(io::Error::new(
                io::ErrorKind::WriteZero,
                "Sent truncated data.",
            ))
        } else {
            Ok(())
        }
    }

    /// Sends the data ready message with written frame count asynchronously.
    ///
    /// # Arguments
    /// * `frames` - An `u32` indicating the written frame count.
    pub async fn data_ready(&self, frames: u32) -> io::Result<()> {
        self.send_audio_message(AudioMessage::Success {
            id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_READY,
            frames,
        })
        .await
    }

    /// Sends the capture ready message with read frame count asynchronously.
    ///
    /// # Arguments
    ///
    /// * `frames` - An `u32` indicating the number of read frames.
    pub async fn capture_ready(&self, frames: u32) -> io::Result<()> {
        self.send_audio_message(AudioMessage::Success {
            id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_CAPTURED,
            frames,
        })
        .await
    }
}

/// A trait controls the state of `CrasAudioHeader` and
/// interacts with server's audio thread through `AudioSocket`.
pub trait CrasStreamData<'a>: Send {
    // Creates `CrasStreamData` with only `AudioSocket`.
    fn new(audio_sock: AudioSocket, header: CrasAudioHeader<'a>, rate: u32) -> Self;
    fn header_mut(&mut self) -> &mut CrasAudioHeader<'a>;
    fn audio_sock_mut(&mut self) -> &mut AudioSocket;
}

/// `CrasStreamData` implementation for `PlaybackBufferStream`.
pub struct CrasPlaybackData<'a> {
    audio_sock: AudioSocket,
    header: CrasAudioHeader<'a>,
    rate: u32,
}

impl<'a> CrasStreamData<'a> for CrasPlaybackData<'a> {
    fn new(audio_sock: AudioSocket, header: CrasAudioHeader<'a>, rate: u32) -> Self {
        Self {
            audio_sock,
            header,
            rate,
        }
    }

    fn header_mut(&mut self) -> &mut CrasAudioHeader<'a> {
        &mut self.header
    }

    fn audio_sock_mut(&mut self) -> &mut AudioSocket {
        &mut self.audio_sock
    }
}

#[async_trait(?Send)]
impl<'a> AsyncBufferCommit for CrasPlaybackData<'a> {
    async fn commit(&mut self, nframes: usize) {
        let log_err = |e| error!("AsyncBufferCommit error: {}", e);
        if let Err(e) = self.header.commit_written_frames(nframes as u32) {
            log_err(e);
        }
        if let Err(e) = self.audio_sock.data_ready(nframes as u32).await {
            log_err(e);
        }
    }

    fn latency_bytes(&self) -> u32 {
        let mut ts = libc::timespec {
            tv_sec: 0,
            tv_nsec: 0,
        };
        // clock_gettime is safe when passed a valid address and a valid enum.
        let result = unsafe {
            libc::clock_gettime(libc::CLOCK_MONOTONIC_RAW, &mut ts as *mut libc::timespec)
        };

        if result != 0 {
            error!("clock_gettime() failed!");
            return 0;
        }
        let now = Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32);

        match self.header.get_timestamp().checked_sub(now) {
            None => 0,
            Some(diff) => {
                (diff.as_nanos() * self.header.get_frame_size() as u128 * self.rate as u128
                    / 1_000_000_000) as u32
            }
        }
    }
}

/// `CrasStreamData` implementation for `CaptureBufferStream`.
pub struct CrasCaptureData<'a> {
    audio_sock: AudioSocket,
    header: CrasAudioHeader<'a>,
    rate: u32,
}

impl<'a> CrasStreamData<'a> for CrasCaptureData<'a> {
    fn new(audio_sock: AudioSocket, header: CrasAudioHeader<'a>, rate: u32) -> Self {
        Self {
            audio_sock,
            header,
            rate,
        }
    }

    fn header_mut(&mut self) -> &mut CrasAudioHeader<'a> {
        &mut self.header
    }

    fn audio_sock_mut(&mut self) -> &mut AudioSocket {
        &mut self.audio_sock
    }
}

#[async_trait(?Send)]
impl<'a> AsyncBufferCommit for CrasCaptureData<'a> {
    async fn commit(&mut self, nframes: usize) {
        let log_err = |e| error!("AsyncBufferCommit error: {}", e);
        if let Err(e) = self.header.commit_read_frames(nframes as u32) {
            log_err(e);
        }
        if let Err(e) = self.audio_sock.capture_ready(nframes as u32).await {
            log_err(e);
        }
    }

    fn latency_bytes(&self) -> u32 {
        let mut ts = libc::timespec {
            tv_sec: 0,
            tv_nsec: 0,
        };
        // clock_gettime is safe when passed a valid address and a valid enum.
        let result = unsafe {
            libc::clock_gettime(libc::CLOCK_MONOTONIC_RAW, &mut ts as *mut libc::timespec)
        };

        if result != 0 {
            error!("clock_gettime() failed!");
            return 0;
        }
        let now = Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32);
        match now.checked_sub(self.header.get_timestamp()) {
            None => 0,
            Some(diff) => {
                (diff.as_nanos() * self.header.get_frame_size() as u128 * self.rate as u128
                    / 1_000_000_000) as u32
            }
        }
    }
}

#[allow(dead_code)]
pub struct CrasStream<'a, T: CrasStreamData<'a> + AsyncBufferCommit> {
    stream_id: u32,
    server_socket: CrasServerSocket,
    block_size: u32,
    direction: CRAS_STREAM_DIRECTION,
    rate: u32,
    num_channels: usize,
    format: snd_pcm_format_t,
    /// A structure for stream to interact with server audio thread.
    controls: T,
    /// The `PhantomData` is used by `controls: T`
    phantom: PhantomData<CrasAudioHeader<'a>>,
    audio_buffer: CrasAudioBuffer,
}

impl<'a, T: CrasStreamData<'a> + AsyncBufferCommit> CrasStream<'a, T> {
    /// Creates a CrasStream by given arguments.
    ///
    /// # Returns
    /// `CrasStream` - CRAS client stream.
    #[allow(clippy::too_many_arguments)]
    pub fn try_new(
        stream_id: u32,
        server_socket: CrasServerSocket,
        block_size: u32,
        direction: CRAS_STREAM_DIRECTION,
        rate: u32,
        num_channels: usize,
        format: snd_pcm_format_t,
        audio_sock: AudioSocket,
        header_fd: CrasAudioShmHeaderFd,
        samples_fd: CrasShmFd,
    ) -> Result<Self, Error> {
        let (header, audio_buffer) = create_header_and_buffers(header_fd, samples_fd)?;

        Ok(Self {
            stream_id,
            server_socket,
            block_size,
            direction,
            rate,
            num_channels,
            format,
            controls: T::new(audio_sock, header, rate),
            phantom: PhantomData,
            audio_buffer,
        })
    }

    async fn wait_request_data(&mut self) -> Result<(), Error> {
        match self.controls.audio_sock_mut().read_audio_message().await? {
            AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                ..
            } => Ok(()),
            _ => Err(Error::MessageTypeError),
        }
    }

    async fn wait_data_ready(&mut self) -> Result<u32, Error> {
        match self.controls.audio_sock_mut().read_audio_message().await? {
            AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_READY,
                frames,
            } => Ok(frames),
            _ => Err(Error::MessageTypeError),
        }
    }
}

impl<'a, T: CrasStreamData<'a> + AsyncBufferCommit> Drop for CrasStream<'a, T> {
    /// A blocking drop function, sends the disconnect message to `CrasClient` and waits for
    /// the return message.
    /// Logs an error message to stderr if the method fails.
    fn drop(&mut self) {
        if let Err(e) = self.server_socket.disconnect_stream(self.stream_id) {
            error!("CrasStream::Drop error: {}", e);
        }
    }
}

#[async_trait(?Send)]
impl<'a, T: CrasStreamData<'a> + AsyncBufferCommit> AsyncPlaybackBufferStream
    for CrasStream<'a, T>
{
    async fn next_playback_buffer<'b>(
        &'b mut self,
        _ex: &dyn AudioStreamsExecutor,
    ) -> Result<AsyncPlaybackBuffer<'b>, BoxError> {
        // Wait for request audio message
        self.wait_request_data().await?;
        let header = self.controls.header_mut();
        let frame_size = header.get_frame_size();
        let (offset, len) = header.get_write_offset_and_len()?;
        let buf = &mut self.audio_buffer.get_buffer()[offset..offset + len];

        AsyncPlaybackBuffer::new(frame_size, buf, &mut self.controls).map_err(Box::from)
    }
}

#[async_trait(?Send)]
impl<'a, T: CrasStreamData<'a> + AsyncBufferCommit> AsyncCaptureBufferStream for CrasStream<'a, T> {
    async fn next_capture_buffer<'b>(
        &'b mut self,
        _ex: &dyn AudioStreamsExecutor,
    ) -> Result<AsyncCaptureBuffer<'b>, BoxError> {
        loop {
            // Wait for data ready message
            let frames = self.wait_data_ready().await? as usize;
            let header = self.controls.header_mut();
            let shm_frames = header.get_readable_frames()?;

            // if shm readable frames is less than client requests, that means
            // overrun has happened in server side and we got partial corrupted
            // buffer. Drop the message and wait for the next one.
            if shm_frames < frames {
                continue;
            }

            let frame_size = header.get_frame_size();
            let len = min(shm_frames, frames) * frame_size;
            let offset = header.get_read_buffer_offset()?;
            let buf = &mut self.audio_buffer.get_buffer()[offset..offset + len];

            return AsyncCaptureBuffer::new(frame_size, buf, &mut self.controls).map_err(Box::from);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cros_async::Executor;

    fn init_audio_socket_pair(ex: &dyn AudioStreamsExecutor) -> (AudioSocket, AudioSocket) {
        let (sock1, sock2) = UnixStream::pair().unwrap();
        let sender = AudioSocket::new(sock1, ex).unwrap();
        let receiver = AudioSocket::new(sock2, ex).unwrap();
        (sender, receiver)
    }

    #[test]
    fn audio_socket_send_and_recv_audio_message() {
        async fn this_test(ex: &dyn AudioStreamsExecutor) {
            let (sender, receiver) = init_audio_socket_pair(ex);
            let message_succ = AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                frames: 0,
            };
            sender.send_audio_message(message_succ).await.unwrap();
            let res = receiver.read_audio_message().await.unwrap();
            assert_eq!(
                res,
                AudioMessage::Success {
                    id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                    frames: 0
                }
            );

            let message_err = AudioMessage::Error(123);
            sender.send_audio_message(message_err).await.unwrap();
            let res = receiver.read_audio_message().await.unwrap();
            assert_eq!(res, AudioMessage::Error(123));
        }

        let ex = Executor::new().expect("failed to create executor");
        ex.run_until(this_test(&ex)).unwrap();
    }

    #[test]
    fn audio_socket_data_ready_send_and_recv() {
        async fn this_test(ex: &dyn AudioStreamsExecutor) {
            let (sock1, sock2) = UnixStream::pair().unwrap();
            let audio_socket_send = AudioSocket::new(sock1, ex).unwrap();
            let audio_socket_recv = AudioSocket::new(sock2, ex).unwrap();
            audio_socket_send.data_ready(256).await.unwrap();

            // Test receiving by using raw audio_message since CRAS audio server use this.
            let audio_msg: audio_message = audio_socket_recv.read_from_socket().await.unwrap();
            let ref_audio_msg = audio_message {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_READY,
                error: 0,
                frames: 256,
            };
            // Use brace to copy unaligned data locally
            assert_eq!({ audio_msg.id }, { ref_audio_msg.id });
            assert_eq!({ audio_msg.error }, { ref_audio_msg.error });
            assert_eq!({ audio_msg.frames }, { ref_audio_msg.frames });
        }

        let ex = Executor::new().expect("failed to create executor");
        ex.run_until(this_test(&ex)).unwrap();
    }

    #[test]
    fn audio_socket_capture_ready() {
        async fn this_test(ex: &dyn AudioStreamsExecutor) {
            let (sock1, sock2) = UnixStream::pair().unwrap();
            let audio_socket_send = AudioSocket::new(sock1, ex).unwrap();
            let audio_socket_recv = AudioSocket::new(sock2, ex).unwrap();
            audio_socket_send
                .capture_ready(256)
                .await
                .expect("Failed to send capture ready message.");

            // Test receiving by using raw audio_message since CRAS audio server use this.
            let audio_msg: audio_message = audio_socket_recv
                .read_from_socket()
                .await
                .expect("Failed to read audio message from AudioSocket.");
            let ref_audio_msg = audio_message {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_CAPTURED,
                error: 0,
                frames: 256,
            };
            // Use brace to copy unaligned data locally
            assert_eq!({ audio_msg.id }, { ref_audio_msg.id });
            assert_eq!({ audio_msg.error }, { ref_audio_msg.error });
            assert_eq!({ audio_msg.frames }, { ref_audio_msg.frames });
        }

        let ex = Executor::new().expect("failed to create executor");
        ex.run_until(this_test(&ex)).unwrap();
    }

    #[test]
    fn audio_socket_send_when_broken_pipe() {
        async fn this_test(ex: &dyn AudioStreamsExecutor) {
            let sock1 = {
                let (sock1, _) = UnixStream::pair().unwrap();
                sock1
            };
            let audio_socket = AudioSocket::new(sock1, ex).unwrap();
            let res = audio_socket.data_ready(256).await;

            //Broken pipe
            assert!(res.is_err(), "Result should be an error.",);
        }

        let ex = Executor::new().expect("failed to create executor");
        ex.run_until(this_test(&ex)).unwrap();
    }
}
