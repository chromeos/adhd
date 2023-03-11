# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
from pathlib import Path
import re


def canonical_url(name, urls):
    """Given a set of URLs return the best one that could be used to reference
    its source.
    """
    for url in urls:
        if url.startswith('https://storage.googleapis.com/chromeos-localmirror/distfiles/'):
            continue
        if url.startswith('https://mirror.bazel.build'):
            continue
        return url
    raise Exception(f'No good URLs found for {name}: {urls}')


RE_GITHUB_ARCHIVE = re.compile(r'^https://github\.com/([^/]+)/([^/]+)/archive/(.+)$')
RE_REFS_TAGS = re.compile(r'^refs/tags/(.+)$')
RE_GITHUB_RELEASE = re.compile(
    r'^https://github\.com/([^/]+)/([^/]+)/releases/download/([^/]+)/([^/]+)$'
)


def canonical_name(name, url):
    """Given the package name and canonical URL determine the name of the archive"""
    m = RE_GITHUB_ARCHIVE.match(url)
    if m:
        user, repo, remaining = m.groups()
        m = RE_REFS_TAGS.match(remaining)
        if m:
            tag_and_extension = m.group(1)
            return f'{user}-{repo}-{tag_and_extension}'
        sha1_and_extension = remaining
        return f'{user}-{repo}-{sha1_and_extension}'
    m = RE_GITHUB_RELEASE.match(url)
    if m:
        user, repo, version, archive = m.groups()
        return f'{user}-{archive}'
    _, _, basename = url.rpartition('/')
    return basename


def main(json_deps, bazel_external_uris_out, deps_sha256_json_out):
    with open(json_deps) as file:
        deps = json.load(file)

    bazel_external_uris = []
    bazel_external_uri_set = set()
    deps_sha256 = []

    for name, rule in deps.items():
        urls = rule['urls'] or [rule['url']]
        url = canonical_url(name, urls)
        if url in bazel_external_uri_set:
            continue
        bazel_external_uri_set.add(url)

        cname = canonical_name(name, url)
        bazel_external_uris.append(f'{url} -> {cname}')
        deps_sha256.append(
            {
                'canonical_url': url,
                'urls': urls,
                'canonical_name': cname,
                'sha256': rule['sha256'],
            }
        )

    bazel_external_uris = sorted(set(bazel_external_uris))
    Path(bazel_external_uris_out).write_text('\n'.join(bazel_external_uris) + '\n')

    with open(deps_sha256_json_out, 'w') as file:
        json.dump(deps_sha256, file, indent=2)


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('json_deps')
    parser.add_argument('bazel_external_uris_out')
    parser.add_argument('deps_sha256_json_out')
    main(**(vars(parser.parse_args())))
