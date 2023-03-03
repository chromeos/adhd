/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_HATS_H_
#define CRAS_SRC_COMMON_CRAS_HATS_H_

#include "cras_types.h"

#define CRAS_HATS_GENERAL_SURVEY_STREAM_LIVE_SEC 180

/* Send a signal to trigger a general audio satisfaction survey.
 *     stream_type - type of the removed stream.
 *     client_type - type of the client opening the stream.
 *     node_type_pair - InputType_OutputType form of string representing the
 *                      active node types when the stream is removed.
 */
void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      const char* node_type_pair);

#endif  // CRAS_SRC_COMMON_CRAS_HATS_H_
