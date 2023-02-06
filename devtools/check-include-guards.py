#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO: This script checks for the files on the live file system, instead
# of the version recorded by git. Ideally we should follow _get_file_content
# in https://chromium.googlesource.com/chromiumos/repohooks/+/refs/heads/main/pre-upload.py
# instead.

import os
import string
import sys


fail = False


for fn in sys.argv[1:]:
    if not fn.endswith('.h'):
        continue

    with open(fn) as file:
        guard_name = (
            ''.join(
                c if c in string.ascii_uppercase + string.digits else '_'
                for c in os.path.basename(fn).upper()
            )
            + '_'
        )
        guard = f'#ifndef {guard_name}\n#define {guard_name}\n'
        if guard not in file.read():
            print(fn, 'is lacking the include guards:')
            print(guard)
            fail = True


if fail:
    sys.exit(1)
