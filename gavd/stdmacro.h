/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_STDMACRO_H_)
#define _STDMACRO_H_
#include <assert.h>
#include <stdlib.h>

#define WEAK __attribute__((weak))

#define STRING(x)       #x        /* Stringify 'x'. */
#define XSTRING(x)      STRING(x) /* Expand 'x', then stringify. */

#define CONCAT(_a, _b) _a##_b
#define XCONCAT(_a, _b) CONCAT(_a, _b)

#endif
