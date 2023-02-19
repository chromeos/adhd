# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is a ad-hoc workaround for the fact that:
# 1. We cannot have a root workspace at //:Cargo.toml without shuffling
#    the repository.
# 2. rules_rust requires everything to be in a Cargo workspace:
#    https://github.com/bazelbuild/rules_rust/issues/1773

load("@rules_rust//crate_universe:defs.bzl", "crate")

def cargo_io_deps():
    return {
        "assert_matches": crate.spec(
            version = "1.5.0",
        ),
        "clap": crate.spec(
            version = "3.1.10",
            features = ["derive"],
        ),
        "dasp_sample": crate.spec(
            version = "0.11.0",
        ),
        "hound": crate.spec(
            version = "3.4.0",
        ),
        "libc": crate.spec(
            version = "0.2.124",
        ),
        "thiserror": crate.spec(
            version = "1.0.30",
        ),
        "bindgen": crate.spec(
            version = "0.63.0",
        ),
        "tempdir": crate.spec(
            version = "0.3.7",
        ),
        "anyhow": crate.spec(
            version = "1.0.68",
        ),
        "log": crate.spec(
            version = "0.4.17",
        ),
        "syslog": crate.spec(
            version = "6.0.1",
        ),
    }
