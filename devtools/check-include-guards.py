#!/usr/bin/env vpython3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Check for include guards.

[VPYTHON:BEGIN]
python_version: "3.8"
[VPYTHON:END]
"""

# TODO: This script checks for the files on the live file system, instead
# of the version recorded by git. Ideally we should follow _get_file_content
# in https://chromium.googlesource.com/chromiumos/repohooks/+/refs/heads/main/pre-upload.py
# instead.

import argparse
import re
import string
import sys


fail = False


parser = argparse.ArgumentParser()
parser.add_argument('--fix', action='store_true')
parser.add_argument('files', nargs='*')
args = parser.parse_args()


for fn in args.files:
    if not fn.endswith('.h'):
        continue

    try:
        file = open(fn)
    except FileNotFoundError:
        continue
    with file:
        guard_name = (
            ''.join(c if c in string.ascii_uppercase + string.digits else '_' for c in fn.upper())
            + '_'
        )
        guard = f'#ifndef {guard_name}\n#define {guard_name}\n'
        content = file.read()
        if guard not in content:
            if args.fix:
                if m := re.search(r'#ifndef ([\w_]+_H_)\n#define \1\n', content):
                    with open(fn, 'w') as file:
                        file.write(content.replace(m.group(1), guard_name))
                        continue
            print(fn, 'is lacking the include guards:')
            print(guard)
            fail = True


if fail:
    sys.exit(1)
