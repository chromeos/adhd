#!/usr/bin/python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# usage: conf2ini [audio.conf file]...
#
# Convert one or more audio.conf file(s) from the audio tuning UI to dsp.ini
# which can be accepted by cras eq/drc plugin.
#
# Audio stream in 2*(number of audio.conf files) channels is expected by the
# generated dsp.ini whose eq/drc configuration is specified per 2 channels.
from __future__ import print_function

import fnmatch
import json
import sys
from typing import List


biquad_type_name = [
    "none",
    "lowpass",
    "highpass",
    "bandpass",
    "lowshelf",
    "highshelf",
    "peaking",
    "notch",
    "allpass",
]

source_header = """\
[output_source]
library=builtin
label=source
purpose=playback
disable=(not (equal? dsp_name "speaker_eq"))"""

output_port = 'output_%d={%s:%d}'

sink_header = """\
[output_sink]
library=builtin
label=sink
purpose=playback"""

input_port = 'input_%d={%s:%d}'

drc_header_fmt = """\
[drc{suffix}]
library=builtin
label=drc
input_0={{{in_name}:{ch1:d}}}
input_1={{{in_name}:{ch2:d}}}
output_2={{{out_name}:{ch1:d}}}
output_3={{{out_name}:{ch2:d}}}
input_4={emphasis_disabled:<7d}   ; emphasis_disabled"""

drc_param = """\
input_%d=%-7g   ; f
input_%d=%-7g   ; enable
input_%d=%-7g   ; threshold
input_%d=%-7g   ; knee
input_%d=%-7g   ; ratio
input_%d=%-7g   ; attack
input_%d=%-7g   ; release
input_%d=%-7g   ; boost"""

eq_header_fmt = """\
[eq2{suffix}]
library=builtin
label=eq2
input_0={{{in_name}:{ch1:d}}}
input_1={{{in_name}:{ch2:d}}}
output_2={{{out_name}:{ch1:d}}}
output_3={{{out_name}:{ch2:d}}}"""

eq_param = """\
input_%d=%-7d ; %s
input_%d=%-7g ; freq
input_%d=%-7g ; Q
input_%d=%-7g ; gain"""


def is_true(conf, pattern: str) -> bool:
    for k in conf:
        if fnmatch.fnmatch(k, pattern) and conf[k]:
            return True
    return False


def intermediate_name(index: int) -> str:
    return 'intermediate' + ('' if index == 1 else str(index))


def conf2ini(filenames: List[str]):
    confs = []

    # read and parse audio.conf file(s)
    for filename in filenames:
        with open(filename) as f:
            confs.append(json.loads(f.read()))

    num_conf = len(confs)

    # print source
    print(source_header)
    for i in range(2 * num_conf):
        print(output_port % (i, 'src', i))

    # print sink
    print('')
    print(sink_header)
    for i in range(2 * num_conf):
        print(input_port % (i, 'dst', i))

    # print eq/drc for each audio.conf file(s)
    for i, conf in enumerate(confs):
        has_drc = is_true(conf, 'global.enable_drc') and is_true(conf, 'drc.*.enable')
        has_eq = is_true(conf, 'global.enable_eq') and is_true(conf, 'eq.*.*.enable')

        stages = []
        if has_drc:
            stages.append(print_drc)
        if has_eq:
            stages.append(print_eq)

        if is_true(conf, 'global.enable_swap') and len(stages) >= 2:
            stages[0], stages[1] = stages[1], stages[0]

        for j, stage in enumerate(stages):
            print('')
            suffix = ('_%d' % (i + 1)) if num_conf > 1 else ''
            src = 'src' if j == 0 else intermediate_name(j)
            dst = 'dst' if j == len(stages) - 1 else intermediate_name(j + 1)
            stage(conf, suffix, i, src, dst)


def print_drc(conf, suffix: str, index: int, src: str, dst: str):
    print(
        drc_header_fmt.format(
            suffix=suffix,
            in_name=src,
            out_name=dst,
            ch1=2 * index,
            ch2=2 * index + 1,
            emphasis_disabled=int(conf['drc.emphasis_disabled']),
        )
    )
    n = 5
    for i in range(3):
        prefix = 'drc.%d.' % i
        f = conf[prefix + 'f']
        enable = int(conf[prefix + 'enable'])
        threshold = conf[prefix + 'threshold']
        knee = conf[prefix + 'knee']
        ratio = conf[prefix + 'ratio']
        attack = conf[prefix + 'attack']
        release = conf[prefix + 'release']
        boost = conf[prefix + 'boost']

        print(
            drc_param
            % (
                n,
                f,
                n + 1,
                enable,
                n + 2,
                threshold,
                n + 3,
                knee,
                n + 4,
                ratio,
                n + 5,
                attack,
                n + 6,
                release,
                n + 7,
                boost,
            )
        )
        n += 8


# Returns two sorted lists, each one contains the enabled eq index for a channel
def enabled_eq(conf) -> List[List[int]]:
    eeq = [[], []]
    for k in conf:
        s = k.split('.')
        if s[0] == 'eq' and s[3] == 'enable' and conf[k]:
            ch_index = int(s[1])
            eq_num = int(s[2])
            eeq[ch_index].append(eq_num)
    return sorted(eeq[0]), sorted(eeq[1])


def print_eq(conf, suffix: str, index: int, src: str, dst: str):
    print(
        eq_header_fmt.format(
            suffix=suffix, in_name=src, out_name=dst, ch1=2 * index, ch2=2 * index + 1
        )
    )

    eeq = enabled_eq(conf)
    eeqn = max(len(eeq[0]), len(eeq[1]))
    n = 4  # the first in index
    for i in range(0, eeqn):
        for ch in (0, 1):
            if i < len(eeq[ch]):
                prefix = 'eq.%d.%d.' % (ch, eeq[ch][i])
                type_name = conf[prefix + 'type']
                type_index = biquad_type_name.index(type_name)
                f = conf[prefix + 'freq']
                q = conf[prefix + 'q']
                g = conf[prefix + 'gain']
            else:
                type_name = 'none'
                type_index = 0
                f = q = g = 0
            print(eq_param % (n, type_index, type_name, n + 1, f, n + 2, q, n + 3, g))
            n += 4


def main():
    if len(sys.argv) < 2:
        print('Error: at least one audio.conf file should be specified')
        print('       usage: conf2ini.py [audio.conf file]...')
        return -1

    conf2ini(sys.argv[1:])
    return 0


if __name__ == "__main__":
    main()
