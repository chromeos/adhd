// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef NDEBUG
#define NDEBUG_IS_DEFINED 1
#else
#define NDEBUG_IS_DEFINED 0
#endif
// This funny static_assert checks that:
// if --//cras/platform:cros_debug is passed (the default),
//   then debug is allowed, NDEBUG should not be defined.
// otherwise if --no//cras/platform:cros_debug is passed,
//   then debug is not allowed, NDEBUG should be defined.
//
// Testing two compilation arguments is still meaningful because
// HAVE_CROS_DEBUG is only configured once, we know it is passed correctly.
// But every environment tries to override NDEBUG.
static_assert(HAVE_CROS_DEBUG != NDEBUG_IS_DEFINED);
