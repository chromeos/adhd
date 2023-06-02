/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_LOG_H_
#define CRAS_SRC_COMMON_CRAS_LOG_H_

#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

// FRA signal enum.
enum FRA_SIGNAL {
  PeripheralsUsbSoundCard,
  USBAudioSelected,
  USBAudioConfigureFailed,
  USBAudioListOutputNodeFailed,
  USBAudioStartFailed,
  USBAudioSoftwareVolumeAbnormalRange,
  USBAudioSoftwareVolumeAbnormalSteps,
  USBAudioUCMNoJack,
  USBAudioUCMWrongJack,
  USBAudioResumeFailed,
};

// The log is monitoring by FRA. If the log format it changed, please also
// update the signal regular expressions under
// `google3/chromeos/feedback/analyzer/signals` as well.
// The `signal` parameter should be the `enum FRA_SIGNAL` type.
#define fra_log(__pri, signal, __fmt, ...) syslog(__pri, __fmt, __VA_ARGS__);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_LOG_H_
