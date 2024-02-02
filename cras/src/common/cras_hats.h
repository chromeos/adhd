/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_HATS_H_
#define CRAS_SRC_COMMON_CRAS_HATS_H_

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CRAS_HATS_GENERAL_SURVEY_STREAM_LIVE_SEC 180
#define CRAS_HATS_BLUETOOTH_SURVEY_STREAM_LIVE_SEC 60
#define CRAS_HATS_OUTPUT_PROC_SURVEY_DEV_LIVE_SEC 120

#define CRAS_HATS_SURVEY_NAME_KEY "SurveyName"
#define CRAS_HATS_SURVEY_NAME_BLUETOOTH "BLUETOOTH"
#define CRAS_HATS_SURVEY_NAME_OUTPUT_PROC "OUTPUTPROC"

/* Send a signal to trigger a general audio satisfaction survey.
 *     stream_type - type of the removed stream.
 *     client_type - type of the client opening the stream.
 *     node_type_pair - InputType_OutputType form of string representing the
 *                      active node types when the stream is removed.
 */
void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      const char* node_type_pair);

/* Send a signal to trigger the Bluetooth audio satisfaction survey.
 *     bt_flags - a bitmask of Bluetooth stack flags.
 */
void cras_hats_trigger_bluetooth_survey(uint32_t bt_flags);

/* Send a signal to trigger the audio output processing satisfaction survey.
 *     node_type - type of the closed output device node used to distinguish
 *                 output processing for speaker and 3.5mm.
 */
void cras_hats_trigger_output_proc_survey(enum CRAS_NODE_TYPE node_type);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_HATS_H_
