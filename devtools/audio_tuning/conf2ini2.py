#!/usr/bin/python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# usage: conf2ini2.py [audio.conf file]
#
# Convert audio.conf from the audio tuning UI to dsp.ini which can be
# accepted by cras eq/drc plugin.
from __future__ import print_function

import sys

import conf2ini


def main():
    if len(sys.argv) < 2:
        print('Error: audio.conf file is not specified')
        print('       usage: conf2ini2.py [audio.conf file]')
        return -1

    if len(sys.argv) > 2:
        print(
            'Error: only one audio.conf file is accepted; use conf2ini.py for '
            'multiple file cases'
        )
        print('       usage: conf2ini.py [audio.conf file]...')
        return -1

    conf2ini.conf2ini(sys.argv[1:])
    return 0


if __name__ == "__main__":
    main()
