#!/usr/bin/env vpython3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script lints the D-Bus introspect XML files in CRAS.


[VPYTHON:BEGIN]
python_version: "3.8"

wheel: <
  name: "infra/python/wheels/lxml/linux-amd64_cp38_cp38"
  version: "version:4.6.2"
>
[VPYTHON:END]
"""

import argparse
import fnmatch
import itertools
import os
import sys
from typing import List, NamedTuple, Optional

import lxml.etree


here = os.path.dirname(os.path.abspath(__file__))


class Error(NamedTuple):
    filename: str
    lineno: int
    message: str

    def __str__(self):
        return f'{self.filename}:{self.lineno}: {self.message}'


def dtd_validation(root, dtd):
    """
    DTD validation is the same as the xmllint command from libxml2:

        xmllint --noout --nonet --dtdvalid DTD.dtd PATH.xml
    """

    dtd.validate(root)
    for e in dtd.error_log:
        yield Error(e.filename, e.line, e.message)


def check_arg_direction(root, filename):
    """
    Check method arguments include the "direction" attribute
    and signal arguments don't

    https://github.com/freedesktop/dbus/blob/dbus-1.13.18/doc/introspect.dtd#L22-L24
    """

    for arg in root.xpath('//method/arg[not(@direction)]'):
        yield Error(filename, arg.sourceline, 'method <arg> should set direction attribute')

    for attr in root.xpath('//signal/arg/@direction'):
        arg = attr.getparent()
        yield Error(
            filename,
            arg.sourceline,
            'signal <arg> should not set direction attribute',
        )


def check_node_id_type(root, filename):
    """
    Check <arg name="node_id" type="t">
    """

    for arg in root.xpath('//arg[@name="node_id" and not(@type="t")]'):
        type_ = arg.attrib['type']
        yield Error(
            filename,
            arg.sourceline,
            f'<arg name="node_id"> must have type="t", got type="{type_}" instead',
        )


def lint(dtdfile: str, files: List[str], ignore_non_dbus_xml_files: bool) -> int:
    if not files:
        print('No files to check.')
        return 0

    if ignore_non_dbus_xml_files:
        files = [
            file
            for file in files
            if fnmatch.fnmatch(os.path.basename(file), 'org.chromium.cras.*.xml')
        ]

    with open(dtdfile) as f:
        dtd = lxml.etree.DTD(f)

    error_count = 0

    for filename in files:
        parser = lxml.etree.XMLParser(
            attribute_defaults=False,
            no_network=True,
        )
        try:
            root = lxml.etree.parse(filename, parser)
        except lxml.etree.XMLSyntaxError as e:
            print(f'{e.filename}:{e.lineno}:{e.offset}: {e.msg}')
            error_count += 1
        else:
            for error in itertools.chain(
                dtd_validation(root, dtd),
                check_arg_direction(root, filename),
                check_node_id_type(root, filename),
            ):
                print(error)
                error_count += 1

    return 1 if error_count else 0


def main():
    ap = argparse.ArgumentParser(
        allow_abbrev=False,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument('--dtd', default=os.path.join(here, 'introspect.dtd'), help='')
    ap.add_argument('files', nargs='*', help='xml file(s) to validate')
    ap.add_argument(
        '--ignore-non-dbus-xml-files',
        action='store_true',
        help='indicates this script is being called by the preupload hook.',
    )
    args = ap.parse_args()
    sys.exit(
        lint(
            dtdfile=args.dtd,
            files=args.files,
            ignore_non_dbus_xml_files=args.ignore_non_dbus_xml_files,
        )
    )


if __name__ == '__main__':
    main()
