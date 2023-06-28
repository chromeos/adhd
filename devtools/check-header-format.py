#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
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
import datetime
import enum
import os
import re
import string
import sys


class ChunkType(enum.IntEnum):
    EMPTY = 0
    LICENSE = 1
    INCLUDE_GUARD = 2
    CPP_EXTERN_C = 3
    INCLUDE = 4
    IF = 5
    ENDIF = 6
    OTHER = 7


CHUNK_RE = re.compile(
    r'(^#ifndef ([\w_]+_HH?_)\n#define \2\n)|(^#ifdef __cplusplus\nextern "C" {\n#endif\n)|(^#include .+\n)|(^(?:#if|#ifdef|#ifndef) [^\n]+\n)|(^#endif[^\n]*\n)|(^\n+)',
    re.MULTILINE,
)

INCLUDE_GUARD_RE = re.compile(r'#ifndef ([\w_]+_HH?_)\n#define \1\n')


def get_chunks(content):
    it = iter(CHUNK_RE.split(content.rstrip('\n') + '\n'))
    for chunk in it:
        if chunk.startswith(('// Copyright', '/* Copyright', '/*\n * Copyright')):
            yield (ChunkType.LICENSE, chunk)
        elif chunk:
            yield (ChunkType.OTHER, chunk)

        try:
            include_guard = next(it)
        except StopIteration:
            return
        _include_guard_name = next(it)
        cpp_extern_c = next(it)
        include = next(it)
        if_macro = next(it)
        endif_macro = next(it)
        empty_line = next(it)
        if include_guard is not None:
            yield (ChunkType.INCLUDE_GUARD, include_guard)
        elif cpp_extern_c is not None:
            yield (ChunkType.CPP_EXTERN_C, cpp_extern_c)
        elif include is not None:
            yield (ChunkType.INCLUDE, include)
        elif if_macro is not None:
            yield (ChunkType.IF, if_macro)
        elif endif_macro is not None:
            yield (ChunkType.ENDIF, endif_macro)
        elif empty_line is not None:
            yield (ChunkType.EMPTY, empty_line)
        else:
            assert False


def make_license_header():
    return f'''// Copyright {datetime.date.today().year} The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
'''


def is_cpp_header(path, content):
    return path.endswith('.hh')


def check(path, content):
    guard_name = (
        ''.join(c if c in string.ascii_uppercase + string.digits else '_' for c in path.upper())
        + '_'
    )
    errors = []

    chunks = list(get_chunks(content))
    chunk_types, chunk_values = zip(*chunks)
    chunk_types = bytearray(chunk_types)
    chunk_values = list(chunk_values)

    def insert(index, ty, val):
        chunk_types.insert(index, ty)
        chunk_values.insert(index, val)

    def append(ty, val):
        chunk_types.append(ty)
        chunk_values.append(val)

    def insert_line_if_not_empty(i, offset=0):
        if len(chunk_types) == i or chunk_types[i] != ChunkType.EMPTY:
            insert(i + offset, ChunkType.EMPTY, '\n')
            return True
        return False

    def append_line_if_not_empty():
        if not chunk_types.endswith(bytes([ChunkType.EMPTY])):
            append(ChunkType.EMPTY, '\n')

    if not chunk_types.startswith(bytes([ChunkType.LICENSE])):
        errors.append('missing license header')
        insert(0, ChunkType.LICENSE, make_license_header())
        insert_line_if_not_empty(1)

    if (
        not is_cpp_header(path, content)
        and ChunkType.OTHER in chunk_types
        and not ChunkType.CPP_EXTERN_C in chunk_types
    ):
        errors.append('missing extern "C"')

        # Insert after #include or include guard.
        i = 0
        for ty in [ChunkType.INCLUDE, ChunkType.INCLUDE_GUARD]:
            try:
                i = max(i, chunk_types.rindex(ty))
            except ValueError:
                pass

        insert_line_if_not_empty(i + 1)
        insert(i + 2, ChunkType.CPP_EXTERN_C, '#ifdef __cplusplus\nextern "C" {\n#endif\n')
        insert_line_if_not_empty(i + 3)

        # Map out the if level.
        if_levels = [0]
        last_if_level = 0
        for ty in chunk_types:
            if ty in (ChunkType.INCLUDE_GUARD, ChunkType.IF):
                last_if_level += 1
            elif ty == ChunkType.ENDIF:
                last_if_level -= 1
            if_levels.append(last_if_level)

        # Insert at the same #if level.
        i = len(chunk_types) - if_levels[::-1].index(if_levels[i + 2])
        i += insert_line_if_not_empty(i - 1, offset=1)
        insert(
            i,
            ChunkType.OTHER,
            '''#ifdef __cplusplus
}  // extern "C"
#endif
''',
        )
        insert_line_if_not_empty(i + 1)

    expected_guard_text = f'#ifndef {guard_name}\n#define {guard_name}\n'
    if not ChunkType.INCLUDE_GUARD in chunk_types:
        errors.append('missing include guard')
        # Chunk 0 is guaranteed to be LICENSE.
        insert_line_if_not_empty(1)
        insert(2, ChunkType.INCLUDE_GUARD, expected_guard_text)
        insert_line_if_not_empty(3)
        append_line_if_not_empty()
        append(ChunkType.OTHER, f'#endif  // {guard_name}\n')
    else:
        # Check that the include guard is valid.
        i = chunk_types.index(ChunkType.INCLUDE_GUARD)
        m = INCLUDE_GUARD_RE.match(chunk_values[i])
        if m.group(1) != guard_name:
            errors.append('incorrect include guard text')
            chunk_values[i] = expected_guard_text

    return errors, ''.join(chunk_values)


def main(files, fix):
    broken = []

    for fn in files:
        if not fn.endswith(('.h', '.hh')):
            continue
        if fn.startswith('third_party/'):
            continue

        try:
            file = open(fn)
        except FileNotFoundError:
            continue
        with file:
            content = file.read()
        errors, fixed = check(fn, content)
        if errors:
            print('{}: {}'.format(fn, ', '.join(errors)))
            broken.append(fn)
        if fix:
            with open(fn, 'w') as file:
                file.write(fixed)

    if broken and not fix:
        print(f'To fix, run: {sys.argv[0]} --fix {" ".join(broken)}')

    return bool(broken)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fix', action='store_true')
    parser.add_argument('files', nargs='*')
    args = parser.parse_args()
    main(args.files, args.fix)
