#!/usr/bin/python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Generate cras_dbus_bindings.h from DBus XML files
"""

import os
import sys


def generate_file_to_c_string(path):
    """
    similiar to `xxd -i`
    """
    string_name = os.path.basename(path).replace('.', '_')
    yield f'static const char {string_name}[] = {{\n'

    with open(path, 'rb') as file:
        buf = memoryview(bytearray(12))

        while True:
            n = file.readinto(buf)
            if not n:
                break

            yield ' '
            for byte in buf[:n]:
                yield f' 0x{byte:02x},'

            yield '\n'

    yield '  0x00};\n'


def generate(files):
    yield '''\
#ifndef CRAS_DBUS_BINDINGS_H_
#define CRAS_DBUS_BINDINGS_H_
'''

    for path in files:
        yield from generate_file_to_c_string(path)

    yield '#endif /* CRAS_DBUS_BINDINGS_H_ */\n'


sys.stdout.write(''.join(generate(sys.argv[1:])))
