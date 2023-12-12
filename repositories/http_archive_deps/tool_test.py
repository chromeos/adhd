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
                'https://github.com/abseil/abseil-cpp/archive/78be63686ba732b25052be15f8d6dee891c05749.zip'
            ),
            'abseil-abseil-cpp-78be63686ba732b25052be15f8d6dee891c05749.zip',
        )


if __name__ == '__main__':
    unittest.main()
