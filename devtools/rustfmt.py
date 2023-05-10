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
import hashlib
import io
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import urllib.request


parser = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
parser.add_argument('--check', action='store_true')
parser.add_argument('files', nargs='*')
args = parser.parse_args()


def get_rustfmt():
    url = 'https://github.com/rust-lang/rustfmt/releases/download/v1.5.1/rustfmt_linux-x86_64_v1.5.1.tar.gz'
    sha256 = '25c8e88d2599aff00aaddd377fc76eb3a5602abc99680aeeaec3b39ce6f782d0'
    name = 'rustfmt_linux-x86_64_v1.5.1/rustfmt'

    cachedir = os.path.expanduser('~/.cache/adhd-devtools/rustfmt')
    os.makedirs(cachedir, exist_ok=True)
    rustfmt = os.path.join(cachedir, sha256)
    if not os.path.exists(rustfmt):
        print(f'Downloading {url}...', file=sys.stderr)
        with urllib.request.urlopen(url) as archive:
            tgz = archive.read()
        with tarfile.open(fileobj=io.BytesIO(tgz), mode='r:gz') as tf:
            b = tf.extractfile(tf.getmember(name)).read()
            assert hashlib.sha256(b).hexdigest() == sha256
            with tempfile.TemporaryDirectory(dir=cachedir) as d:
                temp_rustfmt = os.path.join(d, sha256)
                with open(temp_rustfmt, mode='wb') as w:
                    w.write(b)
                    os.fchmod(w.fileno(), 0o755)
                os.rename(temp_rustfmt, rustfmt)
    return rustfmt


files = [f for f in args.files if f.endswith('.rs') and os.path.exists(f)]
if not files:
    raise SystemExit(0)
cmd = [get_rustfmt(), '--unstable-features', '--edition=2018']
if args.check:
    cmd.extend(('--check', '--files-with-diff'))
cmd.append('--')
cmd.extend(files)
result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)
if args.check and result.returncode:
    format_with = shlex.join([os.path.abspath(__file__), *result.stdout.splitlines()])
    raise SystemExit(f'Rust files are not formatted! Format with:\n{format_with}')
raise SystemExit(result.returncode)
