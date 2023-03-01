# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Resolve a -l{library_name} to its path as /path/to/library.a"""

import os
import shlex
import subprocess
import sys


def get_static_lib_from_line(line):
    """Parses something like:
    /home/aaronyu/adhd/cras/src/server/rust/target/debug/libcras_rust.a(cras_rust-c20885aed599b1b7.12ancxnhxc54e6ck.rcgu.o)

    Returns None if it does not look like a static library.
    """

    path, found, rem = line.rpartition('(')
    if not found:
        return None
    assert rem.endswith(')'), line
    return path


def main(output_path, compiler, link_flags_file, library_name):
    with open(link_flags_file) as file:
        link_flags = file.read().split()

    cmd = [
        compiler,
        *link_flags,
        '-shared',
        '-nostdlib',
        '-Wl,-trace',
        '-Wl,-whole-archive',
        '-o/dev/null',
        f'-l:lib{library_name}.a',
    ]
    pretty_cmd = ' '.join(map(shlex.quote, cmd))
    stdout = subprocess.check_output(cmd, encoding='utf-8')

    libs = set()
    for line in stdout.splitlines():
        path = get_static_lib_from_line(line)
        if path is None:
            continue
        if path.endswith(f'/lib{library_name}.a'):
            libs.add(path)

    if len(libs) > 1:
        raise Exception(f'Multiple libraries returned for {pretty_cmd}:{libs}')
    if not libs:
        raise Exception(f'No libraries found for {pretty_cmd}')

    os.symlink(libs.pop(), output_path)


if __name__ == '__main__':
    output_path, compiler, link_flags_file, library_name = sys.argv[1:]
    main(output_path, compiler, link_flags_file, library_name)
