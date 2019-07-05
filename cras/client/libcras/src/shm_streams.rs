// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::error;
use std::fmt;
use std::os::unix::io::RawFd;
use std::time::{Duration, Instant};

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

/// Class that implements ShmStream trait but does nothing with the samples
pub struct NullShmStream {
    buffer_size: usize,
    frame_size: usize,
    interval: Duration,
    next_frame: Duration,
    start_time: Instant,
}

impl NullShmStream {
    /// Attempt to create a new NullShmStream with the given number of channels,
    /// format, and buffer_size.
    pub fn new(
        buffer_size: usize,
        num_channels: usize,
        format: SampleFormat,
        frame_rate: usize,
    ) -> Self {
        let interval = Duration::from_millis(buffer_size as u64 * 1000 / frame_rate as u64);
        Self {
            buffer_size,
            frame_size: format.sample_bytes() * num_channels,
            interval,
            next_frame: interval,
            start_time: Instant::now(),
        }
    }
}

impl BufferSet for NullShmStream {
    fn callback(&mut self, _offset: usize, _frames: usize) -> GenericResult<()> {
        Ok(())
    }
}

impl ShmStream for NullShmStream {
    fn frame_size(&self) -> usize {
        self.frame_size
    }

    fn wait_for_next_action_with_timeout<'a>(
        &'a mut self,
        timeout: Duration,
    ) -> GenericResult<Option<ServerRequest<'a>>> {
        let elapsed = self.start_time.elapsed();
        if elapsed < self.next_frame {
            if timeout < self.next_frame - elapsed {
                std::thread::sleep(timeout);
                return Ok(None);
            } else {
                std::thread::sleep(self.next_frame - elapsed);
            }
        }
        self.next_frame += self.interval;
        Ok(Some(ServerRequest::new(self.buffer_size, self)))
    }
}

/// Source of `NullShmStream` objects.
#[derive(Default)]
pub struct NullShmStreamSource;

impl NullShmStreamSource {
    pub fn new() -> Self {
        Self::default()
    }
}

impl ShmStreamSource for NullShmStreamSource {
    fn new_stream(
        &mut self,
        _direction: StreamDirection,
        num_channels: usize,
        format: SampleFormat,
        frame_rate: usize,
        buffer_size: usize,
        _client_shm: &SharedMemory,
        _buffer_offsets: [u32; 2],
    ) -> GenericResult<Box<dyn ShmStream>> {
        let new_stream = NullShmStream::new(buffer_size, num_channels, format, frame_rate);
        Ok(Box::new(new_stream))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    };
    use sync::{Condvar, Mutex};

    #[derive(Clone)]
    pub struct MockShmStream {
        request_size: usize,
        frame_size: usize,
        request_notifier: Arc<(Mutex<bool>, Condvar)>,
    }

    impl MockShmStream {
        /// Attempt to create a new MockShmStream with the given number of
        /// channels, format, and buffer_size.
        pub fn new(num_channels: usize, format: SampleFormat, buffer_size: usize) -> Self {
            Self {
                request_size: buffer_size,
                frame_size: format.sample_bytes() * num_channels,
                request_notifier: Arc::new((Mutex::new(false), Condvar::new())),
            }
        }

        /// Call to request data from the stream, causing it to return from
        /// `wait_for_next_action_with_timeout`. Will block until
        /// `set_buffer_offset_and_frames` is called on the ServerRequest returned
        /// from `wait_for_next_action_with_timeout`, or until `timeout` elapses.
        /// Returns true if a response was successfully received.
        pub fn trigger_callback_with_timeout(&mut self, timeout: Duration) -> bool {
            let &(ref lock, ref cvar) = &*self.request_notifier;
            let mut requested = lock.lock();
            *requested = true;
            cvar.notify_one();
            let start_time = Instant::now();
            while *requested {
                requested = cvar.wait_timeout(requested, timeout).0;
                if start_time.elapsed() > timeout {
                    return false;
                }
            }

            return true;
        }
    }

    impl BufferSet for MockShmStream {
        fn callback(&mut self, _offset: usize, _frames: usize) -> GenericResult<()> {
            let &(ref lock, ref cvar) = &*self.request_notifier;
            let mut requested = lock.lock();
            *requested = false;
            cvar.notify_one();
            Ok(())
        }
    }

    impl ShmStream for MockShmStream {
        fn frame_size(&self) -> usize {
            self.frame_size
        }

        fn wait_for_next_action_with_timeout<'a>(
            &'a mut self,
            timeout: Duration,
        ) -> GenericResult<Option<ServerRequest<'a>>> {
            {
                let start_time = Instant::now();
                let &(ref lock, ref cvar) = &*self.request_notifier;
                let mut requested = lock.lock();
                while !*requested {
                    requested = cvar.wait_timeout(requested, timeout).0;
                    if start_time.elapsed() > timeout {
                        return Ok(None);
                    }
                }
            }

            Ok(Some(ServerRequest::new(self.request_size, self)))
        }
    }

    /// Source of `MockShmStream` objects.
    #[derive(Clone, Default)]
    pub struct MockShmStreamSource {
        last_stream: Arc<(Mutex<Option<MockShmStream>>, Condvar)>,
    }

    impl MockShmStreamSource {
        pub fn new() -> Self {
            Default::default()
        }

        /// Get the last stream that has been created from this source. If no stream
        /// has been created, block until one has.
        pub fn get_last_stream(&self) -> MockShmStream {
            let &(ref last_stream, ref cvar) = &*self.last_stream;
            let mut stream = last_stream.lock();
            loop {
                match *stream {
                    None => stream = cvar.wait(stream),
                    Some(ref s) => return s.clone(),
                };
            }
        }
    }

    impl ShmStreamSource for MockShmStreamSource {
        fn new_stream(
            &mut self,
            _direction: StreamDirection,
            num_channels: usize,
            format: SampleFormat,
            _frame_rate: usize,
            buffer_size: usize,
            _client_shm: &SharedMemory,
            _buffer_offsets: [u32; 2],
        ) -> GenericResult<Box<dyn ShmStream>> {
            let &(ref last_stream, ref cvar) = &*self.last_stream;
            let mut stream = last_stream.lock();

            let new_stream = MockShmStream::new(num_channels, format, buffer_size);
            *stream = Some(new_stream.clone());
            cvar.notify_one();
            Ok(Box::new(new_stream))
        }
    }

    #[test]
    fn mock_trigger_callback() {
        let stream_source = MockShmStreamSource::new();
        let mut thread_stream_source = stream_source.clone();

        let buffer_size = 480;
        let num_channels = 2;
        let format = SampleFormat::S24LE;
        let shm = SharedMemory::anon().expect("Failed to create shm");

        let handle = std::thread::spawn(move || {
            let mut stream = thread_stream_source
                .new_stream(
                    StreamDirection::Playback,
                    num_channels,
                    format,
                    44100,
                    buffer_size,
                    &shm,
                    [400, 8000],
                )
                .expect("Failed to create stream");

            let request = stream
                .wait_for_next_action_with_timeout(Duration::from_secs(5))
                .expect("Failed to wait for next action");
            let result = match request {
                Some(r) => {
                    let requested = r.requested_frames();
                    r.set_buffer_offset_and_frames(872, requested)
                        .expect("Failed to set buffer offset and frames");
                    requested
                }
                None => 0,
            };
            result
        });

        let mut stream = stream_source.get_last_stream();
        assert!(stream.trigger_callback_with_timeout(Duration::from_secs(1)));

        let requested_frames = handle.join().expect("Failed to join thread");
        assert_eq!(requested_frames, buffer_size);
    }

    #[test]
    fn null_consumption_rate() {
        let frame_rate = 44100;
        let buffer_size = 480;
        let interval = Duration::from_millis(buffer_size as u64 * 1000 / frame_rate as u64);

        let shm = SharedMemory::anon().expect("Failed to create shm");

        let mut stream_source = NullShmStreamSource::new();
        let mut stream = stream_source
            .new_stream(
                StreamDirection::Playback,
                2,
                SampleFormat::S24LE,
                frame_rate,
                buffer_size,
                &shm,
                [400, 8000],
            )
            .expect("Failed to create stream");

        let start = Instant::now();

        let timeout = Duration::from_secs(5);
        let request = stream
            .wait_for_next_action_with_timeout(timeout)
            .expect("Failed to wait for first request")
            .expect("First request should not have timed out");
        request
            .set_buffer_offset_and_frames(276, 480)
            .expect("Failed to set buffer offset and length");

        // The second call should block until the first buffer is consumed.
        let _request = stream
            .wait_for_next_action_with_timeout(timeout)
            .expect("Failed to wait for second request");
        let elapsed = start.elapsed();
        assert!(
            elapsed > interval,
            "wait_for_next_action_with_timeout didn't block long enough: {:?}",
            elapsed
        );

        assert!(
            elapsed < timeout,
            "wait_for_next_action_with_timeout blocked for too long: {:?}",
            elapsed
        );
    }
}