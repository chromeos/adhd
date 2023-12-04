# CRAS Benchmark

## Build instructions

1.  Install dependencies:

    1.  Bazelisk: https://github.com/bazelbuild/bazelisk
    1.  System dependencies:
        1.  alsa
        1.  libevent
        1.  protobuf

1.  Clone this repository and the webrtc-apm repository, so they are adjacent to each other.

    ```
    git clone https://chromium.googlesource.com/chromiumos/third_party/adhd
    git clone https://chromium.googlesource.com/chromiumos/third_party/webrtc-apm
    ```

1.  Build `cras_bench` in the `adhd` directory:

    ```
    cd adhd
    bazelisk build //cras/benchmark:cras_bench --//:apm -c opt --action_env=CC=clang --action_env=CXX=clang++ --copt=-march=x86-64-v3
    ```

    Notable options:

    *   `-c opt` - enables optimized builds
    *   `--action_env=CC=clang` `--action_env=CXX=clang++` - sets the C and C++ compilers to clang
    *   `--copt` - sets C/C++ compiler options. You can add more with more `--copt=...`
    *   `--copt=-march=x86-64-v3` - sets the target architecture. You should use the best one that fits your system

    The resulting binary will be in `bazel-bin/cras/benchmark/cras_bench`.

## Run instructions

Run with

```
bazel-bin/cras/benchmark/cras_bench --benchmark_out=result.json
```

to get a JSON file with the results at `result.json`.

## Editor note

Run `bazel cquery --//:apm 'filter("^@pkg_config", deps(//cras/benchmark:cras_bench))'`
to query up-to-date system dependencies.
