// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SERVER_CRAS_TRACE_H_
#define CRAS_SERVER_CRAS_TRACE_H_

#include <percetto.h>

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CRAS_PERCETTO_CATEGORIES(C, G) C(audio, "Audio events")

PERCETTO_CATEGORY_DECLARE(CRAS_PERCETTO_CATEGORIES);

PERCETTO_TRACK_DECLARE(CRAS_SPK_HW_LEVEL);
PERCETTO_TRACK_DECLARE(CRAS_FLOOP_OUT_HW_LEVEL);
PERCETTO_TRACK_DECLARE(CRAS_INTERNAL_MIC_HW_LEVEL);
PERCETTO_TRACK_DECLARE(CRAS_FLOOP_IN_HW_LEVEL);

PERCETTO_TRACK_DECLARE(CRAS_SPK_WRITE_FRAMES);
PERCETTO_TRACK_DECLARE(CRAS_FLOOP_OUT_WRITE_FRAMES);
PERCETTO_TRACK_DECLARE(CRAS_FLOOP_IN_READ_FRAMES);
PERCETTO_TRACK_DECLARE(CRAS_INTERNAL_MIC_READ_FRAMES);

// https://github.com/olvaffe/percetto/pull/34
#ifdef NPERCETTO
#define TRACE_EVENT_DATA(category, str_name, ...)
#endif

// Initializes CRAS tracing.
// See PERCETTO_INIT.
int cras_trace_init();

// Log the hw_level.
void cras_trace_hw_level(enum CRAS_NODE_TYPE type, unsigned int hw_level);

// Log the nframes write to or read from the hardware buffer.
void cras_trace_frames(enum CRAS_NODE_TYPE type, unsigned int nframes);

// Log the underrun event.
void cras_trace_underrun(enum CRAS_NODE_TYPE type, enum CRAS_NODE_POSITION);

// Log the overrun event.
void cras_trace_overrun(enum CRAS_NODE_TYPE type, enum CRAS_NODE_POSITION);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SERVER_CRAS_TRACE_H_
