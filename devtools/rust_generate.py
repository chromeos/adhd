#!/usr/bin/python3
# # Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import pathlib
import shlex
import subprocess
import time


adhd = pathlib.Path(__file__).resolve().parents[1]


def run(cmd, env=None, **kwargs):
    print('+', shlex.join(cmd))
    return subprocess.run(
        cmd, check=True, cwd=adhd, env={**os.environ, **env} if env else None, **kwargs
    )


t0 = time.perf_counter()

run(['bazel', 'sync', '--only=crate_index'], env={'CARGO_BAZEL_REPIN': 'true'})
result = run(
    ['bazel', 'query', 'kind(write_source_file, //...)'], stdout=subprocess.PIPE, text=True
)
targets = result.stdout.split()
for target in targets:
    run(['bazel', 'run', target])

t1 = time.perf_counter()
print(f'Done in {t1-t0:.2f} seconds')
