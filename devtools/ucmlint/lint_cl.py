#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import http.client
import json
import logging
import os
import re
import subprocess
import tempfile
from typing import Optional, Union
import urllib.parse
import urllib.request

import ucmlint


GERRIT_HOST = 'https://chromium-review.googlesource.com/'

# Gerrit response magic prefix
# https://gerrit-review.googlesource.com/Documentation/rest-api.html#output
MAGIC_PREFIX = b")]}'\n"
HTTP_URL = 'https://chromium.googlesource.com/chromiumos/overlays/board-overlays'


class Oops(Exception):
    """Exception class that does not need to be handled"""


def gerrit_request(url: Union[str, urllib.request.Request], want_status=200) -> dict:
    if isinstance(url, str):
        url = urllib.parse.urljoin(GERRIT_HOST, url)
    response: http.client.HTTPResponse
    with urllib.request.urlopen(url) as response:
        if response.status != want_status:
            raise Oops(f'HTTP {response.status} {url} (want {want_status})')

        prefix = response.read(len(MAGIC_PREFIX))
        if prefix != MAGIC_PREFIX:
            raise Oops(
                f'HTTP Response body does not start with {MAGIC_PREFIX!r}. '
                f'Found {prefix!r} instead. '
                f'URL: {url}'
            )
        return json.load(response)


def main(change_id: int, work_dir: Optional[str]):
    response = gerrit_request(f'/changes/{change_id}?o=CURRENT_REVISION&o=CURRENT_FILES')
    revision_sha1 = response['current_revision']
    revision = response['revisions'][revision_sha1]
    http_url = revision['fetch']['http']['url']
    assert http_url == HTTP_URL, http_url
    ref = revision['fetch']['http']['ref']
    rev_num = revision['_number']

    with contextlib.ExitStack() as stack:
        need_clone = False
        if work_dir is None:
            work_dir = stack.push(tempfile.TemporaryDirectory(prefix=f'CL-{change_id}-')).name
            need_clone = True
        elif not os.path.exists(work_dir):
            need_clone = True
        if need_clone:
            subprocess.check_call(['git', 'clone', http_url, work_dir])
        subprocess.check_call(['git', 'fetch', http_url, ref], cwd=work_dir)
        subprocess.check_call(['git', 'checkout', 'FETCH_HEAD'], cwd=work_dir)
        files = subprocess.check_output(
            ['git', 'diff', '-z', '--name-only', 'HEAD~', 'HEAD'],
            cwd=work_dir,
            text=True,
        ).split('\0')

        dirs_to_lint = set()
        for file in files:
            if m := re.match(r'^(.+/audio/ucm-config/[^/]+)', file):
                dirs_to_lint.add(m.group(1))

        cwd = os.getcwd()
        os.chdir(work_dir)
        for dir_ in sorted(dirs_to_lint):
            logging.info('-' * 40)
            logging.info(f'linting {dir_}')
            logging.info('-' * 40)
            ucmlint.ucmlint(dir_)

        os.chdir(cwd)
        diags_json = f'{change_id}-{rev_num}-diags.json'
        with open(diags_json, 'w') as file:
            json.dump(
                {
                    'change_id': change_id,
                    'revision': rev_num,
                    'comments': ucmlint.gerrit_json_diags.as_json_compatible(),
                },
                file,
                indent=2,
            )

        logging.info('-' * 40)
        logging.info(f'Diagnostics saved to {diags_json}.')
        logging.info(
            f'Run ./draft_comments.py {diags_json} to upload comments to gerrit as drafts.'
        )


def main0():
    logging.basicConfig(level=logging.INFO, format='[%(levelname)8s] %(message)s')

    import argparse

    p = argparse.ArgumentParser(allow_abbrev=False)
    p.add_argument('change_id', type=int)
    p.add_argument(
        '--work-dir',
        dest='work_dir',
        help='directory of the git checkout of base-overlays, will be changed by this tool',
    )
    args = p.parse_args()
    main(**vars(args))


if __name__ == '__main__':
    main0()
