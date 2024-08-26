/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SERVER_MAIN_MESSAGE_H_
#define CRAS_SERVER_MAIN_MESSAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define CRAS_MAIN_MESSAGE_MAX_LENGTH 256

// The types of message main thread can handle.
enum CRAS_MAIN_MESSAGE_TYPE {
  // Audio thread -> main thread
  CRAS_MAIN_A2DP,
  CRAS_MAIN_AUDIO_THREAD_EVENT,
  CRAS_MAIN_BT,
  CRAS_MAIN_BT_POLICY,
  CRAS_MAIN_METRICS,
  CRAS_MAIN_MONITOR_DEVICE,
  CRAS_MAIN_HOTWORD_TRIGGERED,
  CRAS_MAIN_NON_EMPTY_AUDIO_STATE,
  CRAS_MAIN_SPEAK_ON_MUTE,
  CRAS_MAIN_STREAM_APM,
  CRAS_MAIN_FEATURE_CHANGED,
  CRAS_MAIN_NOTIFY_RTC,
  CRAS_MAIN_EWMA_POWER_REPORT,
  CRAS_MAIN_DLC_INSTALLED,
};

/* Structure of the header of the message handled by main thread.
 *
 * For example:
 * ------------------------------
 * struct cras_some_int_message {
 *     struct cras_main_message header;
 *     int some_int;
 * };
 *
 * int cras_some_int_send(int some_int) {
 *     struct cras_some_int_message msg = CRAS_MAIN_MESSAGE_INIT;
 *     msg.header.type = CRAS_MAIN_SOME_INT;
 *     msg.header.length = sizeof(msg);
 *     msg.some_int = some_int;
 *     return cras_main_message_send((struct cras_main_message *)&msg);
 * }
 * ------------------------------
 *
 * See also CRAS_MAIN_MESSAGE_INIT.
 */
struct cras_main_message {
  // Size of the whole message.
  // Should be <= CRAS_MAIN_MESSAGE_MAX_LENGTH.
  size_t length;
  // Type of the message.
  enum CRAS_MAIN_MESSAGE_TYPE type;
};

/* This macro is for zero-initializing message structs.
 * It would help to avoid "use-of-uninitialized-value" errors.
 * See struct cras_main_message for details.
 */
#define CRAS_MAIN_MESSAGE_INIT \
  {                            \
    .header = { 0 }            \
  }

// Callback function to handle main thread message.
typedef void (*cras_message_callback)(struct cras_main_message* msg, void* arg);

// Sends a message to main thread.
int cras_main_message_send(struct cras_main_message* msg);

// Registers the handler function for specific type of message.
int cras_main_message_add_handler(enum CRAS_MAIN_MESSAGE_TYPE type,
                                  cras_message_callback callback,
                                  void* callback_data);

// Unregisters the handler for given type of message.
void cras_main_message_rm_handler(enum CRAS_MAIN_MESSAGE_TYPE type);

// Callback for main messages.
void handle_main_messages(void* arg, int revents);

// Initialize the message handling mechanism in main thread.
// Returns a fd to POLLIN on.
// When the fd is ready handle_main_messages should be called.
int cras_main_message_init();

#ifdef __cplusplus
}
#endif

#endif  // CRAS_SERVER_MAIN_MESSAGE_H_
