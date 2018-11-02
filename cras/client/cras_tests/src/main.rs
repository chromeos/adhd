// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::thread::spawn;
type Result<T> = std::result::Result<T, Box<std::error::Error>>;

use audio_streams::StreamSource;
use libcras::CrasClient;

const BUFFER_SIZE: usize = 256;
const FRAME_RATE: usize = 44100;
const NUM_CHANNELS: usize = 2;

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    match args.len() {
        2 => {
            let mut cras_client = CrasClient::new()?;
            let (_control, mut stream) =
                cras_client.new_playback_stream(NUM_CHANNELS, FRAME_RATE, BUFFER_SIZE)?;

            let thread = spawn(move || {
                // Play 1000 * BUFFER_SIZE samples from a file
                let mut file = File::open(&args[1]).unwrap();
                let mut local_buffer = [0u8; BUFFER_SIZE * NUM_CHANNELS * 2];
                for _i in 0..1000 {
                    // Reads data to local buffer
                    let _read_count = file.read(&mut local_buffer).unwrap();

                    // Gets writable buffer from stream and
                    let mut buffer = stream.next_playback_buffer().unwrap();
                    // Writes data to stream buffer
                    let _write_frames = buffer.write(&local_buffer).unwrap();
                }
            });
            thread.join().expect("Failed to join playback thread");
            // Stream and client should gracefully be closed out of this scope
        }
        _ => {
            println!("cras_tests /path/to/playback_file.raw");
        }
    };
    Ok(())
}
