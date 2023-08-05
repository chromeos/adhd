// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qv

const Query = ("project:chromiumos/third_party/adhd" +
	" branch:main" +
	" is:open" +
	" after:2023-02-08" + // Ignore old CLs.
	" -hashtag:audio-qv-ignore" + // User request to ignore.
	" (uploaderin:chromeos-gerrit-sandbox-access OR label:Code-Owners=ok)" + // Only handle "trusted" CLs.
	" ((-is:wip -label:Verified=ANY,user=1571002) OR hashtag:audio-qv-trigger)" + // Only handle open CLs that are not voted.
	"")
