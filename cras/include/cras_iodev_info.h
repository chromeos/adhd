/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_INCLUDE_CRAS_IODEV_INFO_H_
#define CRAS_INCLUDE_CRAS_IODEV_INFO_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRAS_IODEV_NAME_BUFFER_SIZE 64
#define CRAS_NODE_TYPE_BUFFER_SIZE 32
#define CRAS_NODE_MIC_POS_BUFFER_SIZE 128
#define CRAS_NODE_NAME_BUFFER_SIZE 64
#define CRAS_NODE_HOTWORD_MODEL_BUFFER_SIZE 16
#define CRAS_DSP_PATTERN_STR_BUFFER_SIZE 28

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

// Visibility of an IO device to the end user.
enum CRAS_IODEV_VISIBILITY {
  CRAS_IODEV_VISIBLE = 0,
  // Some devices are internal to CRAS and should be hidden from the end user.
  // No client except CRAS_CLIENT_TYPE_TEST can see a hidden device.
  CRAS_IODEV_HIDDEN,
};

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
  // Visibility of this IO device to the end user.
  enum CRAS_IODEV_VISIBILITY visibility;
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
  // DEPRECATED: Formerly DISPLAY_ROTATION
  DEPRECATED_ATTR_0,
};

// The working state of DSP processings for a CRAS device.
enum CRAS_DSP_PROC_STATE {
  // Used by retcode when the CRAS device on query has no DSP processing info.
  DSP_PROC_UNSUPPORTED = -EINVAL,
  // The DSP processings are not ever started.
  DSP_PROC_NOT_STARTED = -1,
  // The DSP processings work on CRAS.
  DSP_PROC_ON_CRAS,
  // The DSP processings work on DSP (offloaded).
  DSP_PROC_ON_DSP,
};

static inline const char* cras_dsp_proc_state_to_str(
    enum CRAS_DSP_PROC_STATE state) {
  switch (state) {
    case DSP_PROC_NOT_STARTED:
      return "NOT STARTED";
    case DSP_PROC_ON_CRAS:
      return "PROCESS ON CRAS";
    case DSP_PROC_ON_DSP:
      return "PROCESS ON DSP";
    default:
      return "ERROR";
  }
}

// The DSP processing information of an iodev.
struct __attribute__((__packed__)) cras_dsp_offload_info {
  // Index of the device.
  uint32_t iodev_idx;
  // The working state of DSP processings.
  enum CRAS_DSP_PROC_STATE state;
  // The associated pipeline ID on DSP for the device.
  uint32_t dsp_pipe_id;
  // The available pattern of the associated pipeline on DSP.
  char dsp_pattern[CRAS_DSP_PATTERN_STR_BUFFER_SIZE];
};

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_INCLUDE_CRAS_IODEV_INFO_H_
