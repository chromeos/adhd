# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil


def run(*, prefix, _bin_files, _extra_bin_files, _include_files, _lib_files, _alsa_lib_files):
    for name, paths in [
        ('bin', _bin_files),
        ('extra_bin', _extra_bin_files),
        ('include', _include_files),
        ('lib', _lib_files),
        ('alsa-lib', _alsa_lib_files),
    ]:
        dir = os.path.join(prefix, name)
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

    parser.add_argument('--_bin_files', nargs='+', default=())
    parser.add_argument('--_extra_bin_files', nargs='+', default=())
    parser.add_argument('--_include_files', nargs='+', default=())
    parser.add_argument('--_lib_files', nargs='+', default=())
    parser.add_argument('--_alsa_lib_files', nargs='+', default=())
    parser.add_argument('prefix', help='absolute path to copy files to', type=abspath)

    run(**vars(parser.parse_args()))


if __name__ == '__main__':
    main()
