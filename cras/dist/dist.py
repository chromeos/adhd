# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import typing


class Target(typing.NamedTuple):
    name: str  # The name of the target directory.
    flag: str  # The name of the corresponding flag.


TARGETS = [
    Target('bin', '_bin_files'),
    Target('extra_bin', '_extra_bin_files'),
    Target('include', '_include_files'),
    Target('lib', '_lib_files'),
    Target('alsa-lib', '_alsa_lib_files'),
    Target('fuzzer', '_fuzzer_files'),
    Target('pkgconfig', '_pkgconfig_files'),
]


def run(opts):
    prefix = opts.prefix
    for name, flag in TARGETS:
        dir = os.path.join(prefix, name)
        paths = getattr(opts, flag)
        print(f'Destination: {dir!r}')
        os.makedirs(dir, exist_ok=True)
        for path in paths:
            dest = os.path.join(dir, os.path.basename(path))
            print(f'Copying {path!r}')
            if os.path.exists(dest):
                # A previous invocation could install a read-only file
                # Remove it first to avoid errors with copy2().
                os.remove(dest)
            shutil.copy2(path, dest)


def abspath(s: str) -> str:
    if not os.path.isabs(s):
        raise ValueError(f'{s} is not an absolute path!')
    return s


def main():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument('prefix', help='absolute path to copy files to', type=abspath)
    for _, flag in TARGETS:
        parser.add_argument(f'--{flag}', nargs='+', default=())

    run(parser.parse_args())


if __name__ == '__main__':
    main()
