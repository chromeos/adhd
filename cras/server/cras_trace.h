// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SERVER_CRAS_TRACE_H_
#define CRAS_SERVER_CRAS_TRACE_H_

#include <percetto.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRAS_PERCETTO_CATEGORIES(C, G) C(audio, "Audio events")

PERCETTO_CATEGORY_DECLARE(CRAS_PERCETTO_CATEGORIES);

// https://github.com/olvaffe/percetto/pull/34
#ifdef NPERCETTO
#define TRACE_EVENT_DATA(category, str_name, ...)
#endif

// Initializes CRAS tracing.
// See PERCETTO_INIT.
int cras_trace_init();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SERVER_CRAS_TRACE_H_
