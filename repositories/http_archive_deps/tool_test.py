# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import tool


class CanonicalNameTest(unittest.TestCase):
    def test_releases_download(self):
        self.assertEqual(
            tool.canonical_name(
                'https://github.com/bazelbuild/bazel-skylib/releases/download/1.4.1/bazel-skylib-1.4.1.tar.gz'
            ),
            'bazelbuild-bazel-skylib-1.4.1.tar.gz',
        )

    def test_archive_refs_tags(self):
        self.assertEqual(
            tool.canonical_name(
                'https://github.com/google/benchmark/archive/refs/tags/v1.7.1.tar.gz'
            ),
            'google-benchmark-v1.7.1.tar.gz',
        )

    def test_archive_sha1(self):
        self.assertEqual(
            tool.canonical_name(
                'https://github.com/hedronvision/bazel-compile-commands-extractor/archive/0197fc673a1a6035078ac7790318659d7442e27e.tar.gz'
            ),
            'hedronvision-bazel-compile-commands-extractor-0197fc673a1a6035078ac7790318659d7442e27e.tar.gz',
        )


if __name__ == '__main__':
    unittest.main()
