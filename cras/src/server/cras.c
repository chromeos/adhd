/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE  // for asprintf
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <syslog.h>

#include "cras/src/server/cras_alsa_plugin_io.h"
#include "cras/src/server/cras_bt_manager.h"
#include "cras/src/server/cras_dsp.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server.h"
#include "cras/src/server/cras_speak_on_mute_detector.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_feature_tier.h"
#include "cras_config.h"
#include "cras_shm.h"

#define DEFAULT_LOG_MASK LOG_WARNING

static struct option long_options[] = {
    {"dsp_config", required_argument, 0, 'd'},
    {"syslog_mask", required_argument, 0, 'l'},
    {"device_config_dir", required_argument, 0, 'c'},
    {"disable_profile", required_argument, 0, 'D'},
    {"internal_ucm_suffix", required_argument, 0, 'u'},
    {"board_name", required_argument, 0, 'b'},
    {"cpu_model_name", required_argument, 0, 'p'},
    {0, 0, 0, 0}};

// Ignores sigpipe, we'll notice when a read/write fails.
static void set_signals() {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);
}

// Entry point for the server.
int main(int argc, char** argv) {
  int c, option_index;
  int log_mask = DEFAULT_LOG_MASK;
  const char default_dsp_config[] = CRAS_CONFIG_FILE_DIR "/dsp.ini";
  const char* dsp_config = default_dsp_config;
  const char* device_config_dir = CRAS_CONFIG_FILE_DIR;
  const char* internal_ucm_suffix = NULL;
  const char* board_name = NULL;
  const char* cpu_model_name = NULL;
  unsigned int profile_disable_mask = 0;

  set_signals();

  while (1) {
    c = getopt_long(argc, argv, "", long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      /* To keep this code simple we ask the (technical)
         user to pass one of integer values defined in
         syslog.h - this is a development feature after
         all. While there is no formal standard for the
         integer values there is an informal standard:
         http://tools.ietf.org/html/rfc5424#page-11 */
      case 'l':
        log_mask = atoi(optarg);
        break;

      case 'c':
        device_config_dir = optarg;
        break;

      case 'd':
        dsp_config = optarg;
        break;
      // --disable_profile option takes list of profile names separated by ','
      case 'D':
        while ((optarg != NULL) && (*optarg != 0)) {
          if (strncmp(optarg, "hfp", 3) == 0) {
            profile_disable_mask |= CRAS_BT_PROFILE_MASK_HFP;
          }
          if (strncmp(optarg, "a2dp", 4) == 0) {
            profile_disable_mask |= CRAS_BT_PROFILE_MASK_A2DP;
          }
          optarg = strchr(optarg, ',');
          if (optarg != NULL) {
            optarg++;
          }
        }
        break;
      case 'u':
        if (*optarg != 0) {
          internal_ucm_suffix = optarg;
        }
        break;

      case 'b':
        board_name = optarg;
        break;

      case 'p':
        cpu_model_name = optarg;
        break;

      default:
        break;
    }
  }

  switch (log_mask) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
    case LOG_WARNING:
    case LOG_NOTICE:
    case LOG_INFO:
    case LOG_DEBUG:
      break;
    default:
      fprintf(stderr, "Unsupported syslog priority value: %d; using %d\n",
              log_mask, DEFAULT_LOG_MASK);
      log_mask = DEFAULT_LOG_MASK;
      break;
  }
  setlogmask(LOG_UPTO(log_mask));

  // Initialize system.
  cras_server_init();
  char* shm_name;
  if (asprintf(&shm_name, "/cras-%d", getpid()) < 0) {
    exit(-1);
  }
  int rw_shm_fd;
  int ro_shm_fd;
  struct cras_server_state* exp_state =
      (struct cras_server_state*)cras_shm_setup(shm_name, sizeof(*exp_state),
                                                &rw_shm_fd, &ro_shm_fd);
  if (!exp_state) {
    exit(-1);
  }
  cras_system_state_init(device_config_dir, shm_name, rw_shm_fd, ro_shm_fd,
                         exp_state, sizeof(*exp_state), board_name,
                         cpu_model_name);
  free(shm_name);
  if (internal_ucm_suffix) {
    cras_system_state_set_internal_ucm_suffix(internal_ucm_suffix);
  }
  cras_dsp_init(dsp_config);
  cras_stream_apm_init(device_config_dir);
  cras_speak_on_mute_detector_init();
  cras_iodev_list_init();
  cras_alsa_plugin_io_init(device_config_dir);

  // Start the server.
  return cras_server_run(profile_disable_mask);
}
