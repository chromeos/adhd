// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use audio_processor::config;
use audio_processor::processors::peer::AudioWorkerSubprocessFactory;
use audio_processor::processors::peer::ManagedBlockingSeqPacketProcessor;
use audio_processor::processors::peer::AUDIO_WORKER_PIPELINE_TIMEOUT_SEC;
use audio_processor::AudioProcessor;
use audio_processor::Format;
use audio_processor::MultiBuffer;

#[test]
fn test_peer() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let mut processor = ManagedBlockingSeqPacketProcessor::new(
        &factory,
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

#[test]
#[cfg(feature = "bazel")]
fn test_peer_ffi_negate() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let test_plugin_path = std::env::var("LIBTEST_PLUGINS_SO").expect("LIBTEST_PLUGINS_SO not set");

    let mut processor = ManagedBlockingSeqPacketProcessor::new(
        &factory,
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        },
        config::Processor::Plugin {
            path: test_plugin_path.into(),
            constructor: "negate_processor_create".to_string(),
        },
    )
    .unwrap();

    assert_eq!(
        processor.get_output_format(),
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        }
    );

    let mut input = MultiBuffer::from(vec![vec![1., 2., 3., 4.]]);
    let output = processor.process(input.as_multi_slice()).unwrap();
    assert_eq!(output.into_raw(), [[-1., -2., -3., -4.]]);
}

#[test]
#[cfg(feature = "bazel")]
fn test_peer_ffi_abort_create() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let test_plugin_path = std::env::var("LIBTEST_PLUGINS_SO").expect("LIBTEST_PLUGINS_SO not set");

    let result = ManagedBlockingSeqPacketProcessor::new(
        &factory,
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        },
        config::Processor::Plugin {
            path: test_plugin_path.into(),
            constructor: "bad_plugin_abort_create".to_string(),
        },
    );

    let err_msg = match result {
        Ok(_) => panic!("Expected an error, but got Ok"),
        Err(e) => {
            format!("{e:#}")
        }
    };
    assert_eq!(
        err_msg,
        "BlockingSeqPacketProcessor::new: recv init message reply: empty message"
    );
}

#[test]
#[cfg(feature = "bazel")]
fn test_peer_ffi_abort_run() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let test_plugin_path = std::env::var("LIBTEST_PLUGINS_SO").expect("LIBTEST_PLUGINS_SO not set");

    let mut processor = ManagedBlockingSeqPacketProcessor::new(
        &factory,
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        },
        config::Processor::Plugin {
            path: test_plugin_path.into(),
            constructor: "bad_plugin_abort_run_create".to_string(),
        },
    )
    .unwrap();

    let mut input = MultiBuffer::from(vec![vec![1., 2., 3., 4.]]);
    let result = processor.process(input.as_multi_slice());

    let err_msg = match result {
        Ok(_) => panic!("Expected an error, but got Ok"),
        Err(e) => {
            format!("{e:#}")
        }
    };
    assert_eq!(err_msg, "unrecoverable error: empty message");
}

#[test]
#[cfg(feature = "bazel")]
fn test_peer_ffi_hanging_create() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let test_plugin_path = std::env::var("LIBTEST_PLUGINS_SO").expect("LIBTEST_PLUGINS_SO not set");

    let start_time = std::time::Instant::now();
    let result = ManagedBlockingSeqPacketProcessor::new(
        &factory,
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        },
        config::Processor::Plugin {
            path: test_plugin_path.into(),
            constructor: "bad_plugin_hanging_create".to_string(),
        },
    );
    let elapsed = start_time.elapsed();

    let err_msg = match result {
        Ok(_) => panic!("Expected an error, but got Ok"),
        Err(e) => {
            format!("{e:#}")
        }
    };
    assert_eq!(
        err_msg,
        "BlockingSeqPacketProcessor::new: recv init message reply: empty message"
    );
    assert!(
        elapsed >= Duration::from_secs(AUDIO_WORKER_PIPELINE_TIMEOUT_SEC as u64),
        "elapsed time {:?} is less than timeout",
        elapsed
    );
}

#[test]
#[cfg(feature = "bazel")]
fn test_peer_ffi_hanging_run() {
    let audio_worker_executable = env!("CARGO_BIN_EXE_audio-worker");
    let factory =
        AudioWorkerSubprocessFactory::default().with_executable_path(audio_worker_executable);
    let test_plugin_path = std::env::var("LIBTEST_PLUGINS_SO").expect("LIBTEST_PLUGINS_SO not set");

    let mut processor = ManagedBlockingSeqPacketProcessor::new(
        &factory,
        Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        },
        config::Processor::Plugin {
            path: test_plugin_path.into(),
            constructor: "bad_plugin_hanging_run_create".to_string(),
        },
    )
    .unwrap();

    let mut input = MultiBuffer::from(vec![vec![1., 2., 3., 4.]]);
    let start_time = std::time::Instant::now();
    let result = processor.process(input.as_multi_slice());
    let elapsed = start_time.elapsed();

    let err_msg = match result {
        Ok(_) => panic!("Expected an error, but got Ok"),
        Err(e) => {
            format!("{e:#}")
        }
    };
    assert_eq!(err_msg, "unrecoverable error: empty message");
    assert!(
        elapsed >= Duration::from_secs(AUDIO_WORKER_PIPELINE_TIMEOUT_SEC as u64),
        "elapsed time {:?} is less than timeout",
        elapsed
    );
}
