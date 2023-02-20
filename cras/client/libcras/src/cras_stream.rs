// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::cmp::min;
use std::io;
use std::marker::PhantomData;
use std::time::Duration;
use std::{error, fmt};

use audio_streams::{
    capture::{CaptureBuffer, CaptureBufferStream},
    BoxError, BufferCommit, PlaybackBuffer, PlaybackBufferStream,
};
use cras_sys::gen::{snd_pcm_format_t, CRAS_AUDIO_MESSAGE_ID, CRAS_STREAM_DIRECTION};
use libchromeos::sys::error;

use crate::audio_socket::{AudioMessage, AudioSocket};
use crate::cras_server_socket::CrasServerSocket;
use crate::cras_shm::*;

#[derive(Debug)]
pub enum Error {
    IoError(io::Error),
    MessageTypeError,
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::IoError(ref err) => err.fmt(f),
            Error::MessageTypeError => write!(f, "Message type error"),
        }
    }
}

impl From<io::Error> for Error {
    fn from(io_err: io::Error) -> Error {
        Error::IoError(io_err)
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

impl<'a> BufferCommit for CrasPlaybackData<'a> {
    fn commit(&mut self, nframes: usize) {
        let log_err = |e| error!("buffer commit error: {}", e);
        if let Err(e) = self.header.commit_written_frames(nframes as u32) {
            log_err(e);
        }
        if let Err(e) = self.audio_sock.data_ready(nframes as u32) {
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

impl<'a> BufferCommit for CrasCaptureData<'a> {
    fn commit(&mut self, nframes: usize) {
        let log_err = |e| error!("buffer commit error: {}", e);
        if let Err(e) = self.header.commit_read_frames(nframes as u32) {
            log_err(e);
        }
        if let Err(e) = self.audio_sock.capture_ready(nframes as u32) {
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
pub struct CrasStream<'a, T: CrasStreamData<'a> + BufferCommit> {
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

impl<'a, T: CrasStreamData<'a> + BufferCommit> CrasStream<'a, T> {
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

    fn wait_request_data(&mut self) -> Result<(), Error> {
        match self.controls.audio_sock_mut().read_audio_message()? {
            AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                ..
            } => Ok(()),
            _ => Err(Error::MessageTypeError),
        }
    }

    fn wait_data_ready(&mut self) -> Result<u32, Error> {
        match self.controls.audio_sock_mut().read_audio_message()? {
            AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_DATA_READY,
                frames,
            } => Ok(frames),
            _ => Err(Error::MessageTypeError),
        }
    }
}

impl<'a, T: CrasStreamData<'a> + BufferCommit> Drop for CrasStream<'a, T> {
    /// A blocking drop function, sends the disconnect message to `CrasClient` and waits for
    /// the return message.
    /// Logs an error message to stderr if the method fails.
    fn drop(&mut self) {
        if let Err(e) = self.server_socket.disconnect_stream(self.stream_id) {
            error!("CrasStream::Drop error: {}", e);
        }
    }
}

impl<'a, T: CrasStreamData<'a> + BufferCommit> PlaybackBufferStream for CrasStream<'a, T> {
    fn next_playback_buffer<'b, 's: 'b>(&'s mut self) -> Result<PlaybackBuffer<'b>, BoxError> {
        // Wait for request audio message
        self.wait_request_data()?;
        let header = self.controls.header_mut();
        let frame_size = header.get_frame_size();
        let (offset, len) = header.get_write_offset_and_len()?;
        let buf = &mut self.audio_buffer.get_buffer()[offset..offset + len];

        PlaybackBuffer::new(frame_size, buf, &mut self.controls).map_err(Box::from)
    }
}

impl<'a, T: CrasStreamData<'a> + BufferCommit> CaptureBufferStream for CrasStream<'a, T> {
    fn next_capture_buffer<'b, 's: 'b>(&'b mut self) -> Result<CaptureBuffer<'b>, BoxError> {
        // Wait for data ready message
        let frames = self.wait_data_ready()?;
        let header = self.controls.header_mut();
        let frame_size = header.get_frame_size();
        let shm_frames = header.get_readable_frames()?;
        let len = min(shm_frames, frames as usize) * frame_size;
        let offset = header.get_read_buffer_offset()?;
        let buf = &mut self.audio_buffer.get_buffer()[offset..offset + len];

        CaptureBuffer::new(frame_size, buf, &mut self.controls).map_err(Box::from)
    }
}
