/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_ALSA_CARD_INFO_H_
#define CRAS_SRC_COMMON_CRAS_ALSA_CARD_INFO_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// Actions for card add/remove/change.
enum cras_notify_device_action {
  // Must match gavd action definitions.
  CRAS_DEVICE_ACTION_ADD = 0,
  CRAS_DEVICE_ACTION_REMOVE = 1,
  CRAS_DEVICE_ACTION_CHANGE = 2,
};

enum CRAS_ALSA_CARD_TYPE {
  // Internal card that supports headset, speaker or DMIC.
  ALSA_CARD_TYPE_INTERNAL,
  // USB sound card.
  ALSA_CARD_TYPE_USB,
  // Internal card that supports only HDMI.
  ALSA_CARD_TYPE_HDMI
};

static inline const char* cras_card_type_to_string(
    enum CRAS_ALSA_CARD_TYPE type) {
  switch (type) {
    case ALSA_CARD_TYPE_INTERNAL:
      return "INTERNAL";
    case ALSA_CARD_TYPE_USB:
      return "USB";
    case ALSA_CARD_TYPE_HDMI:
      return "HDMI";
  }
  return NULL;
}

/* Information about an ALSA card to be added to the system. */
#define USB_SERIAL_NUMBER_BUFFER_SIZE 64
struct __attribute__((__packed__)) cras_alsa_card_info {
  enum CRAS_ALSA_CARD_TYPE card_type;
  // Index ALSA uses to refer to the card.  The X in "hw:X".
  uint32_t card_index;
  // vendor ID if the device is on the USB bus.
  uint32_t usb_vendor_id;
  // product ID if the device is on the USB bus.
  uint32_t usb_product_id;
  // serial number if the device is on the USB bus.
  char usb_serial_number[USB_SERIAL_NUMBER_BUFFER_SIZE];
  // the checksum of the USB descriptors if the device
  // is on the USB bus.
  uint32_t usb_desc_checksum;
};

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_ALSA_CARD_INFO_H_
