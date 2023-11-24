# AudioProcessor

AudioProcessor is an interface to process audio frames at fixed sized chunks.

[TOC]

## AudioProcessor Plugins

AudioProcessor plugins are shared libraries that implement the AudioProcessor
API. The interface is defined in [plugin_processor.h](c/plugin_processor.h).
Each plugin shared library defines at least one function that matches the
`processor_create` signature.

## Running a plugin offline on a Linux workstation

A tool named `offline-pipeline` allows AudioProcessor plugins to be tested
offline (out of the audio server).

The tool can be built using `cargo build`.

Here, we use [echo_plugin.cc](c/echo_plugin.cc) as an example.
The echo pluginis an AudioProcessor which generates echo with a delay of 0.5 seconds.

### Prerequisites

Have these in your `$PATH`:

*   `clang`
*   `cargo` (can be installed with instructions in https://rustup.rs/)

Get the code and cd into this directory.

### Build

Build the `offline-pipeline` tool with:

```
cargo build --release
```

Build the plugin with:

```
clang++ -std=gnu++20 -shared -fPIC c/echo_plugin.cc -o libecho.so
```

### Run

Here we try to process some speech:
https://commondatastorage.googleapis.com/chromiumos-test-assets-public/tast/cros/audio/audio_long16_20231013.wav

Download the file:

```
wget https://commondatastorage.googleapis.com/chromiumos-test-assets-public/tast/cros/audio/audio_long16_20231013.wav
```

Process the speech:

```
../target/release/offline-pipeline ./libecho.so --plugin-name=echo_processor_create audio_long16_20231013.wav out.wav
```

The result is at `out.wav`. You can try to play it or inspect it with software like Audacity.

The `offline-pipeline` outputs simple statistics about the processor.

See `offline-pipeline --help` for more information.

## Running a plugin offline on a ChromiumOS device

The `offline-pipeline` tool is also available on test images.

Deploy the shared library to a executable partition then pass the path to
`offline-pipeline`.

To build for x86_64 ChromiumOS, use the following toolchain and sysroot:
*   toolchain: https://storage.googleapis.com/chromiumos-sdk/2023/11/x86_64-cros-linux-gnu-2023.11.08.140039.tar.xz
*   sysroot: https://storage.googleapis.com/chromiumos-image-archive/amd64-generic-public/R121-15672.0.0/sysroot_chromeos-base_chromeos-chrome.tar.xz

Using the echo plugin as an example:

Download and unpack the toolchain:

```
mkdir toolchain
wget https://storage.googleapis.com/chromiumos-sdk/2023/11/x86_64-cros-linux-gnu-2023.11.08.140039.tar.xz
tar xf x86_64-cros-linux-gnu-2023.11.08.140039.tar.xz -C toolchain
mkdir sysroot
wget https://storage.googleapis.com/chromiumos-image-archive/amd64-generic-public/R121-15672.0.0/sysroot_chromeos-base_chromeos-chrome.tar.xz
tar xf sysroot_chromeos-base_chromeos-chrome.tar.xz -C sysroot
```

Build processor:

```
toolchain/bin/x86_64-cros-linux-gnu-clang++ --sysroot=$PWD/sysroot -std=gnu++20 -stdlib=libc++ -shared -fPIC -g -O2 c/echo_plugin.cc -o libecho.so
```

Copy the plugin to the ChromiumOS DUT:

```
rsync -ahPv libecho.so $DUT:/usr/local/lib64/
```

Download test data:

```
wget https://commondatastorage.googleapis.com/chromiumos-test-assets-public/tast/cros/audio/audio_long16_20231013.wav -O /tmp/in.wav
```

Run the plugin on the DUT:

```
offline-pipeline /usr/local/lib64/libecho.so --plugin-name=echo_processor_create /tmp/in.wav /tmp/out.wav
```

Play processed audio directly on the DUT:

```
cras_tests playback /tmp/out.wav
```

## Running a plugin online on a ChromiumOS device

Suppose you followed the instructions to deploy the shared library to your DUT, you can
try to run it online on the DUT.

1.  Modify `/etc/cras/processor_override.txtpb` so that in the `input` section:
    1.  `enabled` is `true`.
    2.  `plugin_path` points to the absolute path of your plugin on the DUT.
    3.  `constructor` is set to your plugin's constructor name.
    4.  There are other parameters available. Refer to the comments in the config file.

2.  Stop `cras` and run the debug version:

    ```
    stop cras
    cras-dev.sh
    ```

3.  Try to record audio using any web app, or use `cras_tests`.

    ```
    cras_tests capture /tmp/capture.wav
    ```

    If the override is enabled, then you should see logs like the below from `cras-dev.sh` logs.

    ```
    CrasProcessor #0 created with: CrasProcessorConfig { channels: 1, block_size: 480, frame_rate: 48000, effect: Overridden }
    ```

4.  To stop debugging, press `Ctrl-C` in the `cras-dev.sh` terminal and then run `start cras`.

## Known issues

*   The `offline-pipeline` currently assumes that the input and output have the
    same frame rate and number of channels.
