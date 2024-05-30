/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE  // for strdupa

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/select.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "cras/common/check.h"
#include "cras/common/rust_common.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/common/cras_types_internal.h"
#include "cras/src/common/cras_version.h"
#include "cras_audio_format.h"
#include "cras_client.h"
#include "cras_iodev_info.h"
#include "cras_timespec.h"
#include "cras_types.h"
#include "cras_util.h"
#include "packet_status_logger.h"
#include "third_party/strlcpy/strlcpy.h"

#define NOT_ASSIGNED (0)
#define PLAYBACK_BUFFERED_TIME_IN_US (5000)

#define BUF_SIZE 32768

static const size_t MAX_IODEVS = 10;            // Max devices to print out.
static const size_t MAX_IONODES = 20;           // Max ionodes to print out.
static const size_t MAX_ATTACHED_CLIENTS = 10;  // Max clients to print out.

static int pipefd[2];
static struct timespec last_latency;
static int show_latency;
static float last_rms_sqr_sum;
static int last_rms_size;
static float total_rms_sqr_sum;
static int total_rms_size;
static int show_rms;
static int show_total_rms;
static int keep_looping = 1;
static int exit_after_done_playing = 1;
static size_t duration_frames;
static int pause_client = 0;
static int pause_a_reply = 0;
static int pause_in_playback_reply = 1000;

static char* channel_layout = NULL;
static int pin_device_id;
static int aec_ref_device_id;

static int play_short_sound = 0;
static int play_short_sound_periods = 0;
static int play_short_sound_periods_left = 0;

static unsigned int effects = 0;

static char* aecdump_file = NULL;
static char time_str[128];

static enum CRAS_CLIENT_TYPE client_type = CRAS_CLIENT_TYPE_TEST;

static bool show_ooo_ts = 0;
static struct timespec last_ts;
static bool ooo_ts_encountered = false;

// ionode flags used in --print_nodes_inlined
enum {
  IONODE_FLAG_DIRECTION,
  IONODE_FLAG_ACTIVE,
  IONODE_FLAG_PLUGGED,
  IONODE_FLAG_LR_SWAPPED,
  IONODE_FLAG_HOTWORD,
  IONODE_NUM_FLAGS
};

struct print_nodes_inlined_options {
  int id_width;
  int maxch_width;
  int name_width;
  int flag_width;
  int vol_width;
  int ui_width;
  int type_width;
};

// Sleep interval between cras_client_read_atlog calls.
static const struct timespec follow_atlog_sleep_ts = {
    0, 50 * 1000 * 1000  // 50 ms.
};

enum {
  THREAD_PRIORITY_UNSET,
  THREAD_PRIORITY_NONE,   // don't set any priority settings
  THREAD_PRIORITY_NICE,   // set nice value
  THREAD_PRIORITY_RT_RR,  // set rt priority with policy SCHED_RR
};
static int thread_priority = THREAD_PRIORITY_UNSET;
static int niceness_level = 0;
static int rt_priority = 0;

static void thread_priority_cb(struct cras_client* client) {
  switch (thread_priority) {
    case THREAD_PRIORITY_NONE:
      break;
    case THREAD_PRIORITY_NICE:
      CRAS_CHECK(0 == cras_set_nice_level(niceness_level));
      break;
    case THREAD_PRIORITY_RT_RR:
      CRAS_CHECK(0 == cras_set_rt_scheduling(rt_priority));
      CRAS_CHECK(0 == cras_set_thread_priority(rt_priority));
      break;
    default:
      CRAS_CHECK(false && "thread_priority is unset!");
  }
}

/*
 * Conditional so the client thread can signal that main should exit.
 *
 * These should not be used directly,
 * but through signal_done() wait_done_timeout() helpers instead.
 */
static bool done_flag = false;
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

struct cras_audio_format* aud_format;
struct {
  char* name;
  snd_pcm_format_t format;
} supported_formats[] = {
    {"S16_LE", SND_PCM_FORMAT_S16_LE},
    {"S24_LE", SND_PCM_FORMAT_S24_LE},
    {"S32_LE", SND_PCM_FORMAT_S32_LE},
    {NULL, 0},
};

static int terminate_stream_loop() {
  keep_looping = 0;
  return write(pipefd[1], "1", 1);
}

static size_t get_block_size(uint64_t buffer_time_in_us, size_t rate) {
  return (size_t)(buffer_time_in_us * rate / 1000000);
}

static void check_stream_terminate(size_t frames) {
  if (duration_frames) {
    if (duration_frames <= frames) {
      terminate_stream_loop();
    } else {
      duration_frames -= frames;
    }
  }
}

static void fill_time_offset(time_t* sec_offset, int32_t* nsec_offset) {
  struct timespec mono_time, real_time;

  clock_gettime(CLOCK_MONOTONIC_RAW, &mono_time);
  clock_gettime(CLOCK_REALTIME, &real_time);
  *sec_offset = real_time.tv_sec - mono_time.tv_sec;
  *nsec_offset = real_time.tv_nsec - mono_time.tv_nsec;
}

// Compute square sum of samples (for calculation of RMS value).
float compute_sqr_sum_16(const int16_t* samples, int size) {
  unsigned i;
  float sqr_sum = 0;

  for (i = 0; i < size; i++) {
    sqr_sum += samples[i] * samples[i];
  }

  return sqr_sum;
}

// Update the RMS values with the given samples.
int update_rms(const uint8_t* samples, int size) {
  switch (aud_format->format) {
    case SND_PCM_FORMAT_S16_LE: {
      last_rms_sqr_sum = compute_sqr_sum_16((int16_t*)samples, size / 2);
      last_rms_size = size / 2;
      break;
    }
    default:
      return -EINVAL;
  }

  total_rms_sqr_sum += last_rms_sqr_sum;
  total_rms_size += last_rms_size;

  return 0;
}

static int parse_effect_bits(const char* s) {
  int val;
  errno = 0;
  val = (int)strtol(s, NULL, 16);
  if (errno) {
    return errno;
  }

  effects = val;
  return 0;
}

/*
 * Parses "--effects" argument string for stream effects. Two formats are given:
 *     <name>[,<name>...]: Effects specified by names. Use comma(,) as delimiter
 *                         for multiple effects.
 *                         Available effect names: aec, ns, agc, vad
 *                         Examples: "aec", "aec,agc"
 *     0x<value>: Effects specified by hex value, in accordance with
 *                CRAS_STREAM_EFFECT bitmasks. Prefix "0x" is required.
 *                Examples: "0x11", "0x3"
 */
static void parse_stream_effects(char* input) {
  char* s;
  effects = 0;

  // Parse if effects specified by hex value.
  if (strncmp("0x", input, 2) == 0) {
    if (parse_effect_bits(optarg) != 0) {
      printf("Invalid effect hex value %s\n", optarg);
    }
    return;
  }

  // Parse effects by names.
  s = strtok(input, ",");
  while (s) {
    if (strcmp("aec", s) == 0) {
      effects |= APM_ECHO_CANCELLATION;
    } else if (strcmp("ns", s) == 0) {
      effects |= APM_NOISE_SUPRESSION;
    } else if (strcmp("agc", s) == 0) {
      effects |= APM_GAIN_CONTROL;
    } else if (strcmp("vad", s) == 0) {
      effects |= APM_VOICE_DETECTION;
    } else {
      printf("Unknown effect %s\n", s);
    }
    s = strtok(NULL, ",");
  }
}

// Parses a string with format <N>:<M> into a node id
static int parse_node_id(char* input, cras_node_id_t* id_out) {
  const char* s;
  char* endptr;
  int dev_index;
  int node_index;

  if (!id_out) {
    return -EINVAL;
  }

  s = strtok(input, ":");
  if (!s) {
    return -EINVAL;
  }
  dev_index = strtol(s, &endptr, 10);
  if (*endptr) {
    return -EINVAL;
  }

  s = strtok(NULL, ":");
  if (!s) {
    return -EINVAL;
  }
  node_index = strtol(s, &endptr, 10);
  if (*endptr) {
    return -EINVAL;
  }

  *id_out = cras_make_node_id(dev_index, node_index);
  return 0;
}

// Parses a string with format <N>:<M>:<0-100> into a node id and a value
static int parse_node_id_with_value(char* input,
                                    cras_node_id_t* id_out,
                                    int* value_out) {
  const char* s;
  char* endptr;
  int dev_index;
  int node_index;
  long int value;

  if (!id_out || !value_out) {
    return -EINVAL;
  }

  s = strtok(input, ":");
  if (!s) {
    return -EINVAL;
  }
  dev_index = strtol(s, &endptr, 10);
  if (*endptr) {
    return -EINVAL;
  }

  s = strtok(NULL, ":");
  if (!s) {
    return -EINVAL;
  }
  node_index = strtol(s, &endptr, 10);
  if (*endptr) {
    return -EINVAL;
  }

  s = strtok(NULL, ":");
  if (!s) {
    return -EINVAL;
  }
  value = strtol(s, &endptr, 10);
  if (*endptr) {
    return -EINVAL;
  }
  if (value > INT_MAX || value < INT_MIN) {
    return -EOVERFLOW;
  }

  *id_out = cras_make_node_id(dev_index, node_index);
  *value_out = value;
  return 0;
}

// Signal done_cond so the main thread can continue execution
static void signal_done() {
  CRAS_CHECK(pthread_mutex_lock(&done_mutex) == 0);
  done_flag = true;
  CRAS_CHECK(pthread_cond_signal(&done_cond) == 0);
  CRAS_CHECK(pthread_mutex_unlock(&done_mutex) == 0);
}

/*
 * Wait for done_cond to be signalled, with a timeout
 * Returns non-zero error code on failure
 */
static int wait_done_timeout(int timeout_sec) {
  int rc = 0;
  struct timespec deadline;

  if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
    return errno;
  }
  deadline.tv_sec += timeout_sec;

  pthread_mutex_lock(&done_mutex);
  if (!done_flag) {
    rc = pthread_cond_timedwait(&done_cond, &done_mutex, &deadline);
  }
  done_flag = false;
  pthread_mutex_unlock(&done_mutex);

  return rc;
}

// Run from callback thread.
static int got_samples(struct cras_client* client,
                       cras_stream_id_t stream_id,
                       uint8_t* captured_samples,
                       size_t frames,
                       const struct timespec* captured_time,
                       void* user_arg) {
  int* fd = (int*)user_arg;
  int ret;
  int write_size;
  int frame_bytes;

  while (pause_client) {
    usleep(10000);
  }

  cras_client_calc_capture_latency(captured_time, &last_latency);
  if (show_ooo_ts && timespec_after(&last_ts, captured_time)) {
    printf("Capture timestamp out of order\n");
    printf("Last capture timestamp: %ld.%09ld\n", last_ts.tv_sec,
           last_ts.tv_nsec);
    printf("Current capture timestamp: %ld.%09ld\n", captured_time->tv_sec,
           captured_time->tv_nsec);
    terminate_stream_loop();
    ooo_ts_encountered = true;
  }
  last_ts.tv_nsec = captured_time->tv_nsec;
  last_ts.tv_sec = captured_time->tv_sec;

  frame_bytes = cras_client_format_bytes_per_frame(aud_format);
  write_size = frames * frame_bytes;

  // Update RMS values with all available frames.
  if (keep_looping) {
    update_rms(captured_samples,
               MIN(write_size, duration_frames * frame_bytes));
  }

  check_stream_terminate(frames);

  ret = write(*fd, captured_samples, write_size);
  if (ret != write_size) {
    printf("Error writing file\n");
  }
  return frames;
}

// Run from callback thread.
static int put_samples(struct cras_client* client,
                       cras_stream_id_t stream_id,
                       uint8_t* captured_samples,
                       uint8_t* playback_samples,
                       unsigned int frames,
                       const struct timespec* captured_time,
                       const struct timespec* playback_time,
                       void* user_arg) {
  uint32_t frame_bytes = cras_client_format_bytes_per_frame(aud_format);
  int fd = *(int*)user_arg;
  uint8_t buff[BUF_SIZE];
  int nread;

  while (pause_client) {
    usleep(10000);
  }

  if (pause_a_reply) {
    usleep(pause_in_playback_reply);
    pause_a_reply = 0;
  }

  check_stream_terminate(frames);

  cras_client_calc_playback_latency(playback_time, &last_latency);
  if (show_ooo_ts && timespec_after(&last_ts, playback_time)) {
    printf("Playback timestamp out of order\n");
    printf("Last playback timestamp: %ld.%09ld\n", last_ts.tv_sec,
           last_ts.tv_nsec);
    printf("Current playback timestamp: %ld.%09ld\n", playback_time->tv_sec,
           playback_time->tv_nsec);
    terminate_stream_loop();
    ooo_ts_encountered = true;
  }
  last_ts.tv_nsec = playback_time->tv_nsec;
  last_ts.tv_sec = playback_time->tv_sec;

  if (play_short_sound) {
    if (play_short_sound_periods_left) {
      // Play a period from file.
      play_short_sound_periods_left--;
    } else {
      // Fill zeros to play silence.
      memset(playback_samples, 0, MIN(frames * frame_bytes, BUF_SIZE));
      return frames;
    }
  }

  nread = read(fd, buff, MIN(frames * frame_bytes, BUF_SIZE));
  if (nread <= 0) {
    if (exit_after_done_playing) {
      terminate_stream_loop();
    }
    return nread;
  }

  memcpy(playback_samples, buff, nread);
  return nread / frame_bytes;
}

// Run from callback thread.
static int put_stdin_samples(struct cras_client* client,
                             cras_stream_id_t stream_id,
                             uint8_t* captured_samples,
                             uint8_t* playback_samples,
                             unsigned int frames,
                             const struct timespec* captured_time,
                             const struct timespec* playback_time,
                             void* user_arg) {
  int rc = 0;
  uint32_t frame_bytes = cras_client_format_bytes_per_frame(aud_format);

  rc = read(0, playback_samples, (size_t)frames * (size_t)frame_bytes);
  if (rc <= 0) {
    terminate_stream_loop();
    return rc;
  }

  return rc / frame_bytes;
}

static int stream_error(struct cras_client* client,
                        cras_stream_id_t stream_id,
                        int err,
                        void* arg) {
  printf("Stream error %d\n", err);
  terminate_stream_loop();
  return 0;
}

static void print_last_latency() {
  if (last_latency.tv_sec > 0 || last_latency.tv_nsec > 0) {
    printf("%u.%09u\n", (unsigned)last_latency.tv_sec,
           (unsigned)last_latency.tv_nsec);
  } else {
    printf("-%lld.%09lld\n", (long long)-last_latency.tv_sec,
           (long long)-last_latency.tv_nsec);
  }
}

static void print_last_rms() {
  if (last_rms_size != 0) {
    printf("%.9f\n", sqrt(last_rms_sqr_sum / last_rms_size));
  }
}

static void print_total_rms() {
  if (total_rms_size != 0) {
    printf("%.9f\n", sqrt(total_rms_sqr_sum / total_rms_size));
  }
}

static void print_dev_info(const struct cras_iodev_info* devs, int num_devs) {
  unsigned i;

  printf("\tID\tMaxCha\tLastOpen\tName\n");
  for (i = 0; i < num_devs; i++) {
    printf("\t%u\t%u\t%s\t\t%s\n", devs[i].idx, devs[i].max_supported_channels,
           cras_iodev_last_open_result_abb_str(devs[i].last_open_result),
           devs[i].name);
  }
}

static void print_node_info(struct cras_client* client,
                            const struct cras_ionode_info* nodes,
                            int num_nodes,
                            int is_input) {
  unsigned i;

  printf(
      "\tStable Id\t ID\t%4s  UI       Plugged\tL/R swapped\t      "
      "Time Hotword\tType\t\tMaxCha Name\n",
      is_input ? "Gain" : " Vol");
  for (i = 0; i < num_nodes; i++) {
    char max_channels_str[7];
    if (is_input) {
      // Print "X" as don't-care for input nodes because
      // cras_client_get_max_supported_channels() is only valid for outputs.
      strlcpy(max_channels_str, "     X", sizeof(max_channels_str));
    } else {
      uint32_t max_channels;
      int rc = cras_client_get_max_supported_channels(
          client, cras_make_node_id(nodes[i].iodev_idx, nodes[i].ionode_idx),
          &max_channels);
      if (rc) {
        max_channels = 0;
      }
      snprintf(max_channels_str, sizeof(max_channels_str), "%6u", max_channels);
    }
    printf("\t(%08x)\t%u:%u\t%5g %f %7s\t%14s\t%10ld %-7s\t%-16s%-6s%c%s\n",
           pseudonymize_stable_id(nodes[i].stable_id), nodes[i].iodev_idx,
           nodes[i].ionode_idx,
           is_input ? nodes[i].capture_gain / 100.0 : (double)nodes[i].volume,
           nodes[i].ui_gain_scaler, nodes[i].plugged ? "yes" : "no",
           nodes[i].left_right_swapped ? "yes" : "no",
           (long)nodes[i].plugged_time.tv_sec, nodes[i].active_hotword_model,
           nodes[i].type, max_channels_str, nodes[i].active ? '*' : ' ',
           nodes[i].name);
  }
}

static void print_device_lists(struct cras_client* client) {
  struct cras_iodev_info devs[MAX_IODEVS];
  struct cras_ionode_info nodes[MAX_IONODES];
  size_t num_devs, num_nodes;
  int rc;

  num_devs = MAX_IODEVS;
  num_nodes = MAX_IONODES;
  rc = cras_client_get_output_devices(client, devs, nodes, &num_devs,
                                      &num_nodes);
  if (rc < 0) {
    return;
  }
  printf("Output Devices:\n");
  print_dev_info(devs, num_devs);
  printf("Output Nodes:\n");
  print_node_info(client, nodes, num_nodes, 0);

  num_devs = MAX_IODEVS;
  num_nodes = MAX_IONODES;
  rc =
      cras_client_get_input_devices(client, devs, nodes, &num_devs, &num_nodes);
  printf("Input Devices:\n");
  print_dev_info(devs, num_devs);
  printf("Input Nodes:\n");
  print_node_info(client, nodes, num_nodes, 1);
}

// truncate the input string if it is too long
//
// keeps heads and tails as important numbers such as ":0,6" tend to be
// in the end of the string.
//
// For example:
// str_truncate(10, "foo") -> "foo"
// str_truncate(10, "a very long string") -> "a v...ring"
static char* str_truncate(int len, char* str) {
  CRAS_CHECK(len >= 3);
  int actual_len = strlen(str);
  if (actual_len <= len) {
    return str;
  }

  int headlen = (len - 3) / 2;
  // set ...
  memset(str + headlen, '.', 3);

  int tailstart = actual_len - (len - headlen - 3);
  memmove(str + headlen + 3, str + tailstart, actual_len - tailstart);
  str[len] = 0;

  return str;
}

static void print_nodes_inlined_for_direction(
    struct cras_client* client,
    const struct print_nodes_inlined_options* opt,
    const struct cras_iodev_info* devs,
    int num_devs,
    const struct cras_ionode_info* nodes,
    int num_nodes,
    bool is_input) {
  bool* has_associated_node = calloc(sizeof(bool), num_devs);
  CRAS_CHECK(has_associated_node != NULL);

  for (int i = 0; i < num_nodes; i++) {
    const struct cras_ionode_info* node = &nodes[i];

    const char* dev_name = "<unknown>";
    int dev_max_ch = -1;
    int dev_id = node->iodev_idx;
    for (int j = 0; j < num_devs; j++) {
      if (devs[j].idx == dev_id) {
        has_associated_node[j] = true;
        dev_name = devs[j].name;
        dev_max_ch = devs[j].max_supported_channels;
        break;
      }
    }

    char flags[IONODE_NUM_FLAGS + 1] = {0};
    memset(flags, '-', IONODE_NUM_FLAGS);

    flags[IONODE_FLAG_DIRECTION] = is_input ? 'I' : 'O';

    if (nodes[i].active) {
      flags[IONODE_FLAG_ACTIVE] = 'A';
    }

    if (nodes[i].plugged) {
      flags[IONODE_FLAG_PLUGGED] = 'P';
    }

    if (nodes[i].left_right_swapped) {
      flags[IONODE_FLAG_LR_SWAPPED] = 'S';
    }

    // active_hotword_model not an empty string
    if (nodes[i].active_hotword_model[0]) {
      flags[IONODE_FLAG_HOTWORD] = 'H';
    }

    // clang-format off
		printf("%*d  %-*s  %*d:%-*d  %-*s  %*d  %*f  %-*s  %s\n",
			opt->maxch_width, dev_max_ch,
			opt->name_width, str_truncate(opt->name_width, strndupa(dev_name, CRAS_IODEV_NAME_BUFFER_SIZE)),
			opt->id_width, dev_id,
			opt->id_width, node->ionode_idx,
			opt->flag_width, flags,
			opt->vol_width, is_input ? node->capture_gain / 100: node->volume,
			opt->ui_width, node->ui_gain_scaler,
			opt->type_width, str_truncate(opt->type_width, strndupa(node->type, CRAS_NODE_TYPE_BUFFER_SIZE)),
			str_truncate(opt->name_width, strndupa(node->name, CRAS_NODE_NAME_BUFFER_SIZE))
		);
    // clang-format on
  }

  // every dev should have a node associated with it
  for (int i = 0; i < num_devs; i++) {
    CRAS_CHECK(has_associated_node[i]);
  }

  free(has_associated_node);
}

static void print_nodes_inlined(struct cras_client* client) {
  struct cras_iodev_info devs[MAX_IODEVS];
  struct cras_ionode_info nodes[MAX_IONODES];
  size_t num_devs, num_nodes;
  int rc;

  const struct print_nodes_inlined_options opt = {
      .id_width = 2,
      .maxch_width = 2,
      .name_width = 30,
      .flag_width = IONODE_NUM_FLAGS,
      .vol_width = 3,
      .ui_width = 8,
      .type_width = 17  // strlen("POST_DSP_LOOPBACK") == 17
  };

  printf("%*s  %*s  /--Nodes---\n", opt.maxch_width + opt.name_width + 2,
         "---Devices--\\", 1 + 2 * opt.id_width, ""  // ID column
  );
  // clang-format off
	printf("%-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %s\n",
		opt.maxch_width, "Ch",
		opt.name_width, "DeviceName",
		1 + 2 * opt.id_width, "ID",
		opt.flag_width, "Flag",
		opt.vol_width, "Vol",
		opt.ui_width, "UI",
		opt.type_width, "Type",
		"NodeName"
	);
  // clang-format on

  num_devs = MAX_IODEVS;
  num_nodes = MAX_IONODES;
  rc = cras_client_get_output_devices(client, devs, nodes, &num_devs,
                                      &num_nodes);
  if (rc == 0) {
    print_nodes_inlined_for_direction(client, &opt, devs, num_devs, nodes,
                                      num_nodes, false);
  }

  num_devs = MAX_IODEVS;
  num_nodes = MAX_IONODES;
  rc =
      cras_client_get_input_devices(client, devs, nodes, &num_devs, &num_nodes);
  if (rc == 0) {
    print_nodes_inlined_for_direction(client, &opt, devs, num_devs, nodes,
                                      num_nodes, true);
  }

  printf(
      "---\n"
      "ID: $dev_id:$node_id\n"
      "Ch: Max supported channels\n"
      "Flags:\n"
      "  I: Input Node\n"
      "  O: Output Node\n"
      "  A: Active\n"
      "  P: Plugged\n"
      "  S: LR Swapped\n"
      "  H: There is an active hotword model\n");
}

static void print_attached_client_list(struct cras_client* client) {
  struct cras_attached_client_info clients[MAX_ATTACHED_CLIENTS];
  size_t i;
  int num_clients;

  num_clients =
      cras_client_get_attached_clients(client, clients, MAX_ATTACHED_CLIENTS);
  if (num_clients < 0) {
    return;
  }
  num_clients = MIN(num_clients, MAX_ATTACHED_CLIENTS);
  printf("Attached clients:\n");
  printf("\tID\tpid\tuid\n");
  for (i = 0; i < num_clients; i++) {
    printf("\t%u\t%d\t%d\n", clients[i].id, clients[i].pid, clients[i].gid);
  }
}

static void print_active_stream_info(struct cras_client* client) {
  struct timespec ts;
  unsigned num_streams;

  num_streams = cras_client_get_num_active_streams(client, &ts);
  printf("Num active streams: %u\n", num_streams);
  printf("Last audio active time: %lld, %lld\n", (long long)ts.tv_sec,
         (long long)ts.tv_nsec);
}

static void print_system_volumes(struct cras_client* client) {
  printf(
      "System Volume (0-100): %zu %s\n"
      "Capture Muted : %s\n",
      cras_client_get_system_volume(client),
      cras_client_get_system_muted(client) ? "(Muted)" : "",
      cras_client_get_system_capture_muted(client) ? "Muted" : "Not muted");
}

static void print_user_muted(struct cras_client* client) {
  printf("User muted: %s\n",
         cras_client_get_user_muted(client) ? "Muted" : "Not muted");
}

/*
 * Convert time value from one clock to the other using given offset
 * in sec and nsec.
 */
static void convert_time(unsigned int* sec,
                         unsigned int* nsec,
                         time_t sec_offset,
                         int32_t nsec_offset) {
  sec_offset += *sec;
  nsec_offset += *nsec;
  if (nsec_offset >= 1000000000L) {
    sec_offset++;
    nsec_offset -= 1000000000L;
  } else if (nsec_offset < 0) {
    sec_offset--;
    nsec_offset += 1000000000L;
  }
  *sec = sec_offset;
  *nsec = nsec_offset;
}

static float get_ewma_power_as_float(uint32_t data) {
  float f = 0.0f;

  /* Convert from the uint32_t log type back to float.
   * If data cannot be assigned to float, default value will
   * be printed as -inf to hint the problem.
   */
  if (sizeof(uint32_t) == sizeof(float)) {
    memcpy(&f, &data, sizeof(float));
  } else {
    printf("%-30s float to uint32_t\n", "MEMORY_NOT_ALIGNED");
  }

  /* Convert to dBFS and set to zero if it's
   * insignificantly low.  Picking the same threshold
   * 1.0e-10f as in Chrome.
   */
  return (f < 1.0e-10f) ? -INFINITY : 10.0f * log10f(f);
}

static void show_alog_tag(const struct audio_thread_event_log* log,
                          unsigned int tag_idx,
                          int32_t sec_offset,
                          int32_t nsec_offset) {
  unsigned int tag = (log->log[tag_idx].tag_sec >> 24) & 0xff;
  unsigned int sec = log->log[tag_idx].tag_sec & 0x00ffffff;
  unsigned int nsec = log->log[tag_idx].nsec;
  unsigned int data1 = log->log[tag_idx].data1;
  unsigned int data2 = log->log[tag_idx].data2;
  unsigned int data3 = log->log[tag_idx].data3;
  time_t lt;
  struct tm t;

  // Skip unused log entries.
  if (log->log[tag_idx].tag_sec == 0 && log->log[tag_idx].nsec == 0) {
    return;
  }

  // Convert from monotonic raw clock to realtime clock.
  convert_time(&sec, &nsec, sec_offset, nsec_offset);
  lt = sec;
  gmtime_r(&lt, &t);
  strftime(time_str, 128, "%Y-%m-%dT%H:%M:%S", &t);

  printf("%s.%09u cras atlog  ", time_str, nsec);

  // Prepare realtime string for arguments.
  switch (tag) {
    case AUDIO_THREAD_A2DP_FLUSH:
    case AUDIO_THREAD_READ_AUDIO_TSTAMP:
    case AUDIO_THREAD_FILL_AUDIO_TSTAMP:
    case AUDIO_THREAD_STREAM_RESCHEDULE:
    case AUDIO_THREAD_STREAM_SLEEP_TIME:
    case AUDIO_THREAD_STREAM_SLEEP_ADJUST:
    case AUDIO_THREAD_DEV_SLEEP_TIME:
      sec = data2;
      nsec = data3;
      break;
  }
  convert_time(&sec, &nsec, sec_offset, nsec_offset);
  lt = sec;
  gmtime_r(&lt, &t);
  strftime(time_str, 128, " %H:%M:%S", &t);

  switch (tag) {
    case AUDIO_THREAD_WAKE:
      printf("%-30s num_fds:%d\n", "WAKE", (int)data1);
      break;
    case AUDIO_THREAD_SLEEP:
      printf("%-30s sleep:%09d.%09d non_empty %d\n", "SLEEP", (int)data1,
             (int)data2, (int)data3);
      break;
    case AUDIO_THREAD_READ_AUDIO:
      printf("%-30s dev:%u hw_level:%u read:%u\n", "READ_AUDIO", data1, data2,
             data3);
      break;
    case AUDIO_THREAD_READ_AUDIO_TSTAMP:
      printf("%-30s dev:%u tstamp:%s.%09u\n", "READ_AUDIO_TSTAMP", data1,
             time_str, nsec);
      break;
    case AUDIO_THREAD_READ_AUDIO_DONE: {
      float f = get_ewma_power_as_float(data2);
      printf("%-30s read_remainder:%u power:%f dBFS\n", "READ_AUDIO_DONE",
             data1, f);
      break;
    }
    case AUDIO_THREAD_READ_OVERRUN:
      printf("%-30s dev:%u stream:%x num_overruns:%u\n", "READ_AUDIO_OVERRUN",
             data1, data2, data3);
      break;
    case AUDIO_THREAD_FILL_AUDIO:
      printf("%-30s dev:%u hw_level:%u min_cb_level:%u\n", "FILL_AUDIO", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_FILL_AUDIO_TSTAMP:
      printf("%-30s dev:%u tstamp:%s.%09u\n", "FILL_AUDIO_TSTAMP", data1,
             time_str, nsec);
      break;
    case AUDIO_THREAD_FILL_AUDIO_DONE: {
      float f = get_ewma_power_as_float(data3);
      printf("%-30s hw_level:%u total_written:%u power:%f dBFS\n",
             "FILL_AUDIO_DONE", data1, data2, f);
      break;
    }
    case AUDIO_THREAD_WRITE_STREAMS_MIX:
      printf("%-30s write_limit:%u max_offset:%u buffer_avail:%u\n",
             "WRITE_STREAMS_MIX", data1, data2, data3);
      break;
    case AUDIO_THREAD_WRITE_STREAMS_MIXED:
      printf("%-30s written_frames:%u\n", "WRITE_STREAMS_MIXED", data1);
      break;
    case AUDIO_THREAD_WRITE_STREAMS_STREAM:
      printf("%-30s id:%x shm_frames:%u cb_pending:%u\n",
             "WRITE_STREAMS_STREAM", data1, data2, data3);
      break;
    case AUDIO_THREAD_FETCH_STREAM: {
      float f = get_ewma_power_as_float(data3);
      printf("%-30s id:%x cbth:%u power:%f dBFS\n",
             "WRITE_STREAMS_FETCH_STREAM", data1, data2, f);
      break;
    }
    case AUDIO_THREAD_STREAM_ADDED:
      printf("%-30s id:%x dev:%u\n", "STREAM_ADDED", data1, data2);
      break;
    case AUDIO_THREAD_STREAM_REMOVED:
      printf("%-30s id:%x\n", "STREAM_REMOVED", data1);
      break;
      break;
    case AUDIO_THREAD_A2DP_FLUSH:
      printf("%-30s state %u next flush time:%s.%09u\n", "A2DP_FLUSH", data1,
             time_str, nsec);
      break;
    case AUDIO_THREAD_A2DP_THROTTLE_TIME:
      printf("%-30s %u ms, queued:%u\n", "A2DP_THROTTLE_TIME",
             data1 * 1000 + data2 / 1000000, data3);
      break;
    case AUDIO_THREAD_A2DP_WRITE:
      printf("%-30s written:%u queued:%u\n", "A2DP_WRITE", data1, data2);
      break;
    case AUDIO_THREAD_LEA_READ:
      printf("%-30s read:%u started:%u\n", "LEA_READ", data1, data2);
      break;
    case AUDIO_THREAD_LEA_WRITE:
      printf("%-30s written:%u queued:%u\n", "LEA_WRITE", data1, data2);
      break;
    case AUDIO_THREAD_DEV_STREAM_MIX:
      printf("%-30s written:%u read:%u\n", "DEV_STREAM_MIX", data1, data2);
      break;
    case AUDIO_THREAD_CAPTURE_POST:
      printf("%-30s stream:%x thresh:%u rd_buf:%u\n", "CAPTURE_POST", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_CAPTURE_WRITE:
      printf("%-30s stream:%x write:%u shm_fr:%u\n", "CAPTURE_WRITE", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_CONV_COPY:
      printf("%-30s wr_buf:%u shm_writable:%u offset:%u\n", "CONV_COPY", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_STREAM_FETCH_PENDING:
      printf("%-30s id:%x\n", "STREAM_FETCH_PENGING", data1);
      break;
    case AUDIO_THREAD_STREAM_RESCHEDULE:
      printf("%-30s id:%x next_cb_ts:%s.%09u\n", "STREAM_RESCHEDULE", data1,
             time_str, nsec);
      break;
    case AUDIO_THREAD_STREAM_SLEEP_TIME:
      printf("%-30s id:%x wake:%s.%09u\n", "STREAM_SLEEP_TIME", data1, time_str,
             nsec);
      break;
    case AUDIO_THREAD_STREAM_SLEEP_ADJUST:
      printf("%-30s id:%x from:%s.%09u\n", "STREAM_SLEEP_ADJUST", data1,
             time_str, nsec);
      break;
    case AUDIO_THREAD_STREAM_SKIP_CB:
      printf("%-30s id:%x write_offset_0:%u write_offset_1:%u\n",
             "STREAM_SKIP_CB", data1, data2, data3);
      break;
    case AUDIO_THREAD_DEV_SLEEP_TIME:
      printf("%-30s dev:%u wake:%s.%09u\n", "DEV_SLEEP_TIME", data1, time_str,
             nsec);
      break;
    case AUDIO_THREAD_SET_DEV_WAKE:
      printf("%-30s dev:%u hw_level:%u sleep:%u\n", "SET_DEV_WAKE", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_DEV_ADDED:
      printf("%-30s dev:%u\n", "DEV_ADDED", data1);
      break;
    case AUDIO_THREAD_DEV_REMOVED:
      printf("%-30s dev:%u\n", "DEV_REMOVED", data1);
      break;
    case AUDIO_THREAD_IODEV_CB:
      printf("%-30s revents:%u events:%u\n", "IODEV_CB", data1, data2);
      break;
    case AUDIO_THREAD_PB_MSG:
      printf("%-30s msg_id:%u\n", "PB_MSG", data1);
      break;
    case AUDIO_THREAD_ODEV_NO_STREAMS:
      printf("%-30s dev:%u\n", "ODEV_NO_STREAMS", data1);
      break;
    case AUDIO_THREAD_ODEV_LEAVE_NO_STREAMS:
      printf("%-30s dev:%u\n", "ODEV_LEAVE_NO_STREAMS", data1);
      break;
    case AUDIO_THREAD_ODEV_START:
      printf("%-30s dev:%u min_cb_level:%u\n", "ODEV_START", data1, data2);
      break;
    case AUDIO_THREAD_FILL_ODEV_ZEROS:
      printf("%-30s dev:%u write:%u\n", "FILL_ODEV_ZEROS", data1, data2);
      break;
    case AUDIO_THREAD_ODEV_DEFAULT_NO_STREAMS:
      printf("%-30s dev:%u hw_level:%u target:%u\n", "DEFAULT_NO_STREAMS",
             data1, data2, data3);
      break;
    case AUDIO_THREAD_UNDERRUN:
      printf("%-30s dev:%u hw_level:%u total_written:%u\n", "UNDERRUN", data1,
             data2, data3);
      break;
    case AUDIO_THREAD_SEVERE_UNDERRUN:
      printf("%-30s dev:%u\n", "SEVERE_UNDERRUN", data1);
      break;
    case AUDIO_THREAD_CAPTURE_DROP_TIME:
      printf("%-30s time:%09u.%09u\n", "CAPTURE_DROP_TIME", data1, data2);
      break;
    case AUDIO_THREAD_DEV_DROP_FRAMES:
      printf("%-30s dev:%u frames:%u\n", "DEV_DROP_FRAMES", data1, data2);
      break;
    case AUDIO_THREAD_LOOPBACK_PUT:
      printf("%-30s nframes_committed:%u\n", "LOOPBACK_PUT", data1);
      break;
    case AUDIO_THREAD_LOOPBACK_GET:
      printf("%-30s nframes_requested:%u avail:%u\n", "LOOPBACK_GET", data1,
             data2);
      break;
    case AUDIO_THREAD_LOOPBACK_SAMPLE_HOOK:
      printf("%-30s frames_to_copy:%u frames_copied:%u\n", "LOOPBACK_SAMPLE",
             data1, data2);
      break;
    case AUDIO_THREAD_DEV_OVERRUN:
      printf("%-30s dev:%u hw_level:%u\n", "DEV_OVERRUN", data1, data2);
      break;
    case AUDIO_THREAD_DEV_IO_RUN_TIME:
      printf("%-30s wall:%u.%06u user:%u.%06u sys:%u.%06u\n", "DEV_IO_RUN_TIME",
             data1 / 1000000, data1 % 1000000, data2 / 1000000, data2 % 1000000,
             data3 / 1000000, data3 % 1000000);
      break;
    case AUDIO_THREAD_OFFSET_EXCEED_AVAILABLE:
      printf("%-30s dev:%u minimum_offset:%u buffer_available_frames:%u\n",
             "OFFSET_EXCEED_AVAILBLE", data1, data2, data3);
      break;
    case AUDIO_THREAD_WRITE_STREAM_IS_DRAINING:
      printf("%-30s id:%x shm_frames:%u is_draining:%u\n",
             "WRITE_STREAM_IS_DRAINING", data1, data2, data3);
      break;
    case AUDIO_THREAD_UNREASONABLE_AVAILABLE_FRAMES:
      printf(
          "%-30s previous_available:%u previous_write:%u "
          "current_available:%u\n",
          "UNREASONABLE_AVAILABLE_FRAMES", data1, data2, data3);
      break;
    case AUDIO_THREAD_WAKE_DELAY:
      printf("%-30s delay:%09d.%09d\n", "WAKE_DELAY", (int)data1, (int)data2);
      break;
    default:
      printf("%-30s tag:%u\n", "UNKNOWN", tag);
      break;
  }
}

static void print_aligned_audio_debug_info(const struct audio_debug_info* info,
                                           time_t sec_offset,
                                           int32_t nsec_offset) {
  int i, j;

  printf("Audio Debug Stats:\n");
  printf("-------------devices------------\n");
  if (info->num_devs > MAX_DEBUG_DEVS) {
    return;
  }

  for (i = 0; i < info->num_devs; i++) {
    printf("Summary: %s device [%s] %u %u %u \n",
           (info->devs[i].direction == CRAS_STREAM_INPUT) ? "Input" : "Output",
           info->devs[i].dev_name, (unsigned int)info->devs[i].buffer_size,
           (unsigned int)info->devs[i].frame_rate,
           (unsigned int)info->devs[i].num_channels);
    printf("%s dev: %s\n",
           (info->devs[i].direction == CRAS_STREAM_INPUT) ? "Input" : "Output",
           info->devs[i].dev_name);
    printf(
        "dev_idx: %u\n"
        "buffer_size: %u\n"
        "min_buffer_level: %u\n"
        "min_cb_level: %u\n"
        "max_cb_level: %u\n"
        "frame_rate: %u\n"
        "num_channels: %u\n"
        "est_rate_ratio: %lf\n"
        "est_rate_ratio_when_underrun: %lf\n"
        "num_underruns: %u\n"
        "num_underruns_during_nc: %u\n"
        "num_severe_underruns: %u\n"
        "num_samples_dropped: %u\n"
        "highest_hw_level: %u\n"
        "runtime: %u.%09u\n"
        "longest_wake: %u.%09u\n"
        "software_gain_scaler: %lf\n",
        (unsigned int)info->devs[i].dev_idx,
        (unsigned int)info->devs[i].buffer_size,
        (unsigned int)info->devs[i].min_buffer_level,
        (unsigned int)info->devs[i].min_cb_level,
        (unsigned int)info->devs[i].max_cb_level,
        (unsigned int)info->devs[i].frame_rate,
        (unsigned int)info->devs[i].num_channels, info->devs[i].est_rate_ratio,
        info->devs[i].est_rate_ratio_when_underrun,
        (unsigned int)info->devs[i].num_underruns,
        (unsigned int)info->devs[i].num_underruns_during_nc,
        (unsigned int)info->devs[i].num_severe_underruns,
        (unsigned int)info->devs[i].num_samples_dropped,
        (unsigned int)info->devs[i].highest_hw_level,
        (unsigned int)info->devs[i].runtime_sec,
        (unsigned int)info->devs[i].runtime_nsec,
        (unsigned int)info->devs[i].longest_wake_sec,
        (unsigned int)info->devs[i].longest_wake_nsec,
        info->devs[i].internal_gain_scaler);
    printf("\n");
  }

  printf("-------------stream_dump------------\n");
  if (info->num_streams > MAX_DEBUG_STREAMS) {
    return;
  }

  for (i = 0; i < info->num_streams; i++) {
    int channel;
    printf(
        "Summary: %s stream 0x%" PRIx64 " %s %s %u %u 0x%.4x %u %u %x\n",
        (info->streams[i].direction == CRAS_STREAM_INPUT) ? "Input" : "Output",
        info->streams[i].stream_id,
        cras_client_type_str(info->streams[i].client_type),
        cras_stream_type_str(info->streams[i].stream_type),
        (unsigned int)info->streams[i].buffer_frames,
        (unsigned int)info->streams[i].cb_threshold,
        (unsigned int)info->streams[i].effects,
        (unsigned int)info->streams[i].frame_rate,
        (unsigned int)info->streams[i].num_channels,
        (unsigned int)info->streams[i].is_pinned);
    printf("stream: 0x%" PRIx64 " dev: %u\n", info->streams[i].stream_id,
           (unsigned int)info->streams[i].dev_idx);
    printf("direction: %s\n", (info->streams[i].direction == CRAS_STREAM_INPUT)
                                  ? "Input"
                                  : "Output");
    printf("stream_type: %s\n",
           cras_stream_type_str(info->streams[i].stream_type));
    printf("client_type: %s\n",
           cras_client_type_str(info->streams[i].client_type));
    printf(
        "buffer_frames: %u\n"
        "cb_threshold: %u\n"
        "effects: 0x%.4x\n",
        (unsigned int)info->streams[i].buffer_frames,
        (unsigned int)info->streams[i].cb_threshold,
        (unsigned int)info->streams[i].effects);

    printf("active_ap_effects: ");
    print_cras_stream_active_ap_effects(stdout,
                                        info->streams[i].active_ap_effects);
    printf("\n");

    printf(
        "frame_rate: %u\n"
        "num_channels: %u\n"
        "longest_fetch_sec: %u.%09u\n"
        "num_delayed_fetches: %u\n"
        "num_overruns: %u\n"
        "overrun_frames: %u\n"
        "dropped_samples_duration: %u.%09u\n"
        "underrun_duration: %u.%09u\n"
        "is_pinned: %x\n"
        "pinned_dev_idx: %u\n"
        "num_missed_cb: %u\n"
        "%s: %lf\n"
        "runtime: %u.%09u\n"
        "webrtc_apm_forward_blocks_processed: %" PRIu64
        "\n"
        "webrtc_apm_reverse_blocks_processed: %" PRIu64 "\n",
        (unsigned int)info->streams[i].frame_rate,
        (unsigned int)info->streams[i].num_channels,
        (unsigned int)info->streams[i].longest_fetch_sec,
        (unsigned int)info->streams[i].longest_fetch_nsec,
        (unsigned int)info->streams[i].num_delayed_fetches,
        (unsigned int)info->streams[i].num_overruns,
        (unsigned int)info->streams[i].overrun_frames,
        (unsigned int)info->streams[i].dropped_samples_duration_sec,
        (unsigned int)info->streams[i].dropped_samples_duration_nsec,
        (unsigned int)info->streams[i].underrun_duration_sec,
        (unsigned int)info->streams[i].underrun_duration_nsec,
        (unsigned int)info->streams[i].is_pinned,
        (unsigned int)info->streams[i].pinned_dev_idx,
        (unsigned int)info->streams[i].num_missed_cb,
        (info->streams[i].direction == CRAS_STREAM_INPUT) ? "gain" : "volume",
        info->streams[i].stream_volume,
        (unsigned int)info->streams[i].runtime_sec,
        (unsigned int)info->streams[i].runtime_nsec,
        info->streams[i].webrtc_apm_forward_blocks_processed,
        info->streams[i].webrtc_apm_reverse_blocks_processed);
    printf("channel map:");
    for (channel = 0; channel < CRAS_CH_MAX; channel++) {
      printf("%d ", info->streams[i].channel_layout[channel]);
    }
    printf("\n\n");
  }

  printf("Audio Thread Event Log:\n");

  j = info->log.write_pos % info->log.len;
  i = 0;
  printf("start at %d\n", j);
  for (; i < info->log.len; i++) {
    show_alog_tag(&info->log, j, sec_offset, nsec_offset);
    j++;
    j %= info->log.len;
  }
}

static void print_audio_debug_info(const struct audio_debug_info* info) {
  time_t sec_offset;
  int32_t nsec_offset;

  fill_time_offset(&sec_offset, &nsec_offset);

  print_aligned_audio_debug_info(info, sec_offset, nsec_offset);
}

static void audio_debug_info(struct cras_client* client) {
  const struct audio_debug_info* info;
  info = cras_client_get_audio_debug_info(client);
  if (!info) {
    return;
  }
  print_audio_debug_info(info);

  // Signal main thread we are done after the last chunk.
  signal_done();
}

static void show_mainlog_tag(const struct main_thread_event_log* log,
                             unsigned int tag_idx,
                             int32_t sec_offset,
                             int32_t nsec_offset) {
  unsigned int tag = (log->log[tag_idx].tag_sec >> 24) & 0xff;
  unsigned int sec = log->log[tag_idx].tag_sec & 0x00ffffff;
  unsigned int nsec = log->log[tag_idx].nsec;
  unsigned int data1 = log->log[tag_idx].data1;
  unsigned int data2 = log->log[tag_idx].data2;
  unsigned int data3 = log->log[tag_idx].data3;
  time_t lt;
  struct tm t;

  // Skip unused log entries.
  if (log->log[tag_idx].tag_sec == 0 && log->log[tag_idx].nsec == 0) {
    return;
  }

  // Convert from monotomic raw clock to realtime clock.
  convert_time(&sec, &nsec, sec_offset, nsec_offset);
  lt = sec;
  gmtime_r(&lt, &t);
  strftime(time_str, 128, "%Y-%m-%dT%H:%M:%S", &t);

  printf("%s.%09u cras mainlog  ", time_str, nsec);

  switch (tag) {
    case MAIN_THREAD_DEV_CLOSE:
      printf("%-30s dev %u\n", "DEV_CLOSE", data1);
      break;
    case MAIN_THREAD_DEV_DISABLE:
      printf("%-30s dev %u force %u\n", "DEV_DISABLE", data1, data2);
      break;
    case MAIN_THREAD_DEV_INIT:
      printf("%-30s dev %u ch %u rate %u\n", "DEV_INIT", data1, data2, data3);
      break;
    case MAIN_THREAD_DEV_REOPEN:
      printf("%-30s new ch %u old ch %u rate %u\n", "DEV_REOPEN", data1, data2,
             data3);
      break;
    case MAIN_THREAD_ADD_ACTIVE_NODE:
      printf("%-30s dev %u\n", "ADD_ACTIVE_NODE", data1);
      break;
    case MAIN_THREAD_SELECT_NODE:
      printf("%-30s dev %u\n", "SELECT_NODE", data1);
      break;
    case MAIN_THREAD_ADD_TO_DEV_LIST:
      printf("%-30s dev %u %s\n", "ADD_TO_DEV_LIST", data1,
             (data2 == CRAS_STREAM_OUTPUT) ? "output" : "input");
      break;
    case MAIN_THREAD_NODE_PLUGGED:
      printf("%-30s dev %u %s\n", "NODE_PLUGGED", data1,
             data2 ? "plugged" : "unplugged");
      break;
    case MAIN_THREAD_INPUT_NODE_GAIN:
      printf("%-30s dev %u gain %u\n", "INPUT_NODE_GAIN", data1, data2);
      break;
    case MAIN_THREAD_OUTPUT_NODE_VOLUME:
      printf("%-30s dev %u volume %u\n", "OUTPUT_NODE_VOLUME", data1, data2);
      break;
    case MAIN_THREAD_SET_DISPLAY_ROTATION:
      printf("%-30s id %u rotation %u\n", "SET_DISPLAY_ROTATION", data1, data2);
      break;
    case MAIN_THREAD_SET_OUTPUT_USER_MUTE:
      printf("%-30s mute %u\n", "SET_OUTPUT_USER_MUTE", data1);
      break;
    case MAIN_THREAD_RESUME_DEVS:
      printf("RESUME_DEVS\n");
      break;
    case MAIN_THREAD_SUSPEND_DEVS:
      printf("SUSPEND_DEVS\n");
      break;
    case MAIN_THREAD_NC_BLOCK_STATE:
      printf("%-30s %s: non_echo=%u disallow=%u\n", "NC_BLOCK_STATE",
             (data1 ? "NC deactivated" : "NC activated"), data2, data3);
      break;
    case MAIN_THREAD_DEV_DSP_OFFLOAD:
      printf("%-30s dev %u %s %s\n", "DEV_DSP_OFFLOAD", data1,
             data2 ? "enable" : "disable", data3 ? "failed" : "ok");
      break;
    case MAIN_THREAD_STREAM_ADDED:
      printf("%-30s %s stream 0x%x buffer frames %u\n", "STREAM_ADDED",
             (data2 == CRAS_STREAM_OUTPUT ? "output" : "input"), data1, data3);
      break;
    case MAIN_THREAD_STREAM_ADDED_INFO_FORMAT:
      printf("%-30s stream 0x%x format %u (%s) channels %u\n",
             "STREAM_ADDED_INFO_FORMAT", data1, data2,
             snd_pcm_format_name(data2), data3);
      break;
    case MAIN_THREAD_STREAM_REMOVED:
      printf("%-30s stream 0x%x\n", "STREAM_REMOVED", data1);
      break;
    case MAIN_THREAD_NOISE_CANCELLATION:
      printf("%-30s %s\n", "NOISE_CANCELLATION",
             data1 ? "enabled" : "disabled");
      break;
    case MAIN_THREAD_STYLE_TRANSFER:
      printf("%-30s %s\n", "STYLE_TRANSFER", data1 ? "enabled" : "disabled");
      break;
    case MAIN_THREAD_VAD_TARGET_CHANGED: {
      printf(
          "%-30s target_stream 0x%x target_client_stream 0x%x "
          "server_vad_stream 0x%x\n",
          "VAD_TARGET_CHANGED", data1, data2, data3);
      break;
    }
    case MAIN_THREAD_FORCE_RESPECT_UI_GAINS:
      printf("%-30s %s\n", "FORCE_RESPECT_UI_GAINS",
             data1 ? "enabled" : "disabled");
      break;
    default:
      printf("%-30s\n", "UNKNOWN");
      break;
  }
}

static void show_btlog_tag(const struct cras_bt_event_log* log,
                           unsigned int tag_idx,
                           int32_t sec_offset,
                           int32_t nsec_offset) {
  unsigned int tag = (log->log[tag_idx].tag_sec >> 24) & 0xff;
  unsigned int sec = log->log[tag_idx].tag_sec & 0x00ffffff;
  unsigned int nsec = log->log[tag_idx].nsec;
  unsigned int data1 = log->log[tag_idx].data1;
  unsigned int data2 = log->log[tag_idx].data2;
  time_t lt;
  struct tm t;

  // Skip unused log entries.
  if (log->log[tag_idx].tag_sec == 0 && log->log[tag_idx].nsec == 0) {
    return;
  }

  // Convert from monotonic raw clock to realtime clock.
  convert_time(&sec, &nsec, sec_offset, nsec_offset);
  lt = sec;
  gmtime_r(&lt, &t);
  strftime(time_str, 128, "%Y-%m-%dT%H:%M:%S", &t);

  printf("%s.%09u cras btlog  ", time_str, nsec);

  switch (tag) {
    case BT_ADAPTER_ADDED:
      printf("%-30s\n", "ADAPTER_ADDED");
      break;
    case BT_ADAPTER_REMOVED:
      printf("%-30s\n", "ADAPTER_REMOVED");
      break;
    case BT_A2DP_CONFIGURED:
      printf("%-30s connected profiles 0x%.2x\n", "A2DP_CONFIGURED", data1);
      break;
    case BT_A2DP_REQUEST_START:
      printf("%-30s %s\n", "A2DP_REQUEST_START", data1 ? "success" : "failed");
      break;
    case BT_A2DP_REQUEST_STOP:
      printf("%-30s %s\n", "A2DP_REQUEST_STOP", data1 ? "success" : "failed");
      break;
    case BT_A2DP_START:
      printf("%-30s\n", "A2DP_START");
      break;
    case BT_A2DP_SUSPENDED:
      printf("%-30s\n", "A2DP_SUSPENDED");
      break;
    case BT_A2DP_SET_VOLUME:
      printf("%-30s %u\n", "A2DP_SET_VOLUME", data1);
      break;
    case BT_A2DP_SET_ABS_VOLUME_SUPPORT:
      printf("%-30s %u\n", "A2DP_SET_ABS_VOLUME_SUPPORT", data1);
      break;
    case BT_A2DP_UPDATE_VOLUME:
      printf("%-30s %u\n", "A2DP_UPDATE_VOLUME", data1);
      break;
    case BT_AUDIO_GATEWAY_INIT:
      printf("%-30s supported profiles 0x%.2x\n", "AUDIO_GATEWAY_INIT", data1);
      break;
    case BT_AUDIO_GATEWAY_START:
      printf(
          "%-30s offload path is %s%s, hfp_caps bitmask is %u\n",
          "AUDIO_GATEWAY_START", (data1 >> 1) ? "supported" : "not supported",
          (data1 >> 1) ? ((data1 & 1) ? " and enabled" : " but disabled") : "",
          data2);
      break;
    case BT_AVAILABLE_CODECS:
      printf("%-30s codec #%u id %u\n", "AVAILABLE_CODECS", data1, data2);
      break;
    case BT_CODEC_SELECTION:
      printf("%-30s dir %u codec id %u\n", "CODEC_SELECTION", data1, data2);
      break;
    case BT_DEV_ADDED:
      printf("%-30s a2dp %s and hfp %s with codec capability bitmask %u\n",
             "DEV_ADDED", data1 ? "supported" : "not supported",
             (data2 & 1) ? "supported" : "not supported", data2 >> 1);
      break;
    case BT_DEV_REMOVED:
      printf("%-30s\n", "DEV_REMOVED");
      break;
    case BT_DEV_CONNECTED:
      printf("%-30s supported profiles 0x%.2x stable_id 0x%08x\n",
             "DEV_CONNECTED", data1, pseudonymize_stable_id(data2));
      break;
    case BT_DEV_DISCONNECTED:
      printf("%-30s supported profiles 0x%.2x stable_id 0x%08x\n",
             "DEV_DISCONNECTED", data1, pseudonymize_stable_id(data2));
      break;
    case BT_DEV_CONN_WATCH_CB:
      printf("%-30s %u retries left, supported profiles 0x%.2x\n",
             "DEV_CONN_WATCH_CB", data1, data2);
      break;
    case BT_DEV_SUSPEND_CB:
      printf("%-30s profiles supported %u, reason %u\n", "DEV_SUSPEND_CB",
             data1, data2);
      break;
    case BT_HFP_HF_INDICATOR:
      printf("%-30s HF read AG %s indicator\n", "HFP_HF_INDICATOR",
             data1 ? "enabled" : "supported");
      break;
    case BT_HFP_SET_SPEAKER_GAIN:
      printf("%-30s HF set speaker gain %u\n", "HFP_SET_SPEAKER_GAIN", data1);
      break;
    case BT_HFP_UPDATE_SPEAKER_GAIN:
      printf("%-30s HF update speaker gain %u\n", "HFP_UPDATE_SPEAKER_GAIN",
             data1);
      break;
    case BT_HFP_AUDIO_DISCONNECTED:
      printf("%-30s HF audio disconnected\n", "HFP_AUDIO_DISCONNECTED");
      break;
    case BT_HFP_NEW_CONNECTION:
      printf("%-30s\n", "HFP_NEW_CONNECTION");
      break;
    case BT_HFP_REQUEST_DISCONNECT:
      printf("%-30s\n", "HFP_REQUEST_DISCONNECT");
      break;
    case BT_HFP_SUPPORTED_FEATURES:
      printf("%-30s role %s features 0x%.4x\n", "HFP_SUPPORTED_FEATURES",
             data1 ? "AG" : "HF", data2);
      break;
    case BT_HSP_NEW_CONNECTION:
      printf("%-30s\n", "HSP_NEW_CONNECTION");
      break;
    case BT_HSP_REQUEST_DISCONNECT:
      printf("%-30s\n", "HSP_REQUEST_DISCONNECT");
      break;
    case BT_LEA_AUDIO_CONF_UPDATED:
      printf("%-30s gid %d direction %u contexts %u\n",
             "LEA_AUDIO_CONF_UPDATED", data1, data2 >> 16, data2 & 0xffff);
      break;
    case BT_LEA_SET_GROUP_VOLUME:
      printf("%-30s gid %d volume %u\n", "LEA_SET_GROUP_VOLUME", data1, data2);
      break;
    case BT_LEA_GROUP_CONNECTED:
      printf("%-30s gid %d\n", "LEA_GROUP_CONNECTED", data1);
      break;
    case BT_LEA_GROUP_DISCONNECTED:
      printf("%-30s gid %d\n", "LEA_GROUP_DISCONNECTED", data1);
      break;
    case BT_LEA_GROUP_NODE_STATUS:
      printf("%-30s gid %d status %d\n", "LEA_GROUP_NODE_STATUS", data1, data2);
      break;
    case BT_LEA_GROUP_STATUS:
      printf("%-30s gid %d status %d\n", "LEA_GROUP_STATUS", data1, data2);
      break;
    case BT_LEA_GROUP_VOLUME_CHANGED:
      printf("%-30s gid %d volume %u\n", "LEA_GROUP_VOLUME_CHANGED", data1,
             data2);
      break;
    case BT_MANAGER_ADDED:
      printf("%-30s\n", "MANAGER_ADDED");
      break;
    case BT_MANAGER_REMOVED:
      printf("%-30s\n", "MANAGER_REMOVED");
      break;
    case BT_NEW_AUDIO_PROFILE_AFTER_CONNECT:
      printf("%-30s old 0x%.2x, new 0x%.2x\n",
             "NEW_AUDIO_PROFILE_AFTER_CONNECT", data1, data2);
      break;
    case BT_RESET:
      printf("%-30s\n", "RESET");
      break;
    case BT_SCO_CONNECT:
      printf("%-30s %s sk %d\n", "SCO_CONNECT", data1 ? "success" : "failed",
             (int)data2);
      break;
    case BT_SCO_DISCONNECT:
      printf("%-30s %s\n", "SCO_DISCONNECT", data1 ? "success" : "failed");
      break;
    case BT_TRANSPORT_RELEASE:
      printf("%-30s\n", "TRANSPORT_RELEASE");
      break;
    case BT_HCI_ENABLED:
      printf("%-30s hci%d enabled %d\n", "HCI_ENABLED", data1, data2);
      break;
    case BT_HFP_TELEPHONY_EVENT:
      printf("%-30s event:%s call state:%s\n", "HFP_TELEPHONY_EVENT",
             cras_bt_hfp_telephony_event_to_str(
                 (enum CRAS_BT_HFP_TELEPHONY_EVENT)data1),
             cras_bt_hfp_call_state_to_str((enum CRAS_BT_HFP_CALL_STATE)data2));
      break;
    default:
      printf("%-30s\n", "UNKNOWN");
      break;
  }
}

static void convert_to_time_str(const struct timespec* ts,
                                time_t sec_offset,
                                int32_t nsec_offset) {
  time_t lt = ts->tv_sec;
  struct tm t;
  unsigned int time_nsec;

  // Assuming tv_nsec doesn't exceed 10^9
  time_nsec = ts->tv_nsec;
  convert_time((unsigned int*)&lt, &time_nsec, sec_offset, nsec_offset);
  gmtime_r(&lt, &t);
  strftime(time_str, 128, "%Y-%m-%dT%H:%M:%S", &t);
  snprintf(time_str + strlen(time_str), 128 - strlen(time_str), ".%09u",
           time_nsec);
}

static void cras_bt_debug_info(struct cras_client* client) {
  const struct cras_bt_debug_info* info;
  time_t sec_offset;
  int32_t nsec_offset;
  int i, j;
  struct timespec ts;
  struct packet_status_logger wbs_logger;

  info = cras_client_get_bt_debug_info(client);
  fill_time_offset(&sec_offset, &nsec_offset);
  j = info->bt_log.write_pos;
  i = 0;

  printf("Bluetooth Stack: %s\n", info->floss_enabled ? "Floss" : "BlueZ");
  printf("BT debug log:\n");
  for (; i < info->bt_log.len; i++) {
    show_btlog_tag(&info->bt_log, j, sec_offset, nsec_offset);
    j++;
    j %= info->bt_log.len;
  }

  printf("-------------WBS packet loss------------\n");
  wbs_logger = info->wbs_logger;

  packet_status_logger_begin_ts(&wbs_logger, &ts);
  convert_to_time_str(&ts, sec_offset, nsec_offset);
  printf("%s [begin]\n", time_str);

  packet_status_logger_end_ts(&wbs_logger, &ts);
  convert_to_time_str(&ts, sec_offset, nsec_offset);
  printf("%s [end]\n", time_str);

  printf("In hex format:\n");
  packet_status_logger_dump_hex(&wbs_logger);

  printf("In binary format:\n");
  packet_status_logger_dump_binary(&wbs_logger);

  // Signal main thread we are done after the last chunk.
  signal_done();
}

static void main_thread_debug_info(struct cras_client* client) {
  const struct main_thread_debug_info* info;
  time_t sec_offset;
  int32_t nsec_offset;
  int i, j;

  info = cras_client_get_main_thread_debug_info(client);
  fill_time_offset(&sec_offset, &nsec_offset);
  j = info->main_log.write_pos;
  i = 0;
  printf("Main debug log:\n");
  for (; i < info->main_log.len; i++) {
    show_mainlog_tag(&info->main_log, j, sec_offset, nsec_offset);
    j++;
    j %= info->main_log.len;
  }

  // Signal main thread we are done after the last chunk.
  signal_done();
}

static void print_cras_audio_thread_snapshot(
    const struct cras_audio_thread_snapshot* snapshot,
    time_t sec_offset,
    int32_t nsec_offset) {
  struct timespec ts;
  cras_timespec_to_timespec(&ts, &snapshot->timestamp);
  convert_to_time_str(&ts, sec_offset, nsec_offset);

  printf("-------------snapshot------------\n");
  printf("Event time: %s\n", time_str);
  printf("Event type: %s\n",
         audio_thread_event_type_to_str(snapshot->event_type));
  print_aligned_audio_debug_info(&snapshot->audio_debug_info, sec_offset,
                                 nsec_offset);
}

static void audio_thread_snapshots(struct cras_client* client) {
  const struct cras_audio_thread_snapshot_buffer* snapshot_buffer;
  uint32_t i;
  int j;
  int count = 0;
  time_t sec_offset;
  int32_t nsec_offset;

  snapshot_buffer = cras_client_get_audio_thread_snapshot_buffer(client);
  fill_time_offset(&sec_offset, &nsec_offset);
  i = snapshot_buffer->pos;
  for (j = 0; j < CRAS_MAX_AUDIO_THREAD_SNAPSHOTS; j++) {
    if (snapshot_buffer->snapshots[i].timestamp.tv_sec ||
        snapshot_buffer->snapshots[i].timestamp.tv_nsec) {
      print_cras_audio_thread_snapshot(&snapshot_buffer->snapshots[i],
                                       sec_offset, nsec_offset);
      count++;
    }
    i++;
    i %= CRAS_MAX_AUDIO_THREAD_SNAPSHOTS;
  }
  printf("There are %d, snapshots.\n", count);

  // Signal main thread we are done after the last chunk.
  signal_done();
}

static int start_stream(struct cras_client* client,
                        cras_stream_id_t* stream_id,
                        struct cras_stream_params* params,
                        float stream_volume) {
  int rc;

  if (pin_device_id) {
    rc =
        cras_client_add_pinned_stream(client, pin_device_id, stream_id, params);
  } else {
    rc = cras_client_add_stream(client, stream_id, params);
  }
  if (rc < 0) {
    fprintf(stderr, "adding a stream %d\n", rc);
    return rc;
  }
  return cras_client_set_stream_volume(client, *stream_id, stream_volume);
}

static int parse_channel_layout(char* channel_layout_str,
                                int8_t channel_layout[CRAS_CH_MAX]) {
  int i = 0;
  char* chp;

  chp = strtok(channel_layout_str, ",");
  while (chp && i < CRAS_CH_MAX) {
    int channel_layout_int;
    int rc = parse_int(chp, &channel_layout_int);
    if (rc < 0) {
      return rc;
    }
    channel_layout[i++] = channel_layout_int;
    chp = strtok(NULL, ",");
  }

  return 0;
}

static void run_aecdump(struct cras_client* client,
                        uint64_t stream_id,
                        int start) {
  int aecdump_fd;
  if (start) {
    aecdump_fd = open(aecdump_file, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (aecdump_fd == -1) {
      printf("Fail to open file %s", aecdump_file);
      return;
    }

    printf("Dumping AEC info to %s, stream %" PRId64 ", fd %d\n", aecdump_file,
           stream_id, aecdump_fd);
    cras_client_set_aec_dump(client, stream_id, 1, aecdump_fd);
  } else {
    cras_client_set_aec_dump(client, stream_id, 0, -1);
    printf("Close AEC dump file %s\n", aecdump_file);
  }
}

static unsigned int read_dev_idx(int tty) {
  char buf[16];
  int pos = 0;

  do {
    if (read(tty, buf + pos, 1) < 1) {
      break;
    }
  } while (*(buf + pos) != '\n' && ++pos < 16);
  buf[pos] = '\n';
  int idx;
  int rc = parse_int(buf, &idx);
  if (rc < 0) {
    /* If error occurs this will return 0. Since we're in a test
     * tool, just pretend as setting NO_DEVICE(value 0).*/
    return 0;
  }
  return idx;
}

static int run_file_io_stream(struct cras_client* client,
                              int fd,
                              enum CRAS_STREAM_DIRECTION direction,
                              size_t block_size,
                              enum CRAS_STREAM_TYPE stream_type,
                              size_t rate,
                              snd_pcm_format_t format,
                              size_t num_channels,
                              uint32_t flags,
                              int is_loopback,
                              int post_dsp) {
  int rc, tty;
  struct cras_stream_params* params;
  cras_unified_cb_t aud_cb;
  cras_stream_id_t stream_id = 0;
  int stream_playing = 0;
  int* pfd = malloc(sizeof(*pfd));
  *pfd = fd;
  fd_set poll_set;
  struct timespec sleep_ts;
  float volume_scaler = 1.0;
  size_t sys_volume = 100;
  int mute = 0;
  int8_t layout[CRAS_CH_MAX];

  // Set the sleep interval between latency/RMS prints.
  sleep_ts.tv_sec = 1;
  sleep_ts.tv_nsec = 0;

  // Open the pipe file descriptor.
  rc = pipe(pipefd);
  if (rc == -1) {
    perror("failed to open pipe");
    free(pfd);
    return -errno;
  }

  // Reset the total RMS value.
  total_rms_sqr_sum = 0;
  total_rms_size = 0;

  aud_format = cras_audio_format_create(format, rate, num_channels);
  if (aud_format == NULL) {
    close(pipefd[0]);
    close(pipefd[1]);
    free(pfd);
    return -ENOMEM;
  }

  if (channel_layout) {
    // Set channel layout to format
    parse_channel_layout(channel_layout, layout);
    cras_audio_format_set_channel_layout(aud_format, layout);
  }

  if (direction == CRAS_STREAM_OUTPUT) {
    aud_cb = put_samples;
    if (fd == 0) {
      aud_cb = put_stdin_samples;
    }

    params = cras_client_unified_params_create(direction, block_size,
                                               stream_type, flags, pfd, aud_cb,
                                               stream_error, aud_format);
  } else {
    params = cras_client_stream_params_create(
        direction, block_size, block_size, /*unused=*/0, stream_type, flags,
        pfd, got_samples, stream_error, aud_format);
  }
  if (params == NULL) {
    return -ENOMEM;
  }

  cras_client_stream_params_set_effects_for_testing(params, effects);

  cras_client_run_thread(client);
  if (is_loopback) {
    enum CRAS_NODE_TYPE type = CRAS_NODE_TYPE_POST_MIX_PRE_DSP;
    switch (post_dsp) {
      case 1:
        type = CRAS_NODE_TYPE_POST_DSP;
        break;
      case 2:
        type = CRAS_NODE_TYPE_POST_DSP_DELAYED;
        break;
    }

    cras_client_connected_wait(client);
    pin_device_id =
        cras_client_get_first_dev_type_idx(client, type, CRAS_STREAM_INPUT);
  }

  stream_playing = start_stream(client, &stream_id, params, volume_scaler) == 0;

  // To simulate the special behavior that client aborts immediately after
  // stream creation by using --play_short_sound 0
  if (play_short_sound && (play_short_sound_periods == 0)) {
    keep_looping = 0;
  }

  tty = open("/dev/tty", O_RDONLY);

  // There could be no terminal available when run in autotest.
  if (tty == -1) {
    perror("warning: failed to open /dev/tty");
  }

  while (keep_looping) {
    char input;
    int nread;
    unsigned int dev_idx;

    FD_ZERO(&poll_set);
    if (tty >= 0) {
      FD_SET(tty, &poll_set);
    }
    FD_SET(pipefd[0], &poll_set);
    pselect(MAX(tty, pipefd[0]) + 1, &poll_set, NULL, NULL,
            show_latency || show_rms ? &sleep_ts : NULL, NULL);

    if (stream_playing && show_latency) {
      print_last_latency();
    }

    if (stream_playing && show_rms) {
      print_last_rms();
    }

    if (tty < 0 || !FD_ISSET(tty, &poll_set)) {
      continue;
    }

    nread = read(tty, &input, 1);
    if (nread < 1) {
      fprintf(stderr, "Error reading stdin\n");
      return nread;
    }
    switch (input) {
      case 'a':
        dev_idx = read_dev_idx(tty);
        cras_client_set_aec_ref(client, stream_id, dev_idx);
        printf("Setting AEC ref to dev: %u", dev_idx);
        break;
      case 'p':
        pause_client = !pause_client;
        break;
      case 'i':
        pause_a_reply = 1;
        break;
      case 'q':
        terminate_stream_loop();
        break;
      case 's':
        if (stream_playing) {
          break;
        }

        // If started by hand keep running after it finishes.
        exit_after_done_playing = 0;

        stream_playing =
            start_stream(client, &stream_id, params, volume_scaler) == 0;
        break;
      case 'r':
        if (!stream_playing) {
          break;
        }
        cras_client_rm_stream(client, stream_id);
        stream_playing = 0;
        break;
      case 'u':
        volume_scaler = MIN(volume_scaler + 0.1, 1.0);
        cras_client_set_stream_volume(client, stream_id, volume_scaler);
        break;
      case 'd':
        volume_scaler = MAX(volume_scaler - 0.1, 0.0);
        cras_client_set_stream_volume(client, stream_id, volume_scaler);
        break;
      case 'k':
        sys_volume = MIN(sys_volume + 1, 100);
        cras_client_set_system_volume(client, sys_volume);
        break;
      case 'j':
        sys_volume = sys_volume == 0 ? 0 : sys_volume - 1;
        cras_client_set_system_volume(client, sys_volume);
        break;
      case 'm':
        mute = !mute;
        cras_client_set_system_mute(client, mute);
        break;
      case '@':
        print_device_lists(client);
        break;
      case '#':
        print_attached_client_list(client);
        break;
      case 'v':
        printf(
            "Volume: %zu%s Min dB: %ld Max dB: %ld\n"
            "Capture: %s\n",
            cras_client_get_system_volume(client),
            cras_client_get_system_muted(client) ? "(Muted)" : "",
            cras_client_get_system_min_volume(client),
            cras_client_get_system_max_volume(client),
            cras_client_get_system_capture_muted(client) ? "Muted"
                                                         : "Not muted");
        break;
      case '\'':
        play_short_sound_periods_left = play_short_sound_periods;
        break;
      case '\n':
        break;
      default:
        printf("Invalid key\n");
        break;
    }
  }

  if (show_total_rms) {
    print_total_rms();
  }

  cras_client_stop(client);

  cras_audio_format_destroy(aud_format);
  cras_client_stream_params_destroy(params);
  free(pfd);

  close(pipefd[0]);
  close(pipefd[1]);

  if (ooo_ts_encountered) {
    return EINVAL;
  }

  return 0;
}

static int run_capture(struct cras_client* client,
                       const char* file,
                       size_t block_size,
                       enum CRAS_STREAM_TYPE stream_type,
                       size_t rate,
                       snd_pcm_format_t format,
                       size_t num_channels,
                       uint32_t flags,
                       int is_loopback,
                       int post_dsp) {
  int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (fd == -1) {
    perror("failed to open file");
    return -errno;
  }

  int rc = run_file_io_stream(client, fd, CRAS_STREAM_INPUT, block_size,
                              stream_type, rate, format, num_channels, flags,
                              is_loopback, post_dsp);
  close(fd);
  return rc;
}

static int run_playback(struct cras_client* client,
                        const char* file,
                        size_t block_size,
                        enum CRAS_STREAM_TYPE stream_type,
                        size_t rate,
                        snd_pcm_format_t format,
                        size_t num_channels) {
  int fd;

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    perror("failed to open file");
    return -errno;
  }

  int rc = run_file_io_stream(client, fd, CRAS_STREAM_OUTPUT, block_size,
                              stream_type, rate, format, num_channels, 0, 0, 0);
  close(fd);
  return rc;
}

static void print_server_info(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  print_system_volumes(client);
  print_user_muted(client);
  print_device_lists(client);
  print_attached_client_list(client);
  print_active_stream_info(client);
}

static void show_audio_thread_snapshots(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  cras_client_update_audio_thread_snapshots(client, audio_thread_snapshots);

  wait_done_timeout(2);
}

static void show_audio_debug_info(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  cras_client_update_audio_debug_info(client, audio_debug_info);

  wait_done_timeout(2);
}

static void show_cras_bt_debug_info(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  cras_client_update_bt_debug_info(client, cras_bt_debug_info);

  wait_done_timeout(2);
}

static void show_main_thread_debug_info(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  cras_client_update_main_thread_debug_info(client, main_thread_debug_info);

  wait_done_timeout(2);
}

static void hotword_models_cb(struct cras_client* client,
                              const char* hotword_models) {
  printf("Hotword models: %s\n", hotword_models);
  signal_done();
}

static void print_hotword_models(struct cras_client* client,
                                 cras_node_id_t id) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);
  cras_client_get_hotword_models(client, id, hotword_models_cb);

  wait_done_timeout(2);
}

static void request_floop_mask(struct cras_client* client, int mask) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);
  int32_t idx = cras_client_get_floop_dev_idx_by_client_types(client, mask);
  printf("flexible loopback dev id: %" PRId32 " \n", idx);
}

static void dsp_offload_infos_cb(struct cras_client* client,
                                 uint32_t num_infos,
                                 struct cras_dsp_offload_info* infos) {
  printf("There are %u devices supporting DSP offload:\n", num_infos);
  if (num_infos == 0) {
    signal_done();
    return;
  }

  printf("\tCRAS Dev | DSP Pipeline     Pattern : Status\n");
  for (uint32_t i = 0; i < num_infos; i++) {
    printf("\t     %-3u ----> %-3u %16s : %s\n", infos[i].iodev_idx,
           infos[i].dsp_pipe_id, infos[i].dsp_pattern,
           cras_dsp_proc_state_to_str(infos[i].state));
  }
  signal_done();
}

static void print_dsp_offload_infos(struct cras_client* client) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);
  cras_client_get_dsp_offload_info(client, dsp_offload_infos_cb);

  wait_done_timeout(2);
}

static void check_output_plugged(struct cras_client* client, const char* name) {
  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  printf("%s\n", cras_client_output_dev_plugged(client, name) ? "Yes" : "No");
}

// Repeatedly mute and un-mute the output until there is an error.
static void mute_loop_test(struct cras_client* client, int auto_reconnect) {
  int mute = 0;
  int rc;

  if (auto_reconnect) {
    cras_client_run_thread(client);
  }
  while (1) {
    rc = cras_client_set_user_mute(client, mute);
    printf("cras_client_set_user_mute(%d): %d\n", mute, rc);
    if (rc != 0 && !auto_reconnect) {
      return;
    }
    mute = !mute;
    sleep(2);
  }
}

static void show_atlog(time_t sec_offset,
                       int32_t nsec_offset,
                       struct audio_thread_event_log* log,
                       int len,
                       uint64_t missing) {
  int i;
  printf("Audio Thread Event Log:\n");

  if (missing) {
    printf("%" PRIu64 " logs are missing.\n", missing);
  }

  for (i = 0; i < len; ++i) {
    show_alog_tag(log, i, sec_offset, nsec_offset);
  }
}

static void unlock_main_thread(struct cras_client* client) {
  signal_done();
}

static void cras_show_continuous_atlog(struct cras_client* client) {
  struct audio_thread_event_log log;
  static time_t sec_offset;
  static int32_t nsec_offset;
  static uint64_t atlog_read_idx = 0, missing;
  int len;

  cras_client_run_thread(client);
  cras_client_connected_wait(client);  // To synchronize data.
  cras_client_get_atlog_access(client, unlock_main_thread);

  if (wait_done_timeout(2)) {
    goto fail;
  }

  fill_time_offset(&sec_offset, &nsec_offset);

  // Set stdout buffer to line buffered mode.
  setlinebuf(stdout);

  while (1) {
    len = cras_client_read_atlog(client, &atlog_read_idx, &missing, &log);

    if (len < 0) {
      break;
    }
    if (len > 0) {
      show_atlog(sec_offset, nsec_offset, &log, len, missing);
    }
    nanosleep(&follow_atlog_sleep_ts, NULL);
  }
fail:
  printf("Failed to get audio thread log.\n");
}

static int parse_client_type(const char* arg, enum CRAS_CLIENT_TYPE* out) {
  char* str_end;
  long enum_value = strtol(arg, &str_end, 0);

  // If arg is not a number, use it as a keyword to search the enum names.
  if (str_end == arg || *str_end) {
    int i, nmatch = 0;
    for (i = 0; i < CRAS_NUM_CLIENT_TYPE; i++) {
      if (!strcasestr(cras_client_type_str(i), arg)) {
        continue;
      }
      nmatch++;
      enum_value = i;
    }

    if (!nmatch) {
      fprintf(stderr, "Invalid --client_type argument: not found\n");
      return -EINVAL;
    }
    if (nmatch > 1) {
      fprintf(stderr, "Ambiguous --client_type argument: %d matches\n", nmatch);
      return -EINVAL;
    }
  }

  *out = enum_value;
  return 0;
}

static int override_client_type(struct cras_client* client,
                                enum CRAS_CLIENT_TYPE new_type) {
  if (new_type != CRAS_CLIENT_TYPE_TEST) {
    fprintf(stderr, "Overriding client type to %s\n",
            cras_client_type_str(new_type));
  }

  int rc = cras_client_set_client_type(client, new_type);
  if (rc) {
    fprintf(stderr, "Failed to set client type %d: rc = %d\n", new_type, rc);
    return rc;
  }

  return 0;
}

// clang-format off
static struct option long_options[] = {
	{"show_latency",        no_argument,            &show_latency, 1},
	{"show_rms",            no_argument,            &show_rms, 1},
	{"show_total_rms",      no_argument,            &show_total_rms, 1},
	{"select_input",        required_argument,      0, 'a'},
	{"block_size",          required_argument,      0, 'b'},
	{"num_channels",        required_argument,      0, 'c'},
	{"duration_seconds",    required_argument,      0, 'd'},
	{"dump_events",         no_argument,            0, 'e'},
	{"format",              required_argument,      0, 'f'},
	{"capture_gain",        required_argument,      0, 'g'},
	{"help",                no_argument,            0, 'h'},
	{"dump_server_info",    no_argument,            0, 'i'},
	{"check_output_plugged",required_argument,      0, 'j'},
	{"add_active_input",    required_argument,      0, 'k'},
	{"dump_dsp",            no_argument,            0, 'l'},
	{"dump_audio_thread",   no_argument,            0, 'm'},
	{"syslog_mask",         required_argument,      0, 'n'},
	{"channel_layout",      required_argument,      0, 'o'},
	{"get_aec_group_id",    no_argument,            0, 'p'},
	{"user_mute",           required_argument,      0, 'q'},
	{"rate",                required_argument,      0, 'r'},
	{"reload_dsp",          no_argument,            0, 's'},
	{"add_active_output",   required_argument,      0, 't'},
	{"mute",                required_argument,      0, 'u'},
	{"volume",              required_argument,      0, 'v'},
	{"set_node_volume",     required_argument,      0, 'w'},
	{"plug",                required_argument,      0, 'x'},
	{"select_output",       required_argument,      0, 'y'},
	{"playback_delay_us",   required_argument,      0, 'z'},
	{"capture_mute",        required_argument,      0, '0'},
	{"rm_active_input",     required_argument,      0, '1'},
	{"rm_active_output",    required_argument,      0, '2'},
	{"swap_left_right",     required_argument,      0, '3'},
	{"version",             no_argument,            0, '4'},
	{"add_test_dev",        required_argument,      0, '5'},
	{"listen_for_hotword",  required_argument,      0, '7'},
	{"pin_device",          required_argument,      0, '8'},
	{"suspend",             required_argument,      0, '9'},
	{"set_node_gain",       required_argument,      0, ':'},
	{"play_short_sound",    required_argument,      0, '!'},
	{"set_hotword_model",   required_argument,      0, '<'},
	{"get_hotword_models",  required_argument,      0, '>'},
	{"post_dsp",            required_argument,      0, 'A'},
	{"stream_id",           required_argument,      0, 'B'},
	{"capture_file",        required_argument,      0, 'C'},
	{"reload_aec_config",   no_argument,            0, 'D'},
	{"effects",             required_argument,      0, 'E'},
	{"get_aec_supported",   no_argument,            0, 'F'},
	{"aecdump",             required_argument,      0, 'G'},
	{"dump_bt",             no_argument,            0, 'H'},
	{"set_wbs_enabled",     required_argument,      0, 'I'},
	{"follow_atlog",        no_argument,            0, 'J'},
	{"connection_type",     required_argument,      0, 'K'},
	{"loopback_file",       required_argument,      0, 'L'},
	{"mute_loop_test",      required_argument,      0, 'M'},
	{"dump_main",           no_argument,            0, 'N'},
	{"set_aec_ref",         required_argument,      0, 'O'},
	{"playback_file",       required_argument,      0, 'P'},
	{"show_ooo_timestamp",  no_argument,            0, 'Q'},
	{"stream_type",         required_argument,      0, 'T'},
	{"print_nodes_inlined", no_argument,            0, 'U'},
	{"request_floop_mask",  required_argument,      0, 'V'},
	{"thread_priority",     required_argument,      0, 'W'},
	{"client_type",         required_argument,      0, 'X'},
	{"dump_dsp_offload",    no_argument,            0, 'Y'},
	{0, 0, 0, 0}
};
// clang-format on

static void show_usage() {
  int i;

  printf(
      "--add_active_input <N>:<M> - "
      "Add the ionode with the given id to active input device "
      "list\n");
  printf(
      "--add_active_output <N>:<M> - "
      "Add the ionode with the given id to active output device "
      "list\n");
  printf(
      "--add_test_dev <type> - "
      "Add a test iodev.\n");
  printf(
      "--print_nodes_inlined - "
      "Print nodes table with devices inlined\n");
  printf(
      "--block_size <N> - "
      "The number for frames per callback(dictates latency).\n");
  printf(
      "--capture_file <name> - "
      "Name of file to record to.\n");
  printf(
      "--capture_gain <dB> - "
      "Set system capture gain in dB*100 (100 = 1dB).\n");
  printf(
      "--capture_mute <0|1> - "
      "Set capture mute state.\n");
  printf(
      "--channel_layout <layout_str> - "
      "Set multiple channel layout.\n");
  printf(
      "--check_output_plugged <output name> - "
      "Check if the output is plugged in\n");
  printf(
      "--connection_type <connection_type> - "
      "Set cras_client connection_type (default to 0).\n"
      "                                      "
      "Argument: 0 - For control client.\n"
      "                                      "
      "          1 - For playback client.\n"
      "                                      "
      "          2 - For capture client.\n"
      "                                      "
      "          3 - For legacy client in vms.\n"
      "                                      "
      "          4 - For unified client in vms.\n");
  printf(
      "--dump_audio_thread - "
      "Dumps audio thread info.\n");
  printf(
      "--dump_bt - "
      "Dumps debug info for bt audio\n");
  printf(
      "--dump_main - "
      "Dumps debug info from main thread\n");
  printf(
      "--dump_dsp - "
      "Print status of dsp to syslog.\n");
  printf(
      "--dump_server_info - "
      "Print status of the server.\n");
  printf(
      "--dump_dsp_offload - "
      "Print status of DSP offload for supported devices.\n");
  printf(
      "--duration_seconds <N> - "
      "Seconds to record or playback.\n");
  printf(
      "--effects <aec|ns|agc|vad|0xhh> - "
      "Set specific effect(s) on stream parameters by names or hex.\n"
      "                                "
      "Argument: <aec|ns|agc|vad> - Use comma(,) as delimiter for "
      "multiple effects, e.g. \"aec,agc\"\n"
      "                                "
      "          0xhh - Set hex value directly, e.g. 0x11. Available "
      "effect bistmasks:\n"
      "                                "
      "                 0x01=AEC, 0x02=NS, 0x04=AGC, 0x08=VAD,\n"
      "                                "
      "                 0x10=AEC on DSP allowed,\n"
      "                                "
      "                 0x20=NS on DSP allowed,\n"
      "                                "
      "                 0x40=AGC on DSP allowed\n");
  printf(
      "--follow_atlog - "
      "Continuously dumps audio thread event log.\n");
  printf(
      "--format <name> - "
      "The sample format. Either ");
  for (i = 0; supported_formats[i].name; ++i) {
    printf("%s ", supported_formats[i].name);
  }
  printf("(default to S16_LE).\n");
  printf(
      "--get_hotword_models <N>:<M> - "
      "Get the supported hotword models of node\n");
  printf(
      "--help - "
      "Print this message.\n");
  printf(
      "--listen_for_hotword <name> - "
      "Listen and capture hotword stream if supported\n");
  printf(
      "--loopback_file <name> - "
      "Name of file to record from loopback device.\n");
  printf(
      "--mute <0|1> - "
      "Set system mute state.\n");
  printf(
      "--mute_loop_test <0|1> - "
      "Continuously loop mute/un-mute.\n"
      "                         "
      "Argument: 0 - stop on error.\n"
      "                         "
      "          1 - automatically reconnect to CRAS.\n");
  printf(
      "--num_channels <N> - "
      "Two for stereo.\n");
  printf(
      "--pin_device <N> - "
      "Playback/Capture only on the given device.\n");
  printf(
      "--playback_file <name> - "
      "Name of file to play, "
      "\"-\" to playback raw audio from stdin.\n");
  printf(
      "--play_short_sound <N> - "
      "Plays the content in the file for N periods when ' "
      "is pressed.\n");
  printf(
      "--plug <N>:<M>:<0|1> - "
      "Set the plug state (0 or 1) for the ionode with the given "
      "index M on the device with index N\n");
  printf(
      "--rate <N> - "
      "Specifies the sample rate in Hz.\n");
  printf(
      "--reload_dsp - "
      "Reload dsp configuration from the ini file\n");
  printf(
      "--request_floop_mask <mask> -\n"
      "  Requests a flexible loopback device with the given mask.\n"
      "  Prints the device ID; prints negative errno on error\n");
  printf(
      "--rm_active_input <N>:<M> - "
      "Removes the ionode with the given id from active input device "
      "list\n");
  printf(
      "--rm_active_output <N>:<M> - "
      "Removes the ionode with the given id from active output device "
      "list\n");
  printf(
      "--select_input <N>:<M> - "
      "Select the ionode with the given id as preferred input\n");
  printf(
      "--select_output <N>:<M> - "
      "Select the ionode with the given id as preferred output\n");
  printf(
      "--set_hotword_model <N>:<M>:<model> - "
      "Set the model to node\n");
  printf(
      "--playback_delay_us <N> - "
      "Set the time in us to delay a reply for playback when i is "
      "pressed\n");
  printf(
      "--post_dsp <0|1|2> - "
      "Use this flag with --loopback_file. The default value is 0.\n"
      "                   "
      "Argument: 0 - Record from post-mix, pre-DSP loopback device.\n"
      "                   "
      "          1 - Record from post-DSP loopback device.\n"
      "                   "
      "          2 - Record from post-DSP loopback device padded with "
      "silence in the beginning to simulate delay in real HW mic.\n");
  printf(
      "--set_node_volume <N>:<M>:<0-100> - "
      "Set the volume of the ionode with the given id\n");
  printf(
      "--show_latency - "
      "Display latency while playing or recording.\n");
  printf(
      "--show_rms - "
      "Display RMS value of loopback stream.\n");
  printf(
      "--show_total_rms - "
      "Display total RMS value of loopback stream at the end.\n");
  printf(
      "--suspend <0|1> - "
      "Set audio suspend state.\n");
  printf(
      "--swap_left_right <N>:<M>:<0|1> - "
      "Swap or un-swap (1 or 0) the left and right channel for the "
      "ionode with the given index M on the device with index N\n");
  printf(
      "--stream_type <N> - "
      "Specify the type of the stream.\n");
  printf(
      "--syslog_mask <n> - "
      "Set the syslog mask to the given log level.\n");
  printf(
      "--test_hotword_file <N>:<filename> - "
      "Use filename as a hotword buffer for device N\n");
  printf(
      "--user_mute <0|1> - "
      "Set user mute state.\n");
  printf(
      "--version - "
      "Print the git commit ID that was used to build the client.\n");
  printf(
      "--volume <0-100> - "
      "Set system output volume.\n");
  printf(
      "--thread_priority <...> -"
      "Set cras_test_client's thread priority.\n"
      "  * If this flag is not specified, it keeps the default behavior of\n"
      "    setting rt priority, and fallbacks to niceness value.\n"
      "  * --thread_priority=none\n"
      "    audio thread does not set any priority.\n"
      "  * --thread_priority=rt:N\n"
      "    audio thread sets the rt priority to the integer value N.\n"
      "    The policy is set to SCHED_RR.\n"
      "  * --thread_priority=nice:N\n"
      "    audio thread sets the nice value to the integer value N.\n");
  printf(
      "--client_type <int> - "
      "Override the client type.\n");
  printf(
      "--show_ooo_timestamp - "
      "Display out of order timestamps while playing or recording.\n");
}

static int cras_client_create_and_connect(struct cras_client** client,
                                          enum CRAS_CONNECTION_TYPE conn_type) {
  int rc;

  rc = cras_client_create_with_type(client, conn_type);
  if (rc < 0) {
    fprintf(stderr, "Couldn't create client.\n");
    return rc;
  }

  rc = override_client_type(*client, client_type);
  if (rc) {
    cras_client_destroy(*client);
    return rc;
  }

  rc = cras_client_connect_timeout(*client, 1000);
  if (rc) {
    fprintf(stderr, "Couldn't connect to server.\n");
    cras_client_destroy(*client);
    return rc;
  }

  return 0;
}

int main(int argc, char** argv) {
  struct cras_client* client;
  int c, option_index, auto_reconnect, mask, bt_wbs_enabled, log_level, mute,
      volume;
  size_t block_size = NOT_ASSIGNED;
  size_t rate = 48000;
  size_t num_channels = 2;
  float duration_seconds = 0;
  const char* capture_file = NULL;
  const char* playback_file = NULL;
  const char* loopback_file = NULL;
  int post_dsp = 0;
  enum CRAS_STREAM_TYPE stream_type = CRAS_STREAM_TYPE_DEFAULT;
  int rc = 0;
  uint32_t stream_flags = 0;
  cras_stream_id_t stream_id = 0;
  snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
  enum CRAS_CONNECTION_TYPE conn_type = CRAS_CONTROL;
  enum CRAS_CONNECTION_TYPE new_conn_type;
  unsigned long ul = 0;

  option_index = 0;
  openlog("cras_test_client", LOG_PERROR, LOG_USER);
  setlogmask(LOG_UPTO(LOG_INFO));

  rc = cras_client_create_and_connect(&client, conn_type);
  if (rc) {
    return rc;
  }

  if (argc == 1) {
    // Nothing specified, default to dump_server_info.
    print_server_info(client);
    goto destroy_exit;
  }

  while (1) {
    c = getopt_long(argc, argv, "o:s:P:C:r:c:f:h", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'y':
      case 'a': {
        cras_node_id_t id;
        rc = parse_node_id(optarg, &id);
        if (rc) {
          show_usage();
          return rc;
        }

        enum CRAS_STREAM_DIRECTION direction =
            (c == 'y') ? CRAS_STREAM_OUTPUT : CRAS_STREAM_INPUT;
        cras_client_select_node(client, direction, id);
        break;
      }
      case 'b':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid block size %s\n", optarg);
          goto destroy_exit;
        }
        block_size = ul;
        break;
      case 'c':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid channel num %s\n", optarg);
          goto destroy_exit;
        }
        num_channels = ul;
        break;
      case 'd':
        rc = parse_float(optarg, &duration_seconds);
        if (rc < 0) {
          printf("Invalid duration: %s\n", optarg);
          return rc;
        }
        break;
      case 'e':
        show_audio_thread_snapshots(client);
        break;
      case 'f': {
        int i;

        for (i = 0; supported_formats[i].name; ++i) {
          if (strcasecmp(optarg, supported_formats[i].name) == 0) {
            format = supported_formats[i].format;
            break;
          }
        }

        if (!supported_formats[i].name) {
          printf("Unsupported format: %s\n", optarg);
          return -EINVAL;
        }
        break;
      }
      case 'h':
        show_usage();
        break;
      case 'i':
        print_server_info(client);
        break;
      case 'j':
        check_output_plugged(client, optarg);
        break;
      case 'k':
      case 't':
      case '1':
      case '2': {
        cras_node_id_t id;
        rc = parse_node_id(optarg, &id);
        if (rc) {
          show_usage();
          return rc;
        }

        enum CRAS_STREAM_DIRECTION dir;
        if (c == 't' || c == '2') {
          dir = CRAS_STREAM_OUTPUT;
        } else {
          dir = CRAS_STREAM_INPUT;
        }

        if (c == 'k' || c == 't') {
          cras_client_add_active_node(client, dir, id);
        } else {
          cras_client_rm_active_node(client, dir, id);
        }
        break;
      }
      case 'l':
        cras_client_dump_dsp_info(client);
        break;
      case 'm':
        show_audio_debug_info(client);
        break;
      case 'n': {
        rc = parse_int(optarg, &log_level);
        if (rc < 0) {
          fprintf(stderr, "invalid log level %s\n", optarg);
          goto destroy_exit;
        }
        setlogmask(LOG_UPTO(log_level));
        break;
      }
      case 'o':
        channel_layout = optarg;
        break;
      case 'p':
        printf("AEC group ID %d\n", cras_client_get_aec_group_id(client));
        break;
      case 'q': {
        rc = parse_int(optarg, &mute);
        if (rc < 0) {
          fprintf(stderr, "invalid mute value %s\n", optarg);
          goto destroy_exit;
        }
        rc = cras_client_set_user_mute(client, mute);
        if (rc < 0) {
          fprintf(stderr, "problem setting mute\n");
          goto destroy_exit;
        }
        break;
      }
      case 'r':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid rate %s\n", optarg);
          goto destroy_exit;
        }
        rate = ul;
        break;
      case 's':
        cras_client_reload_dsp(client);
        break;
      case 'u': {
        rc = parse_int(optarg, &mute);
        if (rc < 0) {
          fprintf(stderr, "invalid mute value %s\n", optarg);
          goto destroy_exit;
        }
        rc = cras_client_set_system_mute(client, mute);
        if (rc < 0) {
          fprintf(stderr, "problem setting mute\n");
          goto destroy_exit;
        }
        break;
      }
      case 'v': {
        rc = parse_int(optarg, &volume);
        if (rc < 0) {
          fprintf(stderr, "invalid volume %s\n", optarg);
          goto destroy_exit;
        }
        volume = MIN(100, MAX(0, volume));
        rc = cras_client_set_system_volume(client, volume);
        if (rc < 0) {
          fprintf(stderr, "problem setting volume\n");
          goto destroy_exit;
        }
        break;
      }
      case ':':
      case 'w': {
        cras_node_id_t id;
        int value;
        rc = parse_node_id_with_value(optarg, &id, &value);
        if (rc) {
          show_usage();
          return rc;
        }

        if (c == 'w') {
          cras_client_set_node_volume(client, id, value);
        } else {
          cras_client_set_node_capture_gain(client, id, value);
        }
        break;
      }
      case 'x': {
        cras_node_id_t id;
        int value;
        rc = parse_node_id_with_value(optarg, &id, &value);
        if (rc) {
          show_usage();
          return rc;
        }

        enum ionode_attr attr = IONODE_ATTR_PLUGGED;
        cras_client_set_node_attr(client, id, attr, value);
        break;
      }
      case 'z':
        rc = parse_int(optarg, &pause_in_playback_reply);
        if (rc < 0) {
          fprintf(stderr, "invalid pause_in_playback_reply value %s\n", optarg);
          goto destroy_exit;
        }
        break;

      case '0': {
        rc = parse_int(optarg, &mute);
        if (rc < 0) {
          fprintf(stderr, "invalid mute value %s\n", optarg);
          goto destroy_exit;
        }
        rc = cras_client_set_system_capture_mute(client, mute);
        if (rc < 0) {
          fprintf(stderr, "problem setting mute\n");
          goto destroy_exit;
        }
        break;
      }
      case '3': {
        cras_node_id_t id;
        int value;
        rc = parse_node_id_with_value(optarg, &id, &value);
        if (rc) {
          show_usage();
          return rc;
        }

        cras_client_swap_node_left_right(client, id, value);
        break;
      }
      case '4':
        printf("%s\n", VCSID);
        break;
      case '5': {
        unsigned long type;
        rc = parse_unsigned_long(optarg, &type);
        if (errno == ERANGE) {
          fprintf(stderr, "invalid iodev type %s\n", optarg);
          goto destroy_exit;
        }
        cras_client_add_test_iodev(client, type);
        break;
      }
      case '7': {
        stream_flags = HOTWORD_STREAM;
        capture_file = optarg;
        break;
      }
      case '8':
        rc = parse_int(optarg, &pin_device_id);
        if (rc < 0) {
          fprintf(stderr, "invalid device_id %s\n", optarg);
          goto destroy_exit;
        }
        break;
      case '9': {
        int suspend;
        rc = parse_int(optarg, &suspend);
        if (rc < 0) {
          fprintf(stderr, "invalid suspend value %s\n", optarg);
          goto destroy_exit;
        }
        cras_client_set_suspend(client, suspend);
        break;
      }

      case '!': {
        play_short_sound = 1;
        rc = parse_int(optarg, &play_short_sound_periods);
        if (rc < 0) {
          fprintf(stderr, "invalid period count %s\n", optarg);
          goto destroy_exit;
        }
        break;
      }
      case '<':
      case '>': {
        char* s;
        int dev_index;
        int node_index;

        s = strtok(optarg, ":");
        if (!s) {
          show_usage();
          return -EINVAL;
        }
        rc = parse_int(s, &dev_index);
        if (rc < 0) {
          fprintf(stderr, "invalid dev index %s\n", optarg);
          goto destroy_exit;
        }

        s = strtok(NULL, ":");
        if (!s) {
          show_usage();
          return -EINVAL;
        }
        rc = parse_int(s, &node_index);
        if (rc < 0) {
          fprintf(stderr, "invalid node index %s\n", optarg);
          goto destroy_exit;
        }

        s = strtok(NULL, ":");
        if (!s && c == ';') {
          // TODO: c never == ';'
          show_usage();
          return -EINVAL;
        }

        cras_node_id_t id = cras_make_node_id(dev_index, node_index);
        if (c == '<') {
          cras_client_set_hotword_model(client, id, s);
        } else {
          print_hotword_models(client, id);
        }
        break;
      }

      case 'A':
        rc = parse_int(optarg, &rc);
        if (rc < 0) {
          fprintf(stderr, "invalid post_dsp value %s\n", optarg);
          goto destroy_exit;
        }
        break;
      case 'B':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid stream_id %s\n", optarg);
          goto destroy_exit;
        }
        stream_id = ul;
        break;
      case 'C':
        capture_file = optarg;
        break;
      case 'D':
        cras_client_reload_aec_config(client);
        break;
      case 'E':
        parse_stream_effects(optarg);
        break;
      case 'F':
        printf("AEC supported %d\n", !!cras_client_get_aec_supported(client));
        break;
      case 'G':
        aecdump_file = optarg;
        break;
      case 'H':
        show_cras_bt_debug_info(client);
        break;
      case 'I':
        rc = parse_int(optarg, &bt_wbs_enabled);
        if (rc < 0) {
          fprintf(stderr, "invalid bt_wbs_enabled value %s\n", optarg);
          goto destroy_exit;
        }
        cras_client_set_bt_wbs_enabled(client, bt_wbs_enabled);
        break;
      case 'J':
        cras_show_continuous_atlog(client);
        break;
      case 'K':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid connection type %s\n", optarg);
          goto destroy_exit;
        }
        new_conn_type = ul;
        if (cras_validate_connection_type(new_conn_type)) {
          if (new_conn_type != conn_type) {
            cras_client_destroy(client);
            client = NULL;
            rc = cras_client_create_and_connect(&client, new_conn_type);
            if (rc) {
              fprintf(stderr,
                      "Couldn't connect to "
                      "server.\n");
              return rc;
            }
            conn_type = new_conn_type;
          }
        } else {
          printf(
              "Input connection type is not "
              "supported.\n");
        }
        break;
      case 'L':
        loopback_file = optarg;
        break;
      case 'M':
        rc = parse_int(optarg, &auto_reconnect);
        if (rc < 0) {
          fprintf(stderr, "invalid auto reconnect value %s\n", optarg);
          goto destroy_exit;
        }
        mute_loop_test(client, auto_reconnect);
        break;
      case 'N':
        show_main_thread_debug_info(client);
        break;
      case 'O':
        rc = parse_int(optarg, &aec_ref_device_id);
        if (rc < 0) {
          fprintf(stderr, "invalid device id %s\n", optarg);
          goto destroy_exit;
        }
        break;
      case 'P':
        playback_file = optarg;
        break;
      case 'Q':
        show_ooo_ts = 1;
        break;
      case 'T':
        rc = parse_unsigned_long(optarg, &ul);
        if (rc < 0) {
          fprintf(stderr, "invalid stream type %s\n", optarg);
          goto destroy_exit;
        }
        stream_type = ul;
        break;
      case 'U':
        print_nodes_inlined(client);
        break;
      case 'V':
        rc = parse_int(optarg, &mask);
        if (rc < 0) {
          fprintf(stderr, "invalid mask %s\n", optarg);
          goto destroy_exit;
        }
        request_floop_mask(client, mask);

        break;
      case 'W': {
        cras_client_set_thread_priority_cb(client, thread_priority_cb);
        if (0 == strcmp(optarg, "none")) {
          thread_priority = THREAD_PRIORITY_NONE;
        } else if (str_has_prefix(optarg, "nice:")) {
          thread_priority = THREAD_PRIORITY_NICE;
          rc = parse_int(optarg + strlen("nice:"), &niceness_level);
          if (rc < 0) {
            fprintf(stderr, "invalid niceness_levels %s\n", optarg);
            goto destroy_exit;
          }
        } else if (str_has_prefix(optarg, "rt:")) {
          thread_priority = THREAD_PRIORITY_RT_RR;
          rc = parse_int(optarg + strlen("rt:"), &rt_priority);
          if (rc < 0) {
            fprintf(stderr, "invalid rt_priority %s\n", optarg);
            goto destroy_exit;
          }
        } else {
          fprintf(stderr, "invalid --thread_priority argument: %s\n", optarg);
          rc = 1;
          goto destroy_exit;
        }
        break;
      }
      case 'X':
        rc = parse_client_type(optarg, &client_type);
        if (rc) {
          goto destroy_exit;
        }
        rc = override_client_type(client, client_type);
        if (rc) {
          goto destroy_exit;
        }
        break;
      case 'Y':
        print_dsp_offload_infos(client);
        break;
      default:
        break;
    }
  }

  if (optind < argc) {
    printf("Warning: un-welcome arguments: ");
    while (optind < argc) {
      printf("%s ", argv[optind++]);
    }
    printf("\n");
    rc = 1;
    goto destroy_exit;
  }

  duration_frames = duration_seconds * rate;
  if (block_size == NOT_ASSIGNED) {
    block_size = get_block_size(PLAYBACK_BUFFERED_TIME_IN_US, rate);
  }

  if (capture_file != NULL) {
    if (strcmp(capture_file, "-") == 0) {
      rc = run_file_io_stream(client, 1, CRAS_STREAM_INPUT, block_size,
                              stream_type, rate, format, num_channels,
                              stream_flags, 0, 0);
    } else {
      rc = run_capture(client, capture_file, block_size, stream_type, rate,
                       format, num_channels, stream_flags, 0, 0);
    }
  } else if (playback_file != NULL) {
    if (strcmp(playback_file, "-") == 0) {
      rc = run_file_io_stream(client, 0, CRAS_STREAM_OUTPUT, block_size,
                              stream_type, rate, format, num_channels,
                              stream_flags, 0, 0);
    } else {
      rc = run_playback(client, playback_file, block_size, stream_type, rate,
                        format, num_channels);
    }
  } else if (loopback_file != NULL) {
    rc = run_capture(client, loopback_file, block_size, stream_type, rate,
                     format, num_channels, stream_flags, 1, post_dsp);
  } else if (aecdump_file != NULL) {
    run_aecdump(client, stream_id, 1);
    sleep(duration_seconds);
    run_aecdump(client, stream_id, 0);
  }

destroy_exit:
  cras_client_destroy(client);
  return rc;
}
