/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_TELEPHONY_H_
#define CRAS_SRC_SERVER_CRAS_TELEPHONY_H_

#include <dbus/dbus.h>

/* Handle object to hold required info to handle telephony status which
 * is required for responsing HFP query commands.
 */
struct cras_telephony_handle {
  // standard call status indicator, where
  // 0: no call active
  // 1: call is active
  int call;
  // call set up status indicator.
  // 0: not currently in call set up
  // 1: an incoming call prcess ongoing
  // 2: an outgoing call set up is ongoing
  int callsetup;
  // call hold status indicator.
  // 0: no call hold
  // 1: call is placed on hold or active/held calls swapped
  // (The AG has both and active AND a held call)
  // 2: call on hold, no active call
  int callheld;
  // phone number, used on fake memory storage and last phone
  // number storage.
  char* dial_number;

  // dus connetion which is used in whole telephony module.
  DBusConnection* dbus_conn;
};

void cras_telephony_start(DBusConnection* conn);

void cras_telephony_stop();

struct cras_telephony_handle* cras_telephony_get();

// Stores dial number in telephony module.
void cras_telephony_store_dial_number(int len, const char* num);

// Handles answer call event from dbus or HF
int cras_telephony_event_answer_call();

// Handles answer call event from dbus or HF
int cras_telephony_event_terminate_call();

#endif  // CRAS_SRC_SERVER_CRAS_TELEPHONY_H_
