# CRAS Dependency Analysis

## Usage

```
devtools/dependency-analysis/run.bash
```

Currently this prints the dependenecy information as JSON to stdout.

Actual analysis TODO.

## How

This tool does the following steps to collect information from the dependency graph.

1.  Do a full build to produce the object files and [depfile]s.
2.  Use [`bazel aquery`] to query the action graph. The output is a [ActionGraphContainer] protobuf.
3.  Inspect the action graph to figure out the headers declared in the BUILD.bazel graph,
    and inspect the depfiles to figure out the headers that are actually needed to compile each translation unit.
4.  Inspect the object files using the [elf package] to figure out the symbols provided & required by each translation unit.

[`bazel aquery`]: https://bazel.build/query/aquery
[depfile]: https://github.com/bazelbuild/bazel/blob/master/src/main/protobuf/analysis_v2.proto
[ActionGraphContainer]: https://github.com/bazelbuild/bazel/blob/6a4d61ebf2dc2214ffe6abc807e4f597175e1a6c/src/main/protobuf/analysis_v2.proto#L25
[elf package]: https://pkg.go.dev/debug/elf

## Ideas

*   Plot the dependency graph.
*   Suggest leaf modules / translation units for Rustification.
*   Flag stubbed functions declared in unit tests. We should replace them with real ones or fakes.
    See also [SWE Book on test doubles](https://abseil.io/resources/swe-book/html/ch13.html#tlsemicolondrs-id00116).
*   Remove unneeded dependencies.
    If a build target X declares a dependency on Y, but does not actually need headers or symbols
    provided by Y, automatically remove Y from X's `cc_library(deps = ...)`.

## Examples

Print all information:

```
devtools/dependency-analysis/run.bash
```

```
[
  {
    "target_label": "//cras/src/server:libcrasserver",
    "source": "cras/src/server/cras_tm.c",
    "inputs_bazel": [
      "cras/src/server/cras_tm.c",
      "cras/src/server/cras_tm.h",
      "cras/include/cras_util.h",
      "cras/include/cras_types.h",
      "cras/include/cras_audio_format.h",
      "cras/include/cras_iodev_info.h",
      "cras/include/cras_timespec.h",
      "cras/include/packet_status_logger.h",
      "third_party/utlist/utlist.h"
    ],
    "inputs_makefile": [
      "cras/src/server/cras_tm.c",
      "cras/src/server/cras_tm.h",
      "cras/include/cras_util.h",
      "cras/include/cras_types.h",
      "cras/include/cras_audio_format.h",
      "cras/include/cras_iodev_info.h",
      "cras/include/cras_timespec.h",
      "cras/include/packet_status_logger.h",
      "third_party/utlist/utlist.h"
    ],
    "provided_symbols": [
      "cras_tm_create_timer",
      "cras_tm_cancel_timer",
      "cras_tm_init",
      "cras_tm_deinit",
      "cras_tm_get_next_timeout",
      "cras_tm_call_callbacks"
    ],
    "dependant_symbols": [
      "calloc",
      "clock_gettime",
      "__assert_fail",
      "free"
    ]
  },
  ...
]
```
