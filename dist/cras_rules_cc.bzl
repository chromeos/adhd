# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""CRAS specific CC rules"""

def cras_shared_library(
        name,
        roots,
        visibility,
        static_deps = [],
        dynamic_deps = [],
        testonly = False):
    """Export a shared library for external build systems.

    Light wrapper around cc_shared_library to:
    1. Expose cc_shared_library to BUILD. On some bazel versions it's not available.
    2. Set soname.

    Args:
        name: Name of the shared library. The file will be named as lib${name}.so.
        roots: List of cc_library targets whose srcs should be exported.
        visibility: Visibility of the libraries and the filegroup.
        static_deps: List of dependencies that should be linked statically.
        dynamic_deps: List of cc_shared_libraries that provide dynamically linked dependencies.
        testonly: Test only library.

    Each transitive cc_library dependency of roots must be either listed in
    static_deps, or be provided by one of the dynamic_deps.

    See also "State of cc_shared_library":
    https://docs.google.com/document/d/1NJiOr2vBJyrj2q1FFvl7IuoateuJyRWpjn8-6HxLWLs/edit?usp=sharing
    """

    native.cc_shared_library(
        name = name,
        roots = roots,
        visibility = visibility,
        static_deps = static_deps,
        dynamic_deps = dynamic_deps,
        user_link_flags = ["-Wl,-soname=lib{}.so".format(name)],
        testonly = testonly,
    )
