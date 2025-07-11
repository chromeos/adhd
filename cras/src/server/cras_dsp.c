/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_dsp.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras/server/main_message.h"
#include "cras/server/s2/s2.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/common/dumper.h"
#include "cras/src/dsp/dsp_util.h"
#include "cras/src/server/cras_dsp_ini.h"
#include "cras/src/server/cras_dsp_pipeline.h"
#include "cras/src/server/cras_expr.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras_audio_format.h"
#include "cras_iodev_info.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

/* We have a dsp_context for each pipeline. The context records the
 * parameters used to create a pipeline, so the pipeline can be
 * (re-)loaded later. The pipeline is (re-)loaded in the following
 * cases:
 *
 * (1) The client asks to (re-)load it with cras_load_pipeline().
 * (2) The client asks to reload the ini with cras_reload_ini().
 *
 * The pipeline is (re-)loaded asynchronously in an internal thread,
 * so the client needs to use cras_dsp_get_pipeline() and
 * cras_dsp_put_pipeline() to safely access the pipeline.
 */
struct cras_dsp_context {
  pthread_mutex_t mutex;
  struct pipeline* pipeline;

  struct cras_expr_env env;
  int sample_rate;
  const char* purpose;
  struct dsp_offload_map* offload_map;
  struct cras_dsp_context *prev, *next;
};

static struct dumper* syslog_dumper;
static const char* ini_filename;
static struct ini* global_ini;
static struct cras_dsp_context* context_list;

static void initialize_environment(struct cras_expr_env* env) {
  cras_expr_env_install_builtins(env);
  cras_expr_env_set_variable_boolean(env, "disable_eq", 0);
  cras_expr_env_set_variable_boolean(env, "disable_drc", 0);
  cras_expr_env_set_variable_string(env, "dsp_name", "");
  cras_expr_env_set_variable_boolean(env, "swap_lr_disabled", 1);
  cras_expr_env_set_variable_integer(env, "display_rotation", ROTATE_0);
  cras_expr_env_set_variable_integer(env, "FL", CRAS_CH_FL);
  cras_expr_env_set_variable_integer(env, "FR", CRAS_CH_FR);
  cras_expr_env_set_variable_integer(env, "RL", CRAS_CH_RL);
  cras_expr_env_set_variable_integer(env, "RR", CRAS_CH_RR);
}

static void destroy_pipeline(struct pipeline* pipeline) {
  struct ini* private_ini;

  private_ini = cras_dsp_pipeline_get_ini(pipeline);
  cras_dsp_pipeline_free(pipeline);

  /*
   * If pipeline is using an dsp ini other than the global one, free
   * this ini so its life cycle is aligned with the associated dsp
   * pipeline.
   */
  if (private_ini && (private_ini != global_ini)) {
    cras_dsp_ini_free(private_ini);
  }
}

static struct pipeline* prepare_pipeline(struct cras_dsp_context* ctx,
                                         struct ini* target_ini) {
  struct pipeline* pipeline;
  const char* purpose = ctx->purpose;
  int ret;

  pipeline = cras_dsp_pipeline_create(target_ini, &ctx->env, purpose);

  if (pipeline) {
    syslog(LOG_DEBUG, "pipeline created");
  } else {
    syslog(LOG_DEBUG, "pipeline not created");
    goto bail;
  }

  ret = cras_dsp_pipeline_load(pipeline);
  if (ret < 0) {
    syslog(LOG_ERR, "cannot load pipeline: %d", ret);
    goto bail;
  }

  ret = cras_dsp_pipeline_instantiate(pipeline, ctx->sample_rate, &ctx->env);
  if (ret < 0) {
    syslog(LOG_ERR, "cannot instantiate pipeline: %d", ret);
    goto bail;
  }

  if (cras_dsp_pipeline_get_sample_rate(pipeline) != ctx->sample_rate) {
    syslog(LOG_ERR, "pipeline sample rate mismatch (%d vs %d)",
           cras_dsp_pipeline_get_sample_rate(pipeline), ctx->sample_rate);
    goto bail;
  }

  return pipeline;

bail:
  if (pipeline) {
    destroy_pipeline(pipeline);
  }
  return NULL;
}

/* The strategy is to offload the given CRAS pipeline to DSP if applicable. If
 * that is the case, the following steps will be done to offload post-processing
 * effects from CRAS to DSP firmware:
 *   1. Enable the associated components on DSP and set the config blob to them
 *      each correspondent to CRAS pipeline modules.
 *   2. Set offload_applied flag true in CRAS pipeline, which makes the pipeline
 *      run through audio streams while bypassing post-processing modules.
 *
 * On the other hand if the pipeline is not applicable, disabling the associated
 * components on DSP is needed to assure no post-processing effect is on DSP.
 */
static void possibly_offload_pipeline(struct dsp_offload_map* offload_map,
                                      struct pipeline* pipe) {
  bool fallback = false;
  int rc;

  if (!offload_map) {
    // The DSP offload doesn't support for the device running this pipeline.
    return;
  }

  if (!offload_map->parent_dev) {
    syslog(LOG_ERR, "cras_dsp: invalid parent_dev in offload_map");
    return;
  }

  // Disable offload when disallow_bits is non-zero (at least one condition is
  // met that disallows applying DSP offload).
  if (offload_map->disallow_bits) {
    syslog(LOG_DEBUG, "cras_dsp: disallow offload (disallow_bits=%d)",
           offload_map->disallow_bits);
    goto disable_offload;
  }

  // If supports, check if the DSP offload is applicable, i.e. the pattern for
  // the CRAS pipeline is matched with the offload map. The pipeline can be NULL
  // when the current active node has no DSP config specified, which will be
  // regarded as not applicable.
  bool is_applicable = false;
  if (pipe) {
    char* pattern = cras_dsp_pipeline_get_pattern(pipe);
    syslog(LOG_DEBUG, "cras_dsp: trying to offload pipeline (%s)...", pattern);
    is_applicable = str_equals_bounded(offload_map->dsp_pattern, pattern,
                                       DSP_PATTERN_MAX_SIZE);
    free(pattern);
  }
  syslog(LOG_DEBUG, "cras_dsp: offload is %sapplicable",
         is_applicable ? "" : "non-");

  // If not applicable, disable offload.
  if (!is_applicable) {
    cras_dsp_offload_set_disallow_bit(offload_map, DISALLOW_OFFLOAD_BY_PATTERN);
    goto disable_offload;
  }

  // is_applicable == true
  cras_dsp_offload_clear_disallow_bit(offload_map, DISALLOW_OFFLOAD_BY_PATTERN);
  // If the DSP offload is already applied for the same pipeline/node, there
  // is no longer needed for setting configs to components on DSP.
  if (cras_dsp_offload_is_already_applied(offload_map)) {
    syslog(LOG_DEBUG, "cras_dsp: offload is already applied");
    cras_dsp_pipeline_apply_offload(pipe, true);
    return;
  }

  rc = cras_dsp_pipeline_config_offload(offload_map, pipe);
  if (rc) {
    syslog(LOG_ERR, "cras_dsp: Failed to config offload blobs, rc: %d", rc);
    MAINLOG(main_log, MAIN_THREAD_DEV_DSP_OFFLOAD,
            offload_map->parent_dev->info.idx, 1 /* enable */, 1 /* error */);
    fallback = true;
    goto disable_offload;  // fallback to process on CRAS
  }

  rc = cras_dsp_offload_set_state(offload_map, true);
  if (rc) {
    syslog(LOG_ERR, "cras_dsp: Failed to enable offload, rc: %d", rc);
    MAINLOG(main_log, MAIN_THREAD_DEV_DSP_OFFLOAD,
            offload_map->parent_dev->info.idx, 1 /* enable */, 1 /* error */);
    fallback = true;
    goto disable_offload;  // fallback to process on CRAS
  }

  syslog(LOG_DEBUG, "cras_dsp: offload is applied on success.");
  MAINLOG(main_log, MAIN_THREAD_DEV_DSP_OFFLOAD,
          offload_map->parent_dev->info.idx, 1 /* enable */, 0 /* ok */);
  cras_server_metrics_device_dsp_offload_status(
      offload_map->parent_dev, CRAS_DEVICE_DSP_OFFLOAD_SUCCESS);

  // Set offload_applied flag true
  cras_dsp_pipeline_apply_offload(pipe, true);
  return;

disable_offload:
  // Take actions to disable components on DSP if not applicable.
  rc = cras_dsp_offload_set_state(offload_map, false);
  if (rc) {
    // TODO(b/188647460): Consider better error handlings e.g. N-time retries,
    //                    report up to CRAS server, and etc.
    syslog(LOG_ERR, "cras_dsp: Failed to disable offload, rc: %d", rc);
    MAINLOG(main_log, MAIN_THREAD_DEV_DSP_OFFLOAD,
            offload_map->parent_dev->info.idx, 0 /* disable */, 1 /* error */);
    if (fallback) {
      cras_server_metrics_device_dsp_offload_status(
          offload_map->parent_dev, CRAS_DEVICE_DSP_OFFLOAD_FALLBACK_ERROR);
    } else {
      cras_server_metrics_device_dsp_offload_status(
          offload_map->parent_dev, CRAS_DEVICE_DSP_OFFLOAD_ERROR);
    }
  } else {
    MAINLOG(main_log, MAIN_THREAD_DEV_DSP_OFFLOAD,
            offload_map->parent_dev->info.idx, 0 /* disable */, 0 /* ok */);
    if (fallback) {
      cras_server_metrics_device_dsp_offload_status(
          offload_map->parent_dev, CRAS_DEVICE_DSP_OFFLOAD_FALLBACK_SUCCESS);
    }
  }

  // Set offload_applied flag false
  cras_dsp_pipeline_apply_offload(pipe, false);
}

static void cmd_load_pipeline(struct cras_dsp_context* ctx,
                              struct ini* target_ini) {
  struct pipeline *pipeline, *old_pipeline;

  pipeline = target_ini ? prepare_pipeline(ctx, target_ini) : NULL;

  possibly_offload_pipeline(ctx->offload_map, pipeline);

  // This locking is short to avoild blocking audio thread.
  pthread_mutex_lock(&ctx->mutex);
  old_pipeline = ctx->pipeline;
  ctx->pipeline = pipeline;
  pthread_mutex_unlock(&ctx->mutex);

  if (old_pipeline) {
    destroy_pipeline(old_pipeline);
  }
}

static void cmd_reload_ini() {
  struct ini* old_ini = global_ini;
  struct cras_dsp_context* ctx;

  struct ini* new_ini = cras_dsp_ini_create(ini_filename);
  if (!new_ini) {
    syslog(LOG_DEBUG, "cannot create dsp ini");
    return;
  }

  DL_FOREACH (context_list, ctx) {
    // Reset the offload state to force the offload blob re-configuring.
    cras_dsp_offload_reset_map(ctx->offload_map);
    cmd_load_pipeline(ctx, new_ini);
  }

  global_ini = new_ini;

  if (old_ini) {
    cras_dsp_ini_free(old_ini);
  }
}

void notify_reload_cras_dsp() {
  struct cras_main_message msg = {
      .length = sizeof(msg),
      .type = CRAS_MAIN_RELOAD_DSP,
  };
  cras_main_message_send(&msg);
}

// Exported functions

void cras_dsp_init(const char* filename) {
  dsp_enable_flush_denormal_to_zero();
  ini_filename = strdup(filename);
  syslog_dumper = syslog_dumper_create(LOG_WARNING);
  cras_main_message_add_handler(CRAS_MAIN_RELOAD_DSP, cmd_reload_ini, NULL);
  cras_s2_set_reload_output_plugin_processor(notify_reload_cras_dsp);
  cmd_reload_ini();
}

void cras_dsp_stop() {
  syslog_dumper_free(syslog_dumper);
  if (ini_filename) {
    free((char*)ini_filename);
  }
  if (global_ini) {
    cras_dsp_ini_free(global_ini);
    global_ini = NULL;
  }
}

struct cras_dsp_context* cras_dsp_context_new(int sample_rate,
                                              const char* purpose) {
  struct cras_dsp_context* ctx = calloc(1, sizeof(*ctx));

  pthread_mutex_init(&ctx->mutex, NULL);
  initialize_environment(&ctx->env);
  ctx->sample_rate = sample_rate;
  ctx->purpose = strdup(purpose);

  DL_APPEND(context_list, ctx);
  return ctx;
}

void cras_dsp_context_set_offload_map(struct cras_dsp_context* ctx,
                                      struct dsp_offload_map* offload_map) {
  if (ctx) {
    ctx->offload_map = offload_map;
  }
}

void cras_dsp_context_free(struct cras_dsp_context* ctx) {
  DL_DELETE(context_list, ctx);

  pthread_mutex_destroy(&ctx->mutex);
  if (ctx->pipeline) {
    destroy_pipeline(ctx->pipeline);
    ctx->pipeline = NULL;
  }
  cras_expr_env_free(&ctx->env);
  free((char*)ctx->purpose);
  free(ctx);
}

void cras_dsp_set_variable_string(struct cras_dsp_context* ctx,
                                  const char* key,
                                  const char* value) {
  cras_expr_env_set_variable_string(&ctx->env, key, value);
}

void cras_dsp_set_variable_boolean(struct cras_dsp_context* ctx,
                                   const char* key,
                                   char value) {
  cras_expr_env_set_variable_boolean(&ctx->env, key, value);
}

void cras_dsp_set_variable_integer(struct cras_dsp_context* ctx,
                                   const char* key,
                                   int value) {
  cras_expr_env_set_variable_integer(&ctx->env, key, value);
}

void cras_dsp_load_pipeline(struct cras_dsp_context* ctx) {
  cmd_load_pipeline(ctx, global_ini);
}

void cras_dsp_load_mock_pipeline(struct cras_dsp_context* ctx,
                                 unsigned int num_channels) {
  struct ini* mock_ini;
  mock_ini = create_mock_ini(ctx->purpose, num_channels);
  if (mock_ini == NULL) {
    syslog(LOG_ERR, "Failed to create mock ini");
  } else {
    cmd_load_pipeline(ctx, mock_ini);
  }
}

struct pipeline* cras_dsp_get_pipeline(struct cras_dsp_context* ctx) {
  pthread_mutex_lock(&ctx->mutex);
  if (!ctx->pipeline) {
    pthread_mutex_unlock(&ctx->mutex);
    return NULL;
  }
  return ctx->pipeline;
}

void cras_dsp_put_pipeline(struct cras_dsp_context* ctx) {
  pthread_mutex_unlock(&ctx->mutex);
}

void cras_dsp_reload_ini() {
  cmd_reload_ini();
}

void cras_dsp_readapt_pipeline(struct cras_dsp_context* ctx) {
  struct pipeline* pipeline = cras_dsp_get_pipeline(ctx);
  if (!pipeline) {
    syslog(LOG_WARNING, "Bad attempt to readapt pipeline while not loaded.");
    return;
  }
  /* dsp_context mutex locked. Now it's safe to modify dsp
   * pipeline resources. */

  possibly_offload_pipeline(ctx->offload_map, pipeline);
  cras_dsp_put_pipeline(ctx);
}

void cras_dsp_dump_info() {
  struct pipeline* pipeline;
  struct cras_dsp_context* ctx;

  if (global_ini) {
    cras_dsp_ini_dump(syslog_dumper, global_ini);
  }
  DL_FOREACH (context_list, ctx) {
    cras_expr_env_dump(syslog_dumper, &ctx->env);
    pipeline = ctx->pipeline;
    if (pipeline) {
      cras_dsp_pipeline_dump(syslog_dumper, pipeline);
    }
  }
}

CRAS_STREAM_ACTIVE_AP_EFFECT cras_dsp_get_active_ap_effects(
    struct cras_dsp_context* ctx) {
  return ctx ? cras_dsp_pipeline_get_active_ap_effects(ctx->pipeline) : 0;
}

unsigned int cras_dsp_num_output_channels(const struct cras_dsp_context* ctx) {
  return cras_dsp_pipeline_get_num_output_channels(ctx->pipeline);
}

unsigned int cras_dsp_num_input_channels(const struct cras_dsp_context* ctx) {
  return cras_dsp_pipeline_get_num_input_channels(ctx->pipeline);
}
