#!/usr/bin/python3
# # Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import pathlib
import shlex
import subprocess
import time


here = pathlib.Path(__file__).resolve()
adhd = pathlib.Path(__file__).resolve().parents[1]


def run(cmd, env=None, **kwargs):
    print('+', shlex.join(cmd))
    return subprocess.run(
        cmd, check=True, cwd=adhd, env={**os.environ, **env} if env else None, **kwargs
    )


t0 = time.perf_counter()

# Update Cargo.Bazel.lock.
run(
    ['bazel', 'build', '--nobuild', '//cras/...'],
    env={'CARGO_BAZEL_REPIN': 'true'},
)

# Find files to generate.
result = run(
    ['bazel', 'cquery', 'kind(write_source_file, //cras/...)', '--output=starlark'],
    stdout=subprocess.PIPE,
    text=True,
)
targets = sorted(
    set(x for x in result.stdout.split() if x != '@//build/write_source_files:write_source_files')
)
targets_string = json.dumps(targets, indent=4).replace('"\n', '",\n')
(adhd / 'build/write_source_files/write_source_file_targets_generated.bzl').write_text(
    f'''# # Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generated with {here.relative_to(adhd)}

WRITE_SOURCE_FILE_TARGETS = {targets_string}
''',
    encoding='utf-8',
)

run(['bazel', 'run', '//build/write_source_files'])

t1 = time.perf_counter()
print(f'Done in {t1-t0:.2f} seconds')
