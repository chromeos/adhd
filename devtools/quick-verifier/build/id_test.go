// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestGetTriggerIDFromMessage(t *testing.T) {
	assert := assert.New(t)

	message := `[audio-qv](https://goto.google.com/chromeos-audio-qv) is testing this patchset.
Trigger ID: t-ktDrO-NuQmqna3gJ_SndnQ
Logs: https://console.cloud.google.com/cloud-build/builds?project=chromeos-audio-qv&query=tags%3Dt-ktDrO-NuQmqna3gJ_SndnQ`

	tID, ok := getTriggerIDFromMessage(message)
	assert.Equal(tID, "t-ktDrO-NuQmqna3gJ_SndnQ")
	assert.True(ok)

	message = `[audio-qv](https://goto.google.com/chromeos-audio-qv) complete:
Logs: https://console.cloud.google.com/cloud-build/builds?project=chromeos-audio-qv&query=tags%3Dt-ktDrO-NuQmqna3gJ_SndnQ`
	tID, ok = getTriggerIDFromMessage(message)
	assert.False(ok)
}
