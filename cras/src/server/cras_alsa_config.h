/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_ALSA_CONFIG_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_CONFIG_H_

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cras_alsa_config provides API functions to set/get configuration controls via
 * ALSA interface. The supported configuration types include boolean (switch
 * controls) and TLV-byte (blob controls).
 */

/* Sets enabled state to the switch control specified by name.
 * Args:
 *    name - The control name.
 *    enabled - True for enabling; false for disabling.
 * Returns:
 *    The error code.
 */
int cras_alsa_config_set_switch(const char* name, bool enabled);

/* Gets enabled state from the switch control specified by name.
 * Args:
 *    name - The control name.
 *    enabled - The pointer of switch state placeholder.
 * Returns:
 *    The error code.
 */
int cras_alsa_config_get_switch(const char* name, bool* enabled);

/* Sets blob to the TLV-byte typed control specified by name.
 * Args:
 *    name - The control name.
 *    blob - The pointer of the blob data buffer.
 *    blob_size - The number of bytes for the blob data.
 * Returns:
 *    The error code.
 */
int cras_alsa_config_set_tlv_bytes(const char* name,
                                   const uint8_t* blob,
                                   size_t blob_size);

/* Gets the max size of configuration blob for the TLV-byte typed control.
 * Args:
 *    name - The control name.
 * Returns:
 *    The biggest possible bytes count of configuration blob obtained from the
 *    control, otherwise a negative error code.
 */
int cras_alsa_config_get_tlv_bytes_maxcount(const char* name);

/* Gets the configuration blob data from the TLV-byte typed control.
 * Args:
 *    name - The control name.
 *    buf - The pointer of the allocated buffer for blob placeholder.
 *    buf_size - The number of bytes for the blob placeholder.
 * Returns:
 *    The bytes count for the obtained configuration blob, otherwise a
 *    negative error code.
 */
int cras_alsa_config_get_tlv_bytes_data(const char* name,
                                        uint8_t* buf,
                                        size_t buf_size);

/* Releases the connected control elements on specific sound card.
 * Args:
 *    card_index - The sound card index.
 */
void cras_alsa_config_release_controls_on_card(uint32_t card_index);

#ifdef __cplusplus
}
#endif

#endif  // CRAS_SRC_SERVER_CRAS_ALSA_CONFIG_H_
