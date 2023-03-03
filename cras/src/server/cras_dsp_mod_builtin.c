/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <syslog.h>

#include "cras/src/dsp/dcblock.h"
#include "cras/src/dsp/drc.h"
#include "cras/src/dsp/dsp_util.h"
#include "cras/src/dsp/eq.h"
#include "cras/src/dsp/eq2.h"
#include "cras/src/dsp/quad_rotation.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras_types.h"

/*
 *  empty module functions (for source and sink)
 */
static int empty_instantiate(struct dsp_module* module,
                             unsigned long sample_rate,
                             struct cras_expr_env* env) {
  return 0;
}

static void empty_connect_port(struct dsp_module* module,
                               unsigned long port,
                               float* data_location) {}

static void empty_configure(struct dsp_module* module) {}

static int empty_get_delay(struct dsp_module* module) {
  return 0;
}

static void empty_run(struct dsp_module* module, unsigned long sample_count) {}

static void empty_deinstantiate(struct dsp_module* module) {}

static void empty_free_module(struct dsp_module* module) {
  free(module);
}

static int empty_get_properties(struct dsp_module* module) {
  return 0;
}

static void empty_dump(struct dsp_module* module, struct dumper* d) {
  dumpf(d, "built-in module\n");
}

static void empty_init_module(struct dsp_module* module) {
  module->instantiate = &empty_instantiate;
  module->connect_port = &empty_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &empty_run;
  module->deinstantiate = &empty_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  quad_rotation module functions
 */

// Validates the port_map for quad_rotation.
static bool quad_rotation_valid_port_map(int* port_map) {
  int used[NUM_SPEAKER_POS_QUAD] = {};
  for (int i = 0; i < NUM_SPEAKER_POS_QUAD; i++) {
    if (port_map[i] < 0 || port_map[i] > NUM_SPEAKER_POS_QUAD ||
        used[port_map[i]] == 1) {
      return false;
    }
    used[port_map[i]] = 1;
  }
  return true;
}

static int quad_rotation_instantiate(struct dsp_module* module,
                                     unsigned long sample_rate,
                                     struct cras_expr_env* env) {
  struct quad_rotation* data;
  struct cras_expr_expression* expr;
  int rc = 0;
  const char* channel_str[] = {"FL", "RL", "RR", "FR"};

  // four port for input, four for output, and 1 parameters
  module->data = calloc(1, sizeof(struct quad_rotation));
  if (!module->data) {
    syslog(LOG_ERR, "quad_rotation calloc failed");
    rc = -ENOMEM;
    goto fail;
  }
  data = module->data;
  expr = cras_expr_expression_parse("display_rotation");
  rc = cras_expr_expression_eval_int(expr, env, (int*)&data->rotation);
  cras_expr_expression_free(expr);
  if (rc < 0) {
    syslog(LOG_ERR, "failed to eval display_rotation for quad_rotation");
    goto fail;
  }
  for (int i = 0; i < NUM_SPEAKER_POS_QUAD; i++) {
    expr = cras_expr_expression_parse(channel_str[i]);
    rc = cras_expr_expression_eval_int(expr, env, &data->port_map[i]);
    cras_expr_expression_free(expr);
    if (rc < 0) {
      syslog(LOG_ERR, "failed to eval %s for quad_rotation", channel_str[i]);
      goto fail;
    }
  }
  if (!quad_rotation_valid_port_map(data->port_map)) {
    syslog(LOG_ERR, "invalid port_map for quad_rotation");
    goto fail;
  }

  return 0;

fail:
  free(module->data);
  module->data = NULL;
  syslog(LOG_ERR, "quad_rotation_instantiate failed: %d", rc);
  return rc;
}

static void quad_rotation_connect_port(struct dsp_module* module,
                                       unsigned long port,
                                       float* data_location) {
  struct quad_rotation* data;
  float** ports;

  data = (struct quad_rotation*)module->data;
  ports = (float**)data->ports;
  ports[port] = data_location;
}

static void quad_rotation_deinstantiate(struct dsp_module* module) {
  if (module->data) {
    free(module->data);
    module->data = NULL;
  }
}

/*  Moves data on the four channels according to the orientation of the display
 *  and the channel map. For example, when the display rotates 90 degrees
 *  clockwise, moves the data of SPK_POS_RL to SPK_POS_FL, moves the data of
 *  SPK_POS_FL to SPK_POS_FR, moves the data of SPK_POS_RR to SPK_POS_RL, and
 *  moves the data of SPK_POS_FR to SPK_POS_RR.
 *  (The FL in the below graph is equal to SPK_POS_FL, which means the physical
 *  speaker at the position FL. The "*" is the top of the device.)
 *  _________       __________      ___________      __________
 * │         │     │         │     │         │     │         │
 * │RL  *  RR│     │FL     RL│     │FR     FL│     │RR     FR│
 * │         │     │        *│     │         │     │*        │
 * │FL     FR│     │FR     RR│     │RR  *  RL│     │RL     FL│
 * │_________│     │_________│     │_________│     │_________│
 *  ROTATE_0         ROTATE_90      ROTATE_180       ROTATE_270
 */

static void quad_rotation_run(struct dsp_module* module,
                              unsigned long sample_count) {
  struct quad_rotation* data = NULL;

  data = (struct quad_rotation*)module->data;

  switch (data->rotation) {
    case ROTATE_90:
      quad_rotation_rotate_90(data, CLOCK_WISE, sample_count);
      break;
    case ROTATE_180:
      quad_rotation_swap(data, SPK_POS_FL, SPK_POS_RR, sample_count);
      quad_rotation_swap(data, SPK_POS_RL, SPK_POS_FR, sample_count);
      break;
    case ROTATE_270:
      quad_rotation_rotate_90(data, ANTI_CLOCK_WISE, sample_count);
      break;
    default:
      break;
  }
}

static void quad_rotation_init_module(struct dsp_module* module) {
  module->instantiate = &quad_rotation_instantiate;
  module->connect_port = &quad_rotation_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &quad_rotation_run;
  module->deinstantiate = &quad_rotation_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  swap_lr module functions
 */
static int swap_lr_instantiate(struct dsp_module* module,
                               unsigned long sample_rate,
                               struct cras_expr_env* env) {
  module->data = calloc(4, sizeof(float*));
  if (!module->data) {
    syslog(LOG_ERR, "swap_lr_instantiate failed: %d", -ENOMEM);
    return -ENOMEM;
  }
  return 0;
}

static void swap_lr_connect_port(struct dsp_module* module,
                                 unsigned long port,
                                 float* data_location) {
  float** ports;
  ports = (float**)module->data;
  ports[port] = data_location;
}

static void swap_lr_run(struct dsp_module* module, unsigned long sample_count) {
  size_t i;
  float** ports = (float**)module->data;

  /* This module runs dsp in-place, so ports[0] == ports[2],
   * ports[1] == ports[3]. Here we swap data on two channels.
   */
  for (i = 0; i < sample_count; i++) {
    float temp = ports[0][i];
    ports[2][i] = ports[1][i];
    ports[3][i] = temp;
  }
}

static void swap_lr_deinstantiate(struct dsp_module* module) {
  free(module->data);
}

static void swap_lr_init_module(struct dsp_module* module) {
  module->instantiate = &swap_lr_instantiate;
  module->connect_port = &swap_lr_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &swap_lr_run;
  module->deinstantiate = &swap_lr_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  invert_lr module functions
 */
static int invert_lr_instantiate(struct dsp_module* module,
                                 unsigned long sample_rate,
                                 struct cras_expr_env* env) {
  module->data = calloc(4, sizeof(float*));
  if (!module->data) {
    syslog(LOG_ERR, "invert_lr_instantiate failed: %d", -ENOMEM);
    return -ENOMEM;
  }
  return 0;
}

static void invert_lr_connect_port(struct dsp_module* module,
                                   unsigned long port,
                                   float* data_location) {
  float** ports;
  ports = (float**)module->data;
  ports[port] = data_location;
}

static void invert_lr_run(struct dsp_module* module,
                          unsigned long sample_count) {
  size_t i;
  float** ports = (float**)module->data;

  for (i = 0; i < sample_count; i++) {
    ports[2][i] = -ports[0][i];
    ports[3][i] = ports[1][i];
  }
}

static void invert_lr_deinstantiate(struct dsp_module* module) {
  free(module->data);
}

static void invert_lr_init_module(struct dsp_module* module) {
  module->instantiate = &invert_lr_instantiate;
  module->connect_port = &invert_lr_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &invert_lr_run;
  module->deinstantiate = &invert_lr_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  mix_stereo module functions
 */
static int mix_stereo_instantiate(struct dsp_module* module,
                                  unsigned long sample_rate,
                                  struct cras_expr_env* env) {
  module->data = calloc(4, sizeof(float*));
  if (!module->data) {
    syslog(LOG_ERR, "mix_stereo_instantiate failed: %d", -ENOMEM);
    return -ENOMEM;
  }
  return 0;
}

static void mix_stereo_connect_port(struct dsp_module* module,
                                    unsigned long port,
                                    float* data_location) {
  float** ports;
  ports = (float**)module->data;
  ports[port] = data_location;
}

static void mix_stereo_run(struct dsp_module* module,
                           unsigned long sample_count) {
  size_t i;
  float tmp;
  float** ports = (float**)module->data;

  for (i = 0; i < sample_count; i++) {
    tmp = ports[0][i] + ports[1][i];
    ports[2][i] = tmp;
    ports[3][i] = tmp;
  }
}

static void mix_stereo_deinstantiate(struct dsp_module* module) {
  free(module->data);
}

static void mix_stereo_init_module(struct dsp_module* module) {
  module->instantiate = &mix_stereo_instantiate;
  module->connect_port = &mix_stereo_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &mix_stereo_run;
  module->deinstantiate = &mix_stereo_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  dcblock module functions
 */
struct dcblock_data {
  struct dcblock* dcblockl;
  struct dcblock* dcblockr;
  unsigned long sample_rate;

  // One port for input, one for output, and 1 parameter
  float* ports[5];
};

static int dcblock_instantiate(struct dsp_module* module,
                               unsigned long sample_rate,
                               struct cras_expr_env* env) {
  struct dcblock_data* data;

  module->data = calloc(1, sizeof(*data));
  if (!module->data) {
    syslog(LOG_ERR, "dcblock_instantiate failed: %d", -ENOMEM);
    return -ENOMEM;
  }

  data = module->data;
  data->dcblockl = dcblock_new();
  if (!data->dcblockl) {
    goto fail;
  }
  data->dcblockr = dcblock_new();
  if (!data->dcblockr) {
    goto fail;
  }
  data->sample_rate = sample_rate;

  return 0;
fail:
  dcblock_free(data->dcblockl);
  dcblock_free(data->dcblockr);
  free(module->data);
  module->data = NULL;
  syslog(LOG_ERR, "dcblock_instantiate failed: %d", -ENOMEM);
  return -ENOMEM;
}

static void dcblock_connect_port(struct dsp_module* module,
                                 unsigned long port,
                                 float* data_location) {
  struct dcblock_data* data = module->data;
  data->ports[port] = data_location;
}

static void dcblock_configure(struct dsp_module* module) {
  struct dcblock_data* data = module->data;
  if (!data) {
    syslog(LOG_ERR, "dcblock is not instantiated");
    return;
  }

  dcblock_set_config(data->dcblockl, *data->ports[4], data->sample_rate);
  dcblock_set_config(data->dcblockr, *data->ports[4], data->sample_rate);
}

static void dcblock_run(struct dsp_module* module, unsigned long sample_count) {
  struct dcblock_data* data = module->data;
  if (data->ports[0] != data->ports[2]) {
    memcpy(data->ports[2], data->ports[0], sizeof(float) * sample_count);
  }
  if (data->ports[1] != data->ports[3]) {
    memcpy(data->ports[3], data->ports[1], sizeof(float) * sample_count);
  }

  dcblock_process(data->dcblockl, data->ports[2], (int)sample_count);
  dcblock_process(data->dcblockr, data->ports[3], (int)sample_count);
}

static void dcblock_deinstantiate(struct dsp_module* module) {
  struct dcblock_data* data = module->data;
  if (data->dcblockl) {
    dcblock_free(data->dcblockl);
  }
  if (data->dcblockr) {
    dcblock_free(data->dcblockr);
  }
  free(data);
}

static void dcblock_init_module(struct dsp_module* module) {
  module->instantiate = &dcblock_instantiate;
  module->connect_port = &dcblock_connect_port;
  module->configure = &dcblock_configure;
  module->get_delay = &empty_get_delay;
  module->run = &dcblock_run;
  module->deinstantiate = &dcblock_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  eq module functions
 */
struct eq_data {
  int sample_rate;
  struct eq* eq;  // Initialized in eq_configure()

  // One port for input, one for output, and 4 parameters per eq
  float* ports[2 + MAX_BIQUADS_PER_EQ * 4];
};

static int eq_instantiate(struct dsp_module* module,
                          unsigned long sample_rate,
                          struct cras_expr_env* env) {
  struct eq_data* data;

  module->data = calloc(1, sizeof(*data));
  if (!module->data) {
    goto fail;
  }

  data = module->data;
  data->eq = eq_new();
  if (!data->eq) {
    goto fail;
  }

  data->sample_rate = (int)sample_rate;
  return 0;
fail:
  free(module->data);
  module->data = NULL;
  syslog(LOG_ERR, "eq_instantiate failed: %d", -ENOMEM);
  return -ENOMEM;
}

static void eq_connect_port(struct dsp_module* module,
                            unsigned long port,
                            float* data_location) {
  struct eq_data* data = module->data;
  data->ports[port] = data_location;
}

static void eq_configure(struct dsp_module* module) {
  struct eq_data* data = module->data;
  if (!data) {
    syslog(LOG_ERR, "eq is not instantiated");
    return;
  }

  float nyquist = data->sample_rate / 2;
  int i;

  for (i = 2; i < 2 + MAX_BIQUADS_PER_EQ * 4; i += 4) {
    if (!data->ports[i]) {
      break;
    }
    int type = (int)*data->ports[i];
    float freq = *data->ports[i + 1];
    float Q = *data->ports[i + 2];
    float gain = *data->ports[i + 3];
    eq_append_biquad(data->eq, type, freq / nyquist, Q, gain);
  }
}

static void eq_run(struct dsp_module* module, unsigned long sample_count) {
  struct eq_data* data = module->data;
  if (data->ports[0] != data->ports[1]) {
    memcpy(data->ports[1], data->ports[0], sizeof(float) * sample_count);
  }
  eq_process(data->eq, data->ports[1], (int)sample_count);
}

static void eq_deinstantiate(struct dsp_module* module) {
  struct eq_data* data = module->data;
  if (data->eq) {
    eq_free(data->eq);
  }
  free(data);
}

static void eq_init_module(struct dsp_module* module) {
  module->instantiate = &eq_instantiate;
  module->connect_port = &eq_connect_port;
  module->configure = &eq_configure;
  module->get_delay = &empty_get_delay;
  module->run = &eq_run;
  module->deinstantiate = &eq_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  eq2 module functions
 */
struct eq2_data {
  int sample_rate;
  struct eq2* eq2;  // Initialized in eq2_configure()

  // Two ports for input, two for output, and 8 parameters per eq pair
  float* ports[4 + MAX_BIQUADS_PER_EQ2 * 8];
};

static int eq2_instantiate(struct dsp_module* module,
                           unsigned long sample_rate,
                           struct cras_expr_env* env) {
  struct eq2_data* data;

  module->data = calloc(1, sizeof(*data));
  if (!module->data) {
    goto fail;
  }

  data = module->data;
  data->eq2 = eq2_new();
  if (!data->eq2) {
    goto fail;
  }

  data->sample_rate = (int)sample_rate;
  return 0;
fail:
  free(module->data);
  module->data = NULL;
  syslog(LOG_ERR, "eq2_instantiate failed: %d", -ENOMEM);
  return -ENOMEM;
}

static void eq2_connect_port(struct dsp_module* module,
                             unsigned long port,
                             float* data_location) {
  struct eq2_data* data = module->data;
  data->ports[port] = data_location;
}

static void eq2_configure(struct dsp_module* module) {
  struct eq2_data* data = module->data;
  if (!data) {
    syslog(LOG_ERR, "eq2 is not instantiated");
    return;
  }

  float nyquist = data->sample_rate / 2;
  int i, channel;

  for (i = 4; i < 4 + MAX_BIQUADS_PER_EQ2 * 8; i += 8) {
    if (!data->ports[i]) {
      break;
    }
    for (channel = 0; channel < 2; channel++) {
      int k = i + channel * 4;
      int type = (int)*data->ports[k];
      float freq = *data->ports[k + 1];
      float Q = *data->ports[k + 2];
      float gain = *data->ports[k + 3];
      eq2_append_biquad(data->eq2, channel, type, freq / nyquist, Q, gain);
    }
  }
}

static void eq2_run(struct dsp_module* module, unsigned long sample_count) {
  struct eq2_data* data = module->data;

  if (data->ports[0] != data->ports[2]) {
    memcpy(data->ports[2], data->ports[0], sizeof(float) * sample_count);
  }
  if (data->ports[3] != data->ports[1]) {
    memcpy(data->ports[3], data->ports[1], sizeof(float) * sample_count);
  }

  eq2_process(data->eq2, data->ports[2], data->ports[3], (int)sample_count);
}

static void eq2_deinstantiate(struct dsp_module* module) {
  struct eq2_data* data = module->data;
  if (data->eq2) {
    eq2_free(data->eq2);
  }
  free(data);
}

static void eq2_init_module(struct dsp_module* module) {
  module->instantiate = &eq2_instantiate;
  module->connect_port = &eq2_connect_port;
  module->configure = &eq2_configure;
  module->get_delay = &empty_get_delay;
  module->run = &eq2_run;
  module->deinstantiate = &eq2_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 *  drc module functions
 */
struct drc_data {
  int sample_rate;
  struct drc* drc;  // Initialized in drc_configure()

  /* Two ports for input, two for output, one for disable_emphasis,
   * and 8 parameters each band */
  float* ports[4 + 1 + 8 * 3];
};

static int drc_instantiate(struct dsp_module* module,
                           unsigned long sample_rate,
                           struct cras_expr_env* env) {
  struct drc_data* data;

  module->data = calloc(1, sizeof(*data));
  if (!module->data) {
    goto fail;
  }

  data = module->data;
  data->sample_rate = (int)sample_rate;
  data->drc = drc_new(data->sample_rate);
  if (!data->drc) {
    goto fail;
  }

  return 0;
fail:
  free(module->data);
  module->data = NULL;
  syslog(LOG_ERR, "drc_instantiate failed: %d", -ENOMEM);
  return -ENOMEM;
}

static void drc_connect_port(struct dsp_module* module,
                             unsigned long port,
                             float* data_location) {
  struct drc_data* data = module->data;
  data->ports[port] = data_location;
}

static void drc_configure(struct dsp_module* module) {
  struct drc_data* data = module->data;
  if (!data) {
    syslog(LOG_ERR, "drc is not instantiated");
    return;
  }

  int i;
  float nyquist = data->sample_rate / 2;
  struct drc* drc = data->drc;

  drc->emphasis_disabled = (int)*data->ports[4];
  for (i = 0; i < 3; i++) {
    int k = 5 + i * 8;
    float f = *data->ports[k];
    float enable = *data->ports[k + 1];
    float threshold = *data->ports[k + 2];
    float knee = *data->ports[k + 3];
    float ratio = *data->ports[k + 4];
    float attack = *data->ports[k + 5];
    float release = *data->ports[k + 6];
    float boost = *data->ports[k + 7];
    drc_set_param(drc, i, PARAM_CROSSOVER_LOWER_FREQ, f / nyquist);
    drc_set_param(drc, i, PARAM_ENABLED, enable);
    drc_set_param(drc, i, PARAM_THRESHOLD, threshold);
    drc_set_param(drc, i, PARAM_KNEE, knee);
    drc_set_param(drc, i, PARAM_RATIO, ratio);
    drc_set_param(drc, i, PARAM_ATTACK, attack);
    drc_set_param(drc, i, PARAM_RELEASE, release);
    drc_set_param(drc, i, PARAM_POST_GAIN, boost);
  }
  drc_init(drc);
}

static int drc_get_delay(struct dsp_module* module) {
  struct drc_data* data = module->data;
  return DRC_DEFAULT_PRE_DELAY * data->sample_rate;
}

static void drc_run(struct dsp_module* module, unsigned long sample_count) {
  struct drc_data* data = module->data;

  if (data->ports[0] != data->ports[2]) {
    memcpy(data->ports[2], data->ports[0], sizeof(float) * sample_count);
  }
  if (data->ports[1] != data->ports[3]) {
    memcpy(data->ports[3], data->ports[1], sizeof(float) * sample_count);
  }

  drc_process(data->drc, &data->ports[2], (int)sample_count);
}

static void drc_deinstantiate(struct dsp_module* module) {
  struct drc_data* data = module->data;
  if (data->drc) {
    drc_free(data->drc);
  }
  free(data);
}

static void drc_init_module(struct dsp_module* module) {
  module->instantiate = &drc_instantiate;
  module->connect_port = &drc_connect_port;
  module->configure = &drc_configure;
  module->get_delay = &drc_get_delay;
  module->run = &drc_run;
  module->deinstantiate = &drc_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

/*
 * sink module functions
 */
struct sink_data {
  struct ext_dsp_module* ext_module;
  float* ports[MAX_EXT_DSP_PORTS];
};

static int sink_instantiate(struct dsp_module* module,
                            unsigned long sample_rate,
                            struct cras_expr_env* env) {
  module->data = calloc(1, sizeof(struct sink_data));
  if (!module->data) {
    syslog(LOG_ERR, "sink_instantiate failed: %d", -ENOMEM);
    return -ENOMEM;
  }
  return 0;
}

static void sink_deinstantiate(struct dsp_module* module) {
  free(module->data);
}

static void sink_connect_port(struct dsp_module* module,
                              unsigned long port,
                              float* data_location) {
  if (port >= MAX_EXT_DSP_PORTS) {
    syslog(LOG_ERR, "Sink connecting port out of range: %lu.", port);
    return;
  }

  struct sink_data* data = module->data;
  data->ports[port] = data_location;
}

static void sink_run(struct dsp_module* module, unsigned long sample_count) {
  struct sink_data* data = module->data;

  if (!data->ext_module) {
    return;
  }
  data->ext_module->run(data->ext_module, sample_count);
}

static void sink_init_module(struct dsp_module* module) {
  module->instantiate = &sink_instantiate;
  module->connect_port = &sink_connect_port;
  module->configure = &empty_configure;
  module->get_delay = &empty_get_delay;
  module->run = &sink_run;
  module->deinstantiate = &sink_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

void cras_dsp_module_set_sink_ext_module(struct dsp_module* module,
                                         struct ext_dsp_module* ext_module) {
  struct sink_data* data = module->data;
  int i;
  data->ext_module = ext_module;

  if (data->ext_module == NULL) {
    return;
  }

  for (i = 0; i < MAX_EXT_DSP_PORTS; i++) {
    ext_module->ports[i] = data->ports[i];
  }
}

/*
 *  builtin module dispatcher
 */
struct dsp_module* cras_dsp_module_load_builtin(struct plugin* plugin) {
  struct dsp_module* module;
  if (strcmp(plugin->library, "builtin") != 0) {
    return NULL;
  }

  module = calloc(1, sizeof(struct dsp_module));

  if (strcmp(plugin->label, "mix_stereo") == 0) {
    mix_stereo_init_module(module);
  } else if (strcmp(plugin->label, "invert_lr") == 0) {
    invert_lr_init_module(module);
  } else if (strcmp(plugin->label, "dcblock") == 0) {
    dcblock_init_module(module);
  } else if (strcmp(plugin->label, "eq") == 0) {
    eq_init_module(module);
  } else if (strcmp(plugin->label, "eq2") == 0) {
    eq2_init_module(module);
  } else if (strcmp(plugin->label, "drc") == 0) {
    drc_init_module(module);
  } else if (strcmp(plugin->label, "swap_lr") == 0) {
    swap_lr_init_module(module);
  } else if (strcmp(plugin->label, "quad_rotation") == 0) {
    quad_rotation_init_module(module);
  } else if (strcmp(plugin->label, "sink") == 0) {
    sink_init_module(module);
  } else {
    empty_init_module(module);
  }

  return module;
}
