// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use audio_processor::config;
use audio_processor::processors::peer::BlockingSeqPacketProcessor;
use audio_processor::AudioProcessor;
use audio_processor::Format;
use audio_processor::MultiBuffer;
use command_fds::CommandFdExt;
use command_fds::FdMapping;

#[test]
fn test_peer() {
    let audio_worker_binary = env!("CARGO_BIN_EXE_audio-worker");
    dbg!(audio_worker_binary);
    let (host_fd, peer_fd) =
        audio_processor::processors::peer::create_socketpair().expect("create socketpair");

    let _child = std::process::Command::new(audio_worker_binary)
        .fd_mappings(vec![FdMapping {
            parent_fd: peer_fd,
            child_fd: 3,
        }])
        .expect(".fd_mappings()")
        .spawn()
        .expect(".spawn()");

    let mut processor = BlockingSeqPacketProcessor::new(
        host_fd,
        Format {
            channels: 2,
            block_size: 4,
            frame_rate: 44100,
        },
        config::Processor::Negate,
    )
    .unwrap();
    assert_eq!(
        processor.get_output_format(),
        Format {
            channels: 2,
            block_size: 4,
            frame_rate: 44100,
        }
    );

    let mut input = MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
    let output = processor.process(input.as_multi_slice()).unwrap();
    assert_eq!(
        output.into_raw(),
        [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
    );

    let mut input = MultiBuffer::from(vec![vec![11., 12., 13.], vec![15., 16., 17.]]);
    let output = processor.process(input.as_multi_slice()).unwrap();
    assert_eq!(output.into_raw(), [[-11., -12., -13.], [-15., -16., -17.]]);
}
