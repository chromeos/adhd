#!/usr/bin/python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# usage: add_tlv_header_blob [in_file] [out_file]
# args:
#    in_file - the filepath for input binary
#    out_file - the filepath for output binary
#
# Add TLV header bytes for the input binary and push to output, which can be
# used as the parameter binfiles by UCM config.
# The input binary could be generated from SOF tuning tools, reference:
# https://thesofproject.github.io/latest/developer_guides/tuning/sof-ctl.html
from __future__ import print_function

import struct
import sys


SOF_CTRL_CMD_BINARY = 3


def add_tlv_header_blob(file_in, file_out: str):
    with open(file_in, 'rb') as f:
        data = f.read()

    data_size = len(data)
    print('Read input binary: %d bytes' % data_size)

    with open(file_out, 'wb') as f:
        # write tag SOF_CTRL_CMD_BINARY (4-byte)
        f.write(struct.pack('<I', SOF_CTRL_CMD_BINARY))

        # write tlv size (4-byte)
        f.write(struct.pack('<I', data_size))

        # write data
        f.write(data)


def main():
    if len(sys.argv) < 3:
        print('Error: Insufficient arguments')
        print('       usage: add_tlv_header_blob.py in_file out_file')
        return -1

    add_tlv_header_blob(sys.argv[1], sys.argv[2])
    return 0


if __name__ == "__main__":
    main()
