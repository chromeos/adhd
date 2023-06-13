# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# ChromiumOS platform dependencies' ABI is sensitive to USE=cros-debug
# which expands to setting or unsetting NDEBUG.
COPTS = select({
    "//conditions:default": ["-DNDEBUG"],
    "//cras/platform:cros_debug_build": ["-UNDEBUG"],
})
