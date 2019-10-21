// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::error;
use std::fmt;
use std::os::unix::io::RawFd;
use std::time::Duration;

use audio_streams::SampleFormat;
use sys_util::SharedMemory;

use crate::cras_types::StreamDirection;

type GenericResult<T> = std::result::Result<T, Box<dyn error::Error>>;

/// `BufferSet` is used as a callback mechanism for `ServerRequest` objects.
/// It is meant to be implemented by the audio stream, allowing arbitrary code
/// to be run after a buffer offset and length is set.
pub trait BufferSet {
    /// Called when the client sets a buffer offset and length.
    ///
    /// `offset` is the offset within shared memory of the buffer and `frames`
    /// indicates the number of audio frames that can be read from or written to
    /// the buffer.
    fn callback(&mut self, offset: usize, frames: usize) -> GenericResult<()>;
}

#[derive(Debug)]
pub enum Error {
    TooManyFrames(usize, usize),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::TooManyFrames(provided, requested) => write!(
                f,
                "Provided number of frames {} exceeds requested number of frames {}",
                provided, requested
            ),
        }
    }
}

/// `ServerRequest` represents an active request from the server for the client
/// to provide a buffer in shared memory to playback from or capture to.
pub struct ServerRequest<'a> {
    requested_frames: usize,
    buffer_set: &'a mut dyn BufferSet,
}

impl<'a> ServerRequest<'a> {
    /// Create a new ServerRequest object
    ///
    /// Create a ServerRequest object representing a request from the server
    /// for a buffer `requested_frames` in size.
    ///
    /// When the client responds to this request by calling
    /// [`set_buffer_offset_and_frames`](ServerRequest::set_buffer_offset_and_frames),
    /// BufferSet::callback will be called on `buffer_set`.
    ///
    /// # Arguments
    /// * `requested_frames` - The requested buffer size in frames.
    /// * `buffer_set` - The object implementing the callback for when a buffer is provided.
    pub fn new<D: BufferSet>(requested_frames: usize, buffer_set: &'a mut D) -> Self {
        Self {
            requested_frames,
            buffer_set,
        }
    }

    /// Get the number of frames of audio data requested by the server.
    ///
    /// The returned value should never be greater than the `buffer_size`
    /// given in [`new_stream`](ShmStreamSource::new_stream).
    pub fn requested_frames(&self) -> usize {
        self.requested_frames
    }

    /// Sets the buffer offset and length for the requested buffer.
    ///
    /// Sets the buffer offset and length of the buffer that fulfills this
    /// server request to `offset` and `length`, respectively. This means that
    /// `length` bytes of audio samples may be read from/written to that
    /// location in `client_shm` for a playback/capture stream, respectively.
    /// This function may only be called once for a `ServerRequest`, at which
    /// point the ServerRequest is dropped and no further calls are possible.
    ///
    /// # Arguments
    ///
    /// * `offset` - The value to use as the new buffer offset for the next buffer.
    /// * `frames` - The length of the next buffer in frames.
    ///
    /// # Errors
    ///
    /// * If `frames` is greater than `requested_frames`.
    pub fn set_buffer_offset_and_frames(self, offset: usize, frames: usize) -> GenericResult<()> {
        if frames > self.requested_frames {
            return Err(Box::new(Error::TooManyFrames(
                frames,
                self.requested_frames,
            )));
        }

        self.buffer_set.callback(offset, frames)
    }
}

/// `ShmStream` allows a client to interact with an active CRAS stream.
pub trait ShmStream: Send {
    /// Get the size of a frame of audio data for this stream.
    fn frame_size(&self) -> usize;

    /// Waits until the next server message indicating action is required.
    ///
    /// For playback streams, this will be `AUDIO_MESSAGE_REQUEST_DATA`, meaning
    /// that we must set the buffer offset to the next location where playback
    /// data can be found.
    /// For capture streams, this will be `AUDIO_MESSAGE_DATA_READY`, meaning
    /// that we must set the buffer offset to the next location where captured
    /// data can be written to.
    /// Will return early if `timeout` elapses before a message is received.
    ///
    /// # Arguments
    ///
    /// * `timeout` - The amount of time to wait until a message is received.
    ///
    /// # Return value
    ///
    /// Returns `Some(request)` where `request` is an object that implements the
    /// [`ServerRequest`](ServerRequest) trait and which can be used to get the
    /// number of bytes requested for playback streams or that have already been
    /// written to shm for capture streams.
    ///
    /// If the timeout occurs before a message is received, returns `None`.
    ///
    /// # Errors
    ///
    /// * If an invalid message type is received for the stream.
    fn wait_for_next_action_with_timeout(
        &mut self,
        timeout: Duration,
    ) -> GenericResult<Option<ServerRequest>>;
}

/// `ShmStreamSource` creates streams for playback or capture of audio.
pub trait ShmStreamSource: Send {
    /// Creates a new [`ShmStream`](ShmStream)
    ///
    /// Creates a new `ShmStream` object, which allows:
    /// * Waiting until the server has communicated that data is ready or
    ///   requested that we make more data available.
    /// * Setting the location and length of buffers for reading/writing audio data.
    ///
    /// # Arguments
    ///
    /// * `direction` - The direction of the stream, either `Playback` or `Capture`.
    /// * `num_channels` - The number of audio channels for the stream.
    /// * `format` - The audio format to use for audio samples.
    /// * `frame_rate` - The stream's frame rate in Hz.
    /// * `buffer_size` - The maximum size of an audio buffer. This will be the
    ///                   size used for transfers of audio data between client
    ///                   and server.
    /// * `client_shm` - The shared memory area that will contain samples.
    /// * `buffer_offsets` - The two initial values to use as buffer offsets
    ///                      for streams. This way, the server will not write
    ///                      audio data to an arbitrary offset in `client_shm`
    ///                      if the client fails to update offsets in time.
    ///
    /// # Errors
    ///
    /// * If sending the connect stream message to the server fails.
    fn new_stream(
        &mut self,
        direction: StreamDirection,
        num_channels: usize,
        format: SampleFormat,
        frame_rate: usize,
        buffer_size: usize,
        client_shm: &SharedMemory,
        buffer_offsets: [u32; 2],
    ) -> GenericResult<Box<dyn ShmStream>>;

    /// Get a list of file descriptors used by the implementation.
    ///
    /// Returns any open file descriptors needed by the implementation.
    /// This list helps users of the ShmStreamSource enter Linux jails without
    /// closing needed file descriptors.
    fn keep_fds(&self) -> Vec<RawFd> {
        Vec::new()
    }
}
