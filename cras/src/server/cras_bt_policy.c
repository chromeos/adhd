/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_bt_device.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_main_message.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "utlist.h"

static const unsigned int PROFILE_SWITCH_DELAY_MS = 500;

enum BT_POLICY_COMMAND {
	BT_POLICY_SWITCH_PROFILE,
};

struct bt_policy_msg {
	struct cras_main_message header;
	enum BT_POLICY_COMMAND cmd;
	struct cras_bt_device *device;
	struct cras_iodev *dev;
	unsigned int arg1;
	unsigned int arg2;
};

struct profile_switch_policy {
	struct cras_bt_device *device;
	struct cras_timer *timer;
	struct profile_switch_policy *prev, *next;
};

struct profile_switch_policy *profile_switch_policies;

static void profile_switch_delay_cb(struct cras_timer *timer, void *arg)
{
	struct profile_switch_policy *policy =
		(struct profile_switch_policy *)arg;
	struct cras_iodev *iodev;

	/*
	 * During the |PROFILE_SWITCH_DELAY_MS| time interval, BT iodev could
	 * have been enabled by others, and its active profile may have changed.
	 * If iodev has been enabled, that means it has already picked up a
	 * reasonable profile to use and audio thread is accessing iodev now.
	 * We should NOT call into update_active_node from main thread
	 * because that may mess up the active node content.
	 */
	iodev = policy->device->bt_iodevs[CRAS_STREAM_OUTPUT];
	if (iodev) {
		iodev->update_active_node(iodev, 0, 1);
		cras_iodev_list_resume_dev(iodev->info.idx);
	}

	DL_DELETE(profile_switch_policies, policy);
	free(policy);
}

static void switch_profile_with_delay(struct cras_bt_device *device)
{
	struct cras_tm *tm = cras_system_state_get_tm();
	struct profile_switch_policy *policy;

	DL_SEARCH_SCALAR(profile_switch_policies, policy, device, device);
	if (policy) {
		cras_tm_cancel_timer(tm, policy->timer);
		policy->timer = NULL;
	} else {
		policy = (struct profile_switch_policy *)calloc(
			1, sizeof(*policy));
	}

	policy->device = device;
	policy->timer = cras_tm_create_timer(tm, PROFILE_SWITCH_DELAY_MS,
					     profile_switch_delay_cb, policy);
	DL_APPEND(profile_switch_policies, policy);
}

static void switch_profile(struct cras_bt_device *device,
			   struct cras_iodev *bt_iodev)
{
	struct cras_iodev *iodev;
	int dir;

	/* If a bt iodev is active, temporarily force close it.
	 * Note that we need to check all bt_iodevs for the situation that both
	 * input and output are active while switches from HFP to A2DP.
	 */
	for (dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++) {
		iodev = device->bt_iodevs[dir];
		if (!iodev)
			continue;
		cras_iodev_list_suspend_dev(iodev->info.idx);
	}

	for (dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++) {
		iodev = device->bt_iodevs[dir];
		if (!iodev)
			continue;

		/* If the iodev was active or this profile switching is
		 * triggered at opening iodev, add it to active dev list.
		 * However for the output iodev, adding it back to active dev
		 * list could cause immediate switching from HFP to A2DP if
		 * there exists an output stream. Certain headset/speaker
		 * would fail to playback afterwards when the switching happens
		 * too soon, so put this task in a delayed callback.
		 */
		if (dir == CRAS_STREAM_INPUT) {
			iodev->update_active_node(iodev, 0, 1);
			cras_iodev_list_resume_dev(iodev->info.idx);
		} else {
			switch_profile_with_delay(device);
		}
	}
}

static void init_bt_policy_msg(struct bt_policy_msg *msg,
			       enum BT_POLICY_COMMAND cmd,
			       struct cras_bt_device *device,
			       struct cras_iodev *dev, unsigned int arg1,
			       unsigned int arg2)
{
	memset(msg, 0, sizeof(*msg));
	msg->header.type = CRAS_MAIN_BT_POLICY;
	msg->header.length = sizeof(*msg);
	msg->cmd = cmd;
	msg->device = device;
	msg->dev = dev;
	msg->arg1 = arg1;
	msg->arg2 = arg2;
}

static void process_bt_policy_msg(struct cras_main_message *msg, void *arg)
{
	struct bt_policy_msg *policy_msg = (struct bt_policy_msg *)msg;

	switch (policy_msg->cmd) {
	case BT_POLICY_SWITCH_PROFILE:
		switch_profile(policy_msg->device, policy_msg->dev);
		break;
	default:
		break;
	}
}

int cras_bt_policy_switch_profile(struct cras_bt_device *device,
				  struct cras_iodev *bt_iodev)
{
	struct bt_policy_msg msg = CRAS_MAIN_MESSAGE_INIT;
	int rc;

	init_bt_policy_msg(&msg, BT_POLICY_SWITCH_PROFILE, device, bt_iodev, 0,
			   0);
	rc = cras_main_message_send((struct cras_main_message *)&msg);
	return rc;
}

void cras_bt_policy_remove_device(struct cras_bt_device *device)
{
	struct profile_switch_policy *policy;
	struct cras_tm *tm = cras_system_state_get_tm();

	DL_SEARCH_SCALAR(profile_switch_policies, policy, device, device);
	if (policy) {
		DL_DELETE(profile_switch_policies, policy);
		cras_tm_cancel_timer(tm, policy->timer);
		free(policy);
	}
}

void cras_bt_policy_start()
{
	cras_main_message_add_handler(CRAS_MAIN_BT_POLICY,
				      process_bt_policy_msg, NULL);
}

void cras_bt_policy_stop()
{
	cras_main_message_rm_handler(CRAS_MAIN_BT_POLICY);
}
