# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_file")

# TODO(aaronyu): This file is duplicated in adhd and webrtc-apm.
# For now we have to keep them in sync manually.
# Decide how we should de-dupe them.
# Is it OK to require webrtc-apm when building CRAS, or the reverse?

def file_deps():
    http_file(
        name = "the_quick_brown_fox_wav",
        urls = [
            "https://storage.googleapis.com/chromiumos-test-assets-public/tast/cros/audio/the-quick-brown-fox_20230131.wav",
        ],
        sha256 = "6fee476253d128f37d734bab6c4d25cd6fb5bcedd22871b6fceca14dfe0f7632",
    )
