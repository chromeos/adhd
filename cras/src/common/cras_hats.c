/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/common/cras_hats.h"

#include "cras_types.h"

#if HAVE_HATS

#include "cras/src/server/cras_observer.h"

void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      const char* node_type_pair) {
  cras_observer_notify_general_survey(stream_type, client_type, node_type_pair);
}

void cras_hats_trigger_bluetooth_survey(uint32_t bt_flags) {
  cras_observer_notify_bluetooth_survey(bt_flags);
}

#else
void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      const char* node_type_pair) {}

void cras_hats_trigger_bluetooth_survey(uint32_t bt_flags) {}
#endif
