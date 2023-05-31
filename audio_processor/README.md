# AudioProcessor WIP

AudioProcessor is an interface to process audio frames at fixed sized chunks.

## AudioProcessor Plugins

AudioProcessor plugins are shared libraries that implement the AudioProcessor
API. The interface is defined in [plugin_processor.h](c/plugin_processor.h).
Each plugin shared library defines at least one function that matches the
`processor_create` signature.

## Running a plugin offline

A tool named `offline-pipeline` allows AudioProcessor plugins to be tested
offline (out of the audio server).

The tool can be built using `cargo build`.

## Example: negate_plugin

[negate_plugin.c](c/negate_plugin.c) is an AudioProcessor which negates
audio samples. Here we show how we can build and run it.

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
clang -fPIC -shared c/negate_plugin.c -o libnegate.so
```

### Run

Here's an example processing a sine wave.

Generate sine wave:

```
sox -n -L -e signed-integer -b 16 -r 48000 -c 1 sine.wav synth 60 sine 300 sine 300 gain -10
```

Process the sine wave:

```
target/release/offline-pipeline ./libnegate.so --plugin-name=negate_processor_create sine.wav out.wav
```

The result is at `out.wav`.
The `offline-pipeline` outputs simple statistics about the processor.

See `offline-pipeline --help` for more information.

## Running on a ChromiumOS device

The `offline-pipeline` tool is available on test images.

Deploy the shared library to a executable partition then pass the path to
`offline-pipeline`.

TODO: provide official guidelines on cross-compiling for ChromiumOS.

## Known issues

*   The `offline-pipeline` currently assumes that the input and output have the
    same frame rate and number of channels.
