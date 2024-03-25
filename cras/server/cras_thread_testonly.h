// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SERVER_CRAS_THREAD_TESTONLY_H_
#define CRAS_SERVER_CRAS_THREAD_TESTONLY_H_

#ifdef __cplusplus
extern "C" {
#endif

// Disarm thread checks for the current thread for testing.
void cras_thread_disarm_checks();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SERVER_CRAS_THREAD_TESTONLY_H_
