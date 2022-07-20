// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.

#ifndef CRAS_DLC_H_
#define CRAS_DLC_H_

/**
 * Returns Dlc root_path for the "sr-bt-dlc" package.
 */
const char *cras_dlc_sr_bt_get_root(void);

/**
 * Returns `true` if the "sr-bt-dlc" packge is ready for use, otherwise
 * retuns `false`.
 */
bool cras_dlc_sr_bt_is_available(void);

#endif /* CRAS_DLC_H_ */
