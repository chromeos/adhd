# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _fuzz_engine_repository(repository_ctx):
    lib_fuzzing_engine = repository_ctx.os.environ.get("LIB_FUZZING_ENGINE")
    srcs = []
    linkopts = []

    if lib_fuzzing_engine == None:
        linkopts.append("-fsanitize=fuzzer")
    elif lib_fuzzing_engine.startswith("-"):
        # link flags, e.g. -fsanitize=fuzzer
        linkopts.append(lib_fuzzing_engine)
    elif lib_fuzzing_engine.endswith(".a"):
        # library archives, e.g. /usr/lib/libFuzzingEngine.a
        path = repository_ctx.path(lib_fuzzing_engine)
        repository_ctx.symlink(
            path,
            path.basename,
        )
        srcs.append(path.basename)
    else:
        fail("Unknown $LIB_FUZZING_ENGINE value {}".format(repr(lib_fuzzing_engine)))

    repository_ctx.template(
        "BUILD.bazel",
        repository_ctx.path(Label("//:fuzz_engine.BUILD.tpl")),
        {
            "@name@": repr(repository_ctx.name),
            "@srcs@": repr(srcs),
            "@linkopts@": repr(linkopts),
        },
        executable = False,
    )

fuzz_engine_repository = repository_rule(
    implementation = _fuzz_engine_repository,
    environ = [
        "LIB_FUZZING_ENGINE",
    ],
    local = True,
    doc = """
Generates a repository to be added as a dep for cc_binary() fuzzers.

If LIB_FUZZING_ENGINE is set, it is picked up.
Otherwise defaults to -fsanitize=fuzzer.
""",
)
