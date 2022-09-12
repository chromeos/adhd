// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::io;
use std::os::unix::net::UnixStream;
use std::time::Duration;

use cras_sys::gen::{audio_message, CRAS_AUDIO_MESSAGE_ID};
use cros_async::{Executor, TimerAsync};
use futures::{future, future::Either, pin_mut, Future};

#[cfg(test)]
use data_model::DataInit;

use crate::async_;

/// A structure for interacting with the CRAS server audio thread through a `UnixStream::pair`.
pub struct AudioSocket {
    socket: async_::AudioSocket,
    ex: Executor,
}

/// Audio message results which are exchanged by `CrasStream` and CRAS audio server.
/// through an audio socket.
#[allow(dead_code)]
#[derive(Debug)]
pub enum AudioMessage {
    /// * `id` - Audio message id, which is a `enum CRAS_AUDIO_MESSAGE_ID`.
    /// * `frames` - A `u32` indicating the read or written frame count.
    Success {
        id: CRAS_AUDIO_MESSAGE_ID,
        frames: u32,
    },
    /// * `error` - Error code when a error occurs.
    Error(i32),
}

/// Converts AudioMessage to raw audio_message for CRAS audio server.
impl From<AudioMessage> for audio_message {
    fn from(message: AudioMessage) -> audio_message {
        match message {
            AudioMessage::Success { id, frames } => audio_message {
                id,
                error: 0,
                frames,
            },
            AudioMessage::Error(error) => audio_message {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                error,
                frames: 0,
            },
        }
    }
}

/// Converts AudioMessage from raw audio_message from CRAS audio server.
impl From<audio_message> for AudioMessage {
    fn from(message: audio_message) -> Self {
        match message.error {
            0 => AudioMessage::Success {
                id: message.id as CRAS_AUDIO_MESSAGE_ID,
                frames: message.frames,
            },
            error => AudioMessage::Error(error),
        }
    }
}

/// Returns io::ErrorKind::TimedOut if the given future doesn't finish within the timeout
async fn timeout<F, T>(t: Duration, f: F, ex: &Executor) -> io::Result<T>
where
    F: Future<Output = T>,
{
    let sleep_f = TimerAsync::sleep(ex, t);
    pin_mut!(f, sleep_f);
    match future::select(f, sleep_f).await {
        Either::Left((ret, _)) => Ok(ret),
        _ => Err(io::Error::new(io::ErrorKind::TimedOut, format!("{:?}", t))),
    }
}

impl AudioSocket {
    /// Creates `AudioSocket` from a `UnixStream`.
    ///
    /// # Arguments
    /// `socket` - A `UnixStream`.
    pub fn new(s: UnixStream) -> Self {
        let ex = Executor::new().expect("failed to create executor");
        AudioSocket {
            socket: async_::AudioSocket::new(s, &ex).unwrap(),
            ex,
        }
    }

    #[cfg(test)]
    fn read_from_socket<T>(&self) -> io::Result<T>
    where
        T: Sized + DataInit + Default,
    {
        self.ex.run_until(self.socket.read_from_socket())?
    }

    /// Blocks reading an `audio message`.
    ///
    /// # Returns
    /// `AudioMessage` - AudioMessage enum.
    ///
    /// # Errors
    /// Returns io::Error if error occurs.
    pub fn read_audio_message(&self) -> io::Result<AudioMessage> {
        self.ex.run_until(self.socket.read_audio_message())?
    }

    /// Blocks waiting for an `audio message` until `timeout` occurs. If `timeout`
    /// is None, blocks indefinitely.
    ///
    /// # Returns
    /// Some(AudioMessage) - AudioMessage enum if we receive a message before timeout.
    /// None - If the timeout expires.
    ///
    /// # Errors
    /// Returns io::Error if error occurs, or io::ErrorKind::TimedOut upon timeout
    pub fn read_audio_message_with_timeout(
        &mut self,
        t: Option<Duration>,
    ) -> io::Result<AudioMessage> {
        match t {
            None => self.read_audio_message(),
            Some(t) => {
                self.ex
                    .run_until(timeout(t, self.socket.read_audio_message(), &self.ex))??
            }
        }
    }

    /// Sends raw audio message with given AudioMessage enum.
    ///
    /// # Arguments
    /// * `msg` - enum AudioMessage, which could be `Success` with message id
    /// and frames or `Error` with error code.
    ///
    /// # Errors
    /// Returns error if `libc::write` fails.
    #[cfg(test)]
    fn send_audio_message(&self, msg: AudioMessage) -> io::Result<()> {
        self.ex.run_until(self.socket.send_audio_message(msg))?
    }

    /// Sends the data ready message with written frame count.
    ///
    /// # Arguments
    /// * `frames` - An `u32` indicating the written frame count.
    pub fn data_ready(&self, frames: u32) -> io::Result<()> {
        self.ex.run_until(self.socket.data_ready(frames))?
    }

    /// Sends the capture ready message with read frame count.
    ///
    /// # Arguments
    ///
    /// * `frames` - An `u32` indicating the number of read frames.
    pub fn capture_ready(&self, frames: u32) -> io::Result<()> {
        self.ex.run_until(self.socket.capture_ready(frames))?
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // PartialEq for comparing AudioMessage in tests
    impl PartialEq for AudioMessage {
        fn eq(&self, other: &Self) -> bool {
            match (self, other) {
                (
                    AudioMessage::Success { id, frames },
                    AudioMessage::Success {
                        id: other_id,
                        frames: other_frames,
                    },
                ) => id == other_id && frames == other_frames,
                (AudioMessage::Error(err), AudioMessage::Error(other_err)) => err == other_err,
                _ => false,
            }
        }
    }

    fn init_audio_socket_pair() -> (AudioSocket, AudioSocket) {
        let (sock1, sock2) = UnixStream::pair().unwrap();
        let sender = AudioSocket::new(sock1);
        let receiver = AudioSocket::new(sock2);
        (sender, receiver)
    }

    #[test]
    fn audio_socket_send_and_recv_audio_message() {
        let (sender, receiver) = init_audio_socket_pair();
        let message_succ = AudioMessage::Success {
            id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
            frames: 0,
        };
        sender.send_audio_message(message_succ).unwrap();
        let res = receiver.read_audio_message().unwrap();
        assert_eq!(
            res,
            AudioMessage::Success {
                id: CRAS_AUDIO_MESSAGE_ID::AUDIO_MESSAGE_REQUEST_DATA,
                frames: 0
            }
        );

        let message_err = AudioMessage::Error(123);
        sender.send_audio_message(message_err).unwrap();
        let res = receiver.read_audio_message().unwrap();
        assert_eq!(res, AudioMessage::Error(123));
    }

    #[test]
    fn audio_socket_data_ready_send_and_recv() {
        let (sock1, sock2) = UnixStream::pair().unwrap();
        let audio_socket_send = AudioSocket::new(sock1);
        let audio_socket_recv = AudioSocket::new(sock2);
        audio_socket_send.data_ready(256).unwrap();

        // Test receiving by using raw audio_message since CRAS audio server use this.
        let audio_msg: audio_message = audio_socket_recv.read_from_socket().unwrap();
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

    #[test]
    fn audio_socket_capture_ready() {
        let (sock1, sock2) = UnixStream::pair().unwrap();
        let audio_socket_send = AudioSocket::new(sock1);
        let audio_socket_recv = AudioSocket::new(sock2);
        audio_socket_send
            .capture_ready(256)
            .expect("Failed to send capture ready message.");

        // Test receiving by using raw audio_message since CRAS audio server use this.
        let audio_msg: audio_message = audio_socket_recv
            .read_from_socket()
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

    #[test]
    fn audio_socket_send_when_broken_pipe() {
        let sock1 = {
            let (sock1, _) = UnixStream::pair().unwrap();
            sock1
        };
        let audio_socket = AudioSocket::new(sock1);
        let res = audio_socket.data_ready(256);
        //Broken pipe
        assert!(res.is_err(), "Result should be an error.",);
    }
}
