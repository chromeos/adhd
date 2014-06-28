/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The blow logging funcitons must only be called from the audio thread.
 */

#ifndef AUDIO_THREAD_LOG_H_
#define AUDIO_THREAD_LOG_H_

#include <pthread.h>
#include <stdint.h>

#include "cras_types.h"

extern struct audio_thread_event_log *atlog;

static inline
struct audio_thread_event_log *audio_thread_event_log_init()
{
	return (struct audio_thread_event_log *)
			calloc(1, sizeof(struct audio_thread_event_log));
}

static inline
void audio_thread_event_log_deinit(struct audio_thread_event_log *log)
{
	free(log);
}

static inline void audio_thread_write_word(
		struct audio_thread_event_log *log,
		uint32_t word)
{
	log->log[log->write_pos] = word;
	log->write_pos++;
	log->write_pos %= AUDIO_THREAD_EVENT_LOG_SIZE;
}

/* Log a tag and the current time, Uses two words, the first is split
 * 8 bits for tag and 24 for seconds, second word is micro seconds.
 */
static inline void audio_thread_event_log_tag(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	audio_thread_write_word(log, (event << 24) | (ts.tv_sec & 0x00ffffff));
	audio_thread_write_word(log, ts.tv_nsec);
}

static inline void audio_thread_event_log_data(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
}

static inline void audio_thread_event_log_data2(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data,
		uint32_t data2)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
	audio_thread_write_word(log, data2);
}

static inline void audio_thread_event_log_data3(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data,
		uint32_t data2,
		uint32_t data3)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
	audio_thread_write_word(log, data2);
	audio_thread_write_word(log, data3);
}

#endif /* AUDIO_THREAD_LOG_H_ */
