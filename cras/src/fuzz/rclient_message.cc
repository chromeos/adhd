/* Copyright 2017 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "cras_iodev_list.h"
#include "cras_mix.h"
#include "cras_observer.h"
#include "cras_rclient.h"
#include "cras_system_state.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  cras_system_state_init("/tmp");
  cras_observer_server_init();
  cras_mix_init(0);
  cras_iodev_list_init();

  cras_rclient *client = cras_rclient_create(0, 0);
  cras_rclient_buffer_from_client(client, data, size, -1);
  cras_rclient_destroy(client);

  cras_iodev_list_deinit();
  cras_observer_server_free();
  cras_system_state_deinit();
  return 0;
}
