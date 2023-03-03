/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_alsa_io_common.h"

#include <sys/time.h>

#include "cras/src/common/utlist.h"

struct cras_ionode* first_plugged_node(struct cras_iodev* iodev) {
  struct cras_ionode* n;

  /* When this is called at iodev creation, none of the nodes
   * are selected. Just pick the first plugged one and let Chrome
   * choose it later. */
  DL_FOREACH (iodev->nodes, n) {
    if (n->plugged) {
      return n;
    }
  }
  return iodev->nodes;
}