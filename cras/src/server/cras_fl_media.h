/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_FL_MEDIA_H_
#define CRAS_FL_MEDIA_H_

int floss_media_start(DBusConnection *conn, unsigned int hci);

int floss_media_stop(DBusConnection *conn);

#endif /* CRAS_FL_MEDIA_H_ */
