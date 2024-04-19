/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>

#include "cras/src/common/byte_buffer.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/audio_thread_log.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_audio_thread_monitor.h"
#include "cras/src/server/cras_lea_manager.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_utf8.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

#define PCM_BUF_MAX_SIZE_FRAMES (8192 * 4)
#define PCM_BLOCK_MS 10

#define FLOSS_LEA_MAX_BUF_SIZE_BYTES PCM_BUF_MAX_SIZE_FRAMES * 8

// Child of cras_iodev to handle LEA streaming.
struct lea_io {
  // The cras_iodev structure "base class"
  struct cras_iodev base;
  // Buffer to hold pcm samples.
  struct byte_buffer* pcm_buf;
  // How many frames of audio samples we prefer to write in one
  // socket write.
  unsigned int write_block;
  // The associated |cras_lea| object.
  struct cras_lea* lea;
  // The associated ID of the corresponding LE audio group.
  int group_id;
  // If the device has been configured and attached with any stream.
  int started;
};

static void lea_free_base_resources(struct lea_io* leaio) {
  struct cras_ionode* node;
  node = leaio->base.active_node;
  if (node) {
    cras_iodev_rm_node(&leaio->base, node);
    free(node);
  }
  free(leaio->base.supported_channel_counts);
  free(leaio->base.supported_rates);
  free(leaio->base.supported_formats);
}

struct cras_iodev* lea_iodev_create(struct cras_lea* lea,
                                    const char* name,
                                    int group_id,
                                    enum CRAS_STREAM_DIRECTION dir) {
  struct lea_io* leaio;
  struct cras_iodev* iodev;
  struct cras_ionode* node;
  int rc = 0;

  leaio = (struct lea_io*)calloc(1, sizeof(*leaio));
  if (!leaio) {
    return NULL;
  }

  leaio->started = 0;
  leaio->lea = lea;
  leaio->group_id = group_id;

  iodev = &leaio->base;
  iodev->direction = dir;

  snprintf(iodev->info.name, sizeof(iodev->info.name), "%s group %d", name,
           group_id);
  iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
  iodev->info.stable_id = SuperFastHash(
      iodev->info.name, strlen(iodev->info.name), strlen(iodev->info.name));

  // Create an empty ionode
  node = (struct cras_ionode*)calloc(1, sizeof(*node));
  node->dev = iodev;
  node->btflags = CRAS_BT_FLAG_FLOSS | CRAS_BT_FLAG_LEA;
  node->type = CRAS_NODE_TYPE_BLUETOOTH;
  node->volume = 100;
  node->stable_id = iodev->info.stable_id;
  strlcpy(node->name, iodev->info.name, sizeof(node->name));
  node->ui_gain_scaler = 1.0f;
  gettimeofday(&node->plugged_time, NULL);

  // The node name exposed to UI should be a valid UTF8 string.
  if (!is_utf8_string(node->name)) {
    strlcpy(node->name, "LEA UTF8 Group Name", sizeof(node->name));
  }

  ewma_power_disable(&iodev->ewma);

  cras_iodev_add_node(iodev, node);

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    rc = cras_iodev_list_add(iodev);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add(iodev);
  }
  if (rc) {
    syslog(LOG_ERR, "Add LEA iodev to list err");
    goto error;
  }
  cras_iodev_set_active_node(iodev, node);

  /* We need the buffer to read/write data from/to the LEA group even
   * when there is no corresponding stream. */
  leaio->pcm_buf = byte_buffer_create(FLOSS_LEA_MAX_BUF_SIZE_BYTES);
  if (!leaio->pcm_buf) {
    goto error;
  }

  return iodev;
error:
  lea_free_base_resources(leaio);
  free(leaio);
  return NULL;
}

void lea_iodev_destroy(struct cras_iodev* iodev) {
  int rc;
  struct lea_io* leaio = (struct lea_io*)iodev;

  byte_buffer_destroy(&leaio->pcm_buf);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    rc = cras_iodev_list_rm(iodev);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_rm(iodev);
  } else {
    syslog(LOG_ERR, "%s: Unsupported direction %d", __func__, iodev->direction);
    return;
  }

  if (rc < 0) {
    syslog(LOG_ERR, "%s: Failed to remove iodev, rc=%d", __func__, rc);
    return;
  }

  lea_free_base_resources(leaio);
  cras_iodev_free_resources(iodev);
  free(leaio);
}
