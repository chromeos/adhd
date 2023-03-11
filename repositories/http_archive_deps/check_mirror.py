# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import functools
import hashlib
import json
import os
import shlex
import subprocess
import urllib.error
import urllib.request


MIRRORS = [
    'https://storage.googleapis.com/chromeos-localmirror/distfiles/',
    'https://storage.googleapis.com/chromeos-mirror/gentoo/distfiles/',
]


@functools.lru_cache()
def repository_cache():
    return subprocess.check_output(
        ['bazel', 'info', 'repository_cache'],
        text=True,
        stderr=subprocess.DEVNULL,
        cwd=os.environ.get('BUILD_WORKSPACE_DIRECTORY'),
    ).rstrip()


def main(deps_sha256_json):
    with open(deps_sha256_json) as file:
        deps_sha256 = json.load(file)

    for dep in deps_sha256:
        canonical_name = dep['canonical_name']
        for mirror in MIRRORS:
            url = mirror + canonical_name
            req = urllib.request.Request(url, method='HEAD')
            try:
                resp = urllib.request.urlopen(req)
            except urllib.error.HTTPError as e:
                if e.status != 404:
                    raise
            else:
                if resp.status == 200:
                    print('# ✅ ', canonical_name, 'already exist at:', url)
                    break
        else:
            cached_download_file = os.path.join(
                repository_cache(), 'content_addressable/sha256', dep['sha256'], 'file'
            )
            try:
                file = open(cached_download_file, 'rb')
            except FileNotFoundError:
                print(
                    '# ⚠️ ',
                    canonical_name,
                    'is not downloaded by Bazel yet. Try running bazel build //... --nobuild',
                )
            else:
                with file:
                    # TODO: replace with the following once we have python3.11.
                    # sha256 = hashlib.file_digest(file, hashlib.sha256).hexdigest()
                    sha256 = hashlib.sha256(file.read()).hexdigest()
                if sha256 != dep['sha256']:
                    raise Exception(
                        f"Bazel's cache for {dep['canonical_url']} is corrupted. Expected {dep['sha256']}; got {sha256}"
                    )
                print('# ⚠️ ', canonical_name, 'is not uploaded yet. Upload with:')
                print(
                    shlex.join(
                        [
                            'gsutil',
                            'cp',
                            '-n',
                            '-a',
                            'public-read',
                            cached_download_file,
                            'gs://chromeos-localmirror/distfiles/' + canonical_name,
                        ]
                    )
                )


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('deps_sha256_json')
    main(**vars(parser.parse_args()))
