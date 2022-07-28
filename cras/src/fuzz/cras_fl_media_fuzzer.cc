/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "cras_a2dp_manager.h"
#include "cras_alert.h"
#include "cras_bt_log.h"
#include "cras_fl_media.h"
#include "cras_fl_media_adapter.h"
#include "cras_observer.h"
#include "cras_shm.h"
#include "cras_system_state.h"
}

#define BT_OBJECT_BASE "/org/chromium/bluetooth/hci"
#define BT_OBJECT_MEDIA "/media"

static struct fl_media* active_fm = NULL;
static std::string addr = "";

/* This fuzzer consumes bytes of size ranging from 270 to 340.
 * Minimum fuzzing size if therefore set at 350.
 */
const int kMinFuzzDataSize = 350;
const int kMaxStringLength = 100;

struct cras_fl_a2dp_codec_config* codecs_create(
    FuzzedDataProvider* data_provider) {
  int bps = data_provider->ConsumeIntegral<int>();
  int channels = data_provider->ConsumeIntegral<int>();
  int priority = data_provider->ConsumeIntegral<int>();
  int type = data_provider->ConsumeIntegral<int>();
  int rate = data_provider->ConsumeIntegral<int>();

  return cras_floss_a2dp_codec_create(bps, channels, priority, type, rate);
}

void active_fm_create(FuzzedDataProvider* data_provider) {
  int hci = data_provider->ConsumeIntegral<int>();
  fl_media_init(hci);
  active_fm = floss_media_get_active_fm();
}

std::string get_valid_addr(FuzzedDataProvider* data_provider) {
  const int STR_LEN = 17;
  char str[STR_LEN + 1] = {};
  for (int i = 0; i < STR_LEN; i++) {
    if ((i + 1) % 3 == 0) {
      str[i] = ':';
    } else {
      snprintf(str + i, 2, "%X",
               data_provider->ConsumeIntegralInRange<int>(0, 15));
    }
  }
  return std::string(str);
}

std::string get_random_addr(FuzzedDataProvider* data_provider) {
  return data_provider->ConsumeRandomLengthString(kMaxStringLength);
}

void fuzzer_on_bluetooth_device_added(FuzzedDataProvider* data_provider) {
  struct cras_fl_a2dp_codec_config* codecs = codecs_create(data_provider);

  int32_t hfp_cap = data_provider->ConsumeIntegral<int32_t>();
  bool abs_vol_supported = data_provider->ConsumeBool();

  if (data_provider->ConsumeBool()) {
    addr = get_valid_addr(data_provider);
  } else {
    addr = get_random_addr(data_provider);
  }
  std::string name = data_provider->ConsumeRandomLengthString(kMaxStringLength);

  handle_on_bluetooth_device_added(active_fm, addr.c_str(), name.c_str(),
                                   codecs, hfp_cap, abs_vol_supported);
  free(codecs);
  codecs = NULL;
}

void fuzzer_on_bluetooth_device_removed() {
  handle_on_bluetooth_device_removed(active_fm, addr.c_str());
}

void fuzzer_on_absolute_volume_supported_changed(
    FuzzedDataProvider* data_provider) {
  bool abs_vol_supported = data_provider->ConsumeBool();
  handle_on_absolute_volume_supported_changed(active_fm, abs_vol_supported);
}

void fuzzer_on_absolute_volume_changed(FuzzedDataProvider* data_provider) {
  uint8_t volume = data_provider->ConsumeIntegral<uint8_t>();
  handle_on_absolute_volume_changed(active_fm, volume);
}

void fuzzer_on_hfp_volume_changed(FuzzedDataProvider* data_provider) {
  uint8_t volume = data_provider->ConsumeIntegral<uint8_t>();
  handle_on_hfp_volume_changed(active_fm, addr.c_str(), volume);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < kMinFuzzDataSize) {
    handle_on_bluetooth_device_added(NULL, NULL, NULL, NULL, 0, 0);
  } else {
    FuzzedDataProvider data_provider(data, size);
    active_fm_create(&data_provider);
    fuzzer_on_bluetooth_device_added(&data_provider);
    fuzzer_on_bluetooth_device_removed();
    fuzzer_on_absolute_volume_supported_changed(&data_provider);
    fuzzer_on_absolute_volume_changed(&data_provider);
    fuzzer_on_hfp_volume_changed(&data_provider);
    cras_alert_process_all_pending_alerts();
    fl_media_destroy(active_fm);
  }

  return 0;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  char* shm_name;
  if (asprintf(&shm_name, "/cras-%d", getpid()) < 0)
    exit(-ENOMEM);
  struct cras_server_state* exp_state =
      (struct cras_server_state*)calloc(1, sizeof(*exp_state));

  int rw_shm_fd = open("/dev/null", O_RDWR);
  int ro_shm_fd = open("/dev/null", O_RDONLY);

  cras_system_state_init("/tmp", shm_name, rw_shm_fd, ro_shm_fd, exp_state,
                         sizeof(*exp_state));
  free(shm_name);

  cras_observer_server_init();
  btlog = cras_bt_event_log_init();
  return 0;
}