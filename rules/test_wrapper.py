#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs the passed command and prints its exit status"""

import shlex
import signal
import subprocess
import sys


def exit_status_name(status):
    if status >= 0:
        return status
    try:
        sig = signal.Signals(-status)
    except ValueError:
        return status
    return f'{status} ({sig.name})'


def main():
    status = subprocess.call(sys.argv[1:])
    print('-' * 80)
    print(shlex.join(sys.argv[1:]), 'exited with status', exit_status_name(status))
    sys.exit(status)


if __name__ == '__main__':
    main()
