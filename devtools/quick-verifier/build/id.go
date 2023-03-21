// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/base64"
	"regexp"

	"github.com/google/uuid"
)

// randomTriggerID generates a random string to be used as the trigger ID.
func randomTriggerID() string {
	uuid := uuid.Must(uuid.NewRandom())
	return "t-" + base64.RawURLEncoding.EncodeToString(uuid[:])
}

var triggerRegexp = regexp.MustCompile(`(?m:^Trigger ID: (t-[\w-]+)$)`)

func getTriggerIDFromMessage(message string) (id string, ok bool) {
	m := triggerRegexp.FindStringSubmatch(message)
	if len(m) != 0 {
		return m[1], true
	}
	return "", false
}
