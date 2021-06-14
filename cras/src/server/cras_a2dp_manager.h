/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_MANAGER_H_
#define CRAS_A2DP_MANAGER_H_

#define CRAS_A2DP_SOCKET_FILE ".a2dp"
#define CRAS_A2DP_SUSPEND_DELAY_MS (5000)

void cras_floss_a2dp_start();
void cras_floss_a2dp_stop();

/* Acquire a socket file descriptor used to write to the a2dp device. */
int cras_a2dp_skt_acquire();

/* Release the socket file descriptor used to write to the a2dp device. */
int cras_a2dp_skt_release();

/* Schedule a suspend request of the a2dp device. */
void cras_a2dp_schedule_suspend(unsigned int msec);

/* Cancel a pending suspend request if exist of the a2dp device. */
void cras_a2dp_cancel_suspend();

#endif /* CRAS_A2DP_MANAGER_H_ */
