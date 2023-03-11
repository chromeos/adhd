# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This tool verifies that urls in http_archive rules are valid."""

import argparse
import hashlib
import json
import sys
import urllib.error
import urllib.request


def sha256_url(url):
    try:
        with urllib.request.urlopen(url) as file:
            return hashlib.sha256(file.read()).hexdigest()
    except urllib.error.HTTPError as e:
        return str(e).ljust(64)


def main(deps_sha256_json):
    with open(deps_sha256_json) as file:
        deps = json.load(file)

    ok = True

    for dep in deps:
        print(f'Checking {dep["canonical_name"]}...')
        for url in dep['urls']:
            sha256 = sha256_url(url)
            if sha256 == dep['sha256']:
                print(f'✅  {sha256}  {url}')
            else:
                print(f'❌  {sha256}  {url}')
                ok = False

    return 0 if ok else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('deps_sha256_json')
    sys.exit(main(**vars(parser.parse_args())))
