#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
rustfmt wrapper to support our repo hook.
"""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# [VPYTHON:END]

import argparse
import os
import shlex
import subprocess


parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
parser.add_argument('--check', action='store_true')
parser.add_argument('files', nargs='*')
args = parser.parse_args()


files = [f for f in args.files if f.endswith('.rs') and os.path.exists(f)]
if not files:
    raise SystemExit(0)
cmd = ['rustfmt', '+nightly', '--edition=2018']
if args.check:
    cmd.extend(('--check', '--files-with-diff'))
cmd.append('--')
cmd.extend(files)
result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)
if args.check and result.returncode:
    format_with = shlex.join([os.path.abspath(__file__), *result.stdout.splitlines()])
    raise SystemExit(f'Rust files are not formatted! Format with:\n{format_with}')
raise SystemExit(result.returncode)
