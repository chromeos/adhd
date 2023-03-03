/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_INCLUDE_CRAS_IODEV_INFO_H_
#define CRAS_INCLUDE_CRAS_IODEV_INFO_H_

#include <stddef.h>
#include <stdint.h>

#define CRAS_IODEV_NAME_BUFFER_SIZE 64
#define CRAS_NODE_TYPE_BUFFER_SIZE 32
#define CRAS_NODE_MIC_POS_BUFFER_SIZE 128
#define CRAS_NODE_NAME_BUFFER_SIZE 64
#define CRAS_NODE_HOTWORD_MODEL_BUFFER_SIZE 16

// Screen Rotation in clock-wise degrees.
enum CRAS_SCREEN_ROTATION {
  ROTATE_0 = 0,
  ROTATE_90,
  ROTATE_180,
  ROTATE_270,
  NUM_CRAS_SCREEN_ROTATION,
};

// Verify if the stream_id fits the given client_id
static inline int cras_validate_screen_rotation(int r) {
  return r >= ROTATE_0 && r < NUM_CRAS_SCREEN_ROTATION;
}

// Last IO device open result
enum CRAS_IODEV_LAST_OPEN_RESULT {
  UNKNOWN = 0,
  SUCCESS,
  FAILURE,
};

// Abbreviated open result for display in cras_test_client
static inline const char* cras_iodev_last_open_result_abb_str(
    enum CRAS_IODEV_LAST_OPEN_RESULT last_open_result) {
  switch (last_open_result) {
    case UNKNOWN:
      return "UNK";
    case SUCCESS:
      return "OK";
    case FAILURE:
      return "FAIL";
    default:
      return "ERR";
  }
}

// Identifying information about an IO device.
struct __attribute__((__packed__)) cras_iodev_info {
  // iodev index.
  uint32_t idx;
  // Name displayed to the user.
  char name[CRAS_IODEV_NAME_BUFFER_SIZE];
  // ID that does not change due to device plug/unplug or reboot.
  uint32_t stable_id;
  // Max supported channel count of this device.
  uint32_t max_supported_channels;
  // The last opening result for this IO device.
  enum CRAS_IODEV_LAST_OPEN_RESULT last_open_result;
};

// Identifying information about an ionode on an iodev.
struct __attribute__((__packed__)) cras_ionode_info {
  // Index of the device this node belongs.
  uint32_t iodev_idx;
  // Index of this node on the device.
  uint32_t ionode_idx;
  // Set true if this node is known to be plugged in.
  int32_t plugged;
  // If this is the node currently being used.
  int32_t active;
  struct {
    int64_t tv_sec;
    int64_t tv_usec;
  } plugged_time;  // If plugged is true, this is the time it was attached.
  // per-node volume (0-100)
  uint32_t volume;
  // per-node capture gain/attenuation (in 100*dBFS)
  int32_t capture_gain;
  // Adjustable gain scaler set by Chrome.
  float ui_gain_scaler;
  // Set true if left and right channels are swapped.
  int32_t left_right_swapped;
  uint32_t type_enum;
  // ID that does not change due to device plug/unplug or reboot.
  uint32_t stable_id;
  // Type displayed to the user.
  char type[CRAS_NODE_TYPE_BUFFER_SIZE];
  // Name displayed to the user.
  char name[CRAS_NODE_NAME_BUFFER_SIZE];
  // name of the currently selected hotword model.
  char active_hotword_model[CRAS_NODE_HOTWORD_MODEL_BUFFER_SIZE];
  // the display rotation state.
  enum CRAS_SCREEN_ROTATION display_rotation;
  // Bit-wise audio effect support information. See enum
  // audio_effect_type.
  uint32_t audio_effect;
  // The total volume step of the node
  // suggested by the system.
  // Mainly used to calculate
  // the percentage of volume change.
  // This value for input node is invalid (0).
  // Output nodes have valid values ​​(> 0).
  int32_t number_of_volume_steps;
};

// This is used in the cras_client_set_node_attr API.
enum ionode_attr {
  // set the node as plugged/unplugged.
  IONODE_ATTR_PLUGGED,
  // set the node's output volume.
  IONODE_ATTR_VOLUME,
  // set the node's capture gain.
  IONODE_ATTR_CAPTURE_GAIN,
  // Swap the node's left and right channel.
  IONODE_ATTR_SWAP_LEFT_RIGHT,
  // set the node's display rotation state.
  IONODE_ATTR_DISPLAY_ROTATION
};

/* The bitmask enum of audio effects. Bit is toggled on for supporting.
 * This should be always aligned to system_api/dbus/service_constants.h.
 */
enum audio_effect_type {
  // Noise Cancellation support.
  EFFECT_TYPE_NOISE_CANCELLATION = 1 << 0
};

#endif  // CRAS_INCLUDE_CRAS_IODEV_INFO_H_
