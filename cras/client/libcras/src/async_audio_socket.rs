// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::io;
use std::mem;
use std::os::unix::net::UnixStream;

use cras_sys::gen::{audio_message, CRAS_AUDIO_MESSAGE_ID};
use cros_async::{AsyncResult, AsyncWrapper, Executor, IoSourceExt};
use data_model::DataInit;

use crate::audio_socket::AudioMessage;

pub struct AsyncAudioSocket {
    socket: Box<dyn IoSourceExt<AsyncWrapper<UnixStream>> + Send>,
}

impl AsyncAudioSocket {
    /// Creates `AsyncAudioSocket` from a `UnixStream`.
    ///
    /// # Arguments
    /// `socket` - A `UnixStream`.
    /// `ex` - An `Executor`.
    pub fn new(s: UnixStream, ex: &Executor) -> AsyncResult<AsyncAudioSocket> {
        Ok(AsyncAudioSocket {
            socket: ex.async_from(AsyncWrapper::new(s))?,
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

#[cfg(test)]
mod tests {
    use super::*;

    fn init_audio_socket_pair(ex: &Executor) -> (AsyncAudioSocket, AsyncAudioSocket) {
        let (sock1, sock2) = UnixStream::pair().unwrap();
        let sender = AsyncAudioSocket::new(sock1, ex).unwrap();
        let receiver = AsyncAudioSocket::new(sock2, ex).unwrap();
        (sender, receiver)
    }

    #[test]
    fn audio_socket_send_and_recv_audio_message() {
        async fn this_test(ex: &Executor) {
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
        async fn this_test(ex: &Executor) {
            let (sock1, sock2) = UnixStream::pair().unwrap();
            let audio_socket_send = AsyncAudioSocket::new(sock1, ex).unwrap();
            let audio_socket_recv = AsyncAudioSocket::new(sock2, ex).unwrap();
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
        async fn this_test(ex: &Executor) {
            let (sock1, sock2) = UnixStream::pair().unwrap();
            let audio_socket_send = AsyncAudioSocket::new(sock1, ex).unwrap();
            let audio_socket_recv = AsyncAudioSocket::new(sock2, ex).unwrap();
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
        async fn this_test(ex: &Executor) {
            let sock1 = {
                let (sock1, _) = UnixStream::pair().unwrap();
                sock1
            };
            let audio_socket = AsyncAudioSocket::new(sock1, ex).unwrap();
            let res = audio_socket.data_ready(256).await;

            //Broken pipe
            assert!(res.is_err(), "Result should be an error.",);
        }

        let ex = Executor::new().expect("failed to create executor");
        ex.run_until(this_test(&ex)).unwrap();
    }
}
