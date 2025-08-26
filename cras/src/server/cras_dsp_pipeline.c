/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_dsp_pipeline.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <syslog.h>
#include <time.h>

#include "cras/src/common/array.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/common/dumper.h"
#include "cras/src/dsp/dsp_util.h"
#include "cras/src/server/cras_dsp_ini.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_expr.h"
#include "cras_audio_format.h"
#include "cras_util.h"

/* We have a static representation of the dsp graph in a "struct ini",
 * and here we will construct a dynamic representation of it in a
 * "struct pipeline". The difference between the static one and the
 * dynamic one is that we will only include the subset of the dsp
 * graph actually needed in the dynamic one (so those plugins that are
 * disabled will not be included). Here are the mapping between the
 * static representation and the dynamic representation:
 *
 *      static                      dynamic
 *  -------------    --------------------------------------
 *  struct ini       struct pipeline
 *  struct plugin    struct instance
 *  strict port      struct audio_port, struct control_port
 *
 * For example, if the ini file specifies these plugins and their
 * connections:
 *
 * [A]
 * output_0={audio}
 * [B]
 * input_0={audio}
 * output_1={result}
 * [C]
 * input_0={result}
 *
 * That is, A connects to B, and B connects to C. If the plugin B is
 * now disabled, in the pipeline we construct there will only be two
 * instances (A and C) and the audio ports on these instances will
 * connect to each other directly, bypassing B.
 *
 * When DSP offload is supported on the pipeline, the load process contains two
 * major steps:
 * 1. prepare_pipeline - construct the pipeline topology and instantiate
 *                       modules linked with buffers.
 * 2. possibly_offload_pipeline - if applicable, make DSP module effects along
 *                       the pipeline offload to SOF firmware. Enable effects on
 *                       firmware while bypassing DSP modules on CRAS pipeline.
 *
 * For example, consider a common speaker pipeline adopting drc and eq2 modules.
 * When offload is applied, cras_dsp_pipeline_run will only run the sink module.
 * get_source_buffer will return the same as the sink buffer so data will flow
 * in the sink directly.
 *
 *              cras_dsp_pipeline               SOF firmware
 *  -----------------------------------------  --------------
 *   [source] === [drc] === [eq2] === [sink]
 * run---> 1   -->   2   -->   3   -->    4  -->   no effect   !offload_applied
 * run------------------------------->    1  --> (drc+eqiir)    offload_applied
 *
 */

// This represents an audio port on an instance.
struct audio_port {
  struct audio_port* peer;  // the audio port this port connects to
  struct plugin* plugin;    // the plugin corresponds to the instance
  int original_index;       // the port index in the plugin
  int buf_index;            // the buffer index in the pipeline
};

// This represents a control port on an instance.
struct control_port {
  struct control_port* peer;  // the control port this port connects to
  struct plugin* plugin;      // the plugin corresponds to the instance
  int original_index;         // the port index in the plugin
  float value;                // the value of the control port
};

DECLARE_ARRAY_TYPE(struct audio_port, audio_port_array);
DECLARE_ARRAY_TYPE(struct control_port, control_port_array);

/* An instance is a dynamic representation of a plugin. We only create
 * an instance when a plugin is needed (data actually flows through it
 * and it is not disabled). An instance also contains a pointer to a
 * struct dsp_module, which is the implementation of the plugin */
struct instance {
  // The plugin this instance corresponds to
  struct plugin* plugin;

  /* These are the ports on this instance. The difference
   * between this and the port array in a struct plugin is that
   * the ports skip disabled plugins and connect to the upstream
   * ports directly.
   */
  audio_port_array input_audio_ports;
  audio_port_array output_audio_ports;
  control_port_array input_control_ports;
  control_port_array output_control_ports;

  // The implementation of the plugin
  struct dsp_module* module;

  // Whether this module's instantiate() function has been called
  int instantiated;

  // This caches the value returned from get_properties() of a module
  int properties;

  /* This is the total buffering delay from source to this instance. It is
   * in number of frames. */
  int total_delay;
};

DECLARE_ARRAY_TYPE(struct instance, instance_array)

// An pipeline is a dynamic representation of a dsp ini file.
struct pipeline {
  // The purpose of the pipeline. "playback" or "capture"
  const char* purpose;

  // The ini file this pipeline comes from
  struct ini* ini;

  /* All needed instances for this pipeline. It is sorted in an
   * order that if instance B depends on instance A, then A will
   * appear in front of B. */
  instance_array instances;

  /* The maximum number of audio buffers that will be used at
   * the same time for this pipeline */
  int peak_buf;

  // The audio data buffers
  float** buffers;

  // The instance where the audio data flow in
  struct instance* source_instance;

  // The instance where the audio data flow out
  struct instance* sink_instance;

  // The number of audio channels for this pipeline
  int input_channels;
  int output_channels;

  /* The audio sampling rate for this pipeline. It is zero if
   * cras_dsp_pipeline_instantiate() has not been called. */
  int sample_rate;

  // The total time it takes to run the pipeline, in nanoseconds.
  int64_t total_time;

  // The max/min time it takes to run the pipeline, in nanoseconds.
  int64_t max_time;
  int64_t min_time;

  // The number of blocks the pipeline.
  int64_t total_blocks;

  // The total number of sample frames the pipeline processed
  int64_t total_samples;

  // The flag to indicate whether DSP offload is applied on the pipeline.
  bool offload_applied;
};

static struct instance* find_instance_by_plugin(const instance_array* instances,
                                                const struct plugin* plugin) {
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (instances, i, instance) {
    if (instance->plugin == plugin) {
      return instance;
    }
  }

  return NULL;
}

/* Finds out where the data sent to plugin:index come from. The issue
 * we need to handle here is the previous plugin may be disabled, so
 * we need to go upstream until we find the real origin */
static int find_origin_port(struct ini* ini,
                            const instance_array* instances,
                            const struct plugin* plugin,
                            int index,
                            const struct plugin** origin,
                            int* origin_index) {
  enum port_type type;
  struct port* port;
  int flow_id;
  struct flow* flow;
  int i, k;
  int found;

  port = ARRAY_ELEMENT(&plugin->ports, index);
  type = port->type;
  flow_id = port->flow_id;
  if (flow_id == INVALID_FLOW_ID) {
    return -EINVAL;
  }
  flow = ARRAY_ELEMENT(&ini->flows, flow_id);

  // move to the previous plugin
  plugin = flow->from;
  index = flow->from_port;

  // if the plugin is not disabled, it will be pointed by some instance
  if (find_instance_by_plugin(instances, plugin)) {
    *origin = plugin;
    *origin_index = index;
    return 0;
  }

  /* Now we know the previous plugin is disabled, we need to go
   * upstream. We assume the k-th output port of the plugin
   * corresponds to the k-th input port of the plugin (with the
   * same type) */

  k = 0;
  found = 0;
  ARRAY_ELEMENT_FOREACH (&plugin->ports, i, port) {
    if (index == i) {
      found = 1;
      break;
    }
    if (port->direction == PORT_OUTPUT && port->type == type) {
      k++;
    }
  }
  if (!found) {
    return -ENOENT;
  }

  found = 0;
  ARRAY_ELEMENT_FOREACH (&plugin->ports, i, port) {
    if (port->direction == PORT_INPUT && port->type == type) {
      if (k-- == 0) {
        index = i;
        found = 1;
        break;
      }
    }
  }
  if (!found) {
    return -ENOENT;
  }

  return find_origin_port(ini, instances, plugin, index, origin, origin_index);
}

static struct audio_port* find_output_audio_port(instance_array* instances,
                                                 const struct plugin* plugin,
                                                 int index) {
  int i;
  struct instance* instance;
  struct audio_port* audio_port;

  instance = find_instance_by_plugin(instances, plugin);
  if (!instance) {
    return NULL;
  }

  ARRAY_ELEMENT_FOREACH (&instance->output_audio_ports, i, audio_port) {
    if (audio_port->original_index == index) {
      return audio_port;
    }
  }

  return NULL;
}

static struct control_port* find_output_control_port(
    instance_array* instances,
    const struct plugin* plugin,
    int index) {
  int i;
  struct instance* instance;
  struct control_port* control_port;

  instance = find_instance_by_plugin(instances, plugin);
  if (!instance) {
    return NULL;
  }

  ARRAY_ELEMENT_FOREACH (&instance->output_control_ports, i, control_port) {
    if (control_port->original_index == index) {
      return control_port;
    }
  }

  return NULL;
}

static char is_disabled(struct plugin* plugin, struct cras_expr_env* env) {
  char disabled;
  return (plugin->disable_expr &&
          cras_expr_expression_eval_boolean(plugin->disable_expr, env,
                                            &disabled) == 0 &&
          disabled == 1);
}

static int topological_sort(struct pipeline* pipeline,
                            struct cras_expr_env* env,
                            struct plugin* plugin,
                            char* visited) {
  struct port* port;
  struct flow* flow;
  int index, i, flow_id, ret;
  struct instance* instance;
  struct ini* ini = pipeline->ini;

  index = ARRAY_INDEX(&ini->plugins, plugin);
  if (visited[index]) {
    return 0;
  }
  visited[index] = 1;

  ARRAY_ELEMENT_FOREACH (&plugin->ports, i, port) {
    if (port->flow_id == INVALID_FLOW_ID) {
      continue;
    }
    flow_id = port->flow_id;
    flow = ARRAY_ELEMENT(&ini->flows, flow_id);
    if (!flow->from) {
      syslog(LOG_ERR, "no plugin flows to %s:%d", plugin->title, i);
      return -EINVAL;
    }
    ret = topological_sort(pipeline, env, flow->from, visited);
    if (ret < 0) {
      return ret;
    }
  }

  // if the plugin is disabled, we don't construct an instance for it
  if (is_disabled(plugin, env)) {
    return 0;
  }

  instance = ARRAY_APPEND_ZERO(&pipeline->instances);
  instance->plugin = plugin;

  // constructs audio and control ports for the instance
  ARRAY_ELEMENT_FOREACH (&plugin->ports, i, port) {
    int need_connect =
        (port->flow_id != INVALID_FLOW_ID && port->direction == PORT_INPUT);
    const struct plugin* origin = NULL;
    int origin_index = 0;

    if (need_connect) {
      ret = find_origin_port(ini, &pipeline->instances, plugin, i, &origin,
                             &origin_index);
      if (ret < 0) {
        return ret;
      }
    }

    if (port->type == PORT_AUDIO) {
      audio_port_array* audio_port_array = (port->direction == PORT_INPUT)
                                               ? &instance->input_audio_ports
                                               : &instance->output_audio_ports;
      struct audio_port* audio_port = ARRAY_APPEND_ZERO(audio_port_array);
      audio_port->plugin = plugin;
      audio_port->original_index = i;
      if (need_connect) {
        struct audio_port* from;
        from =
            find_output_audio_port(&pipeline->instances, origin, origin_index);
        if (!from) {
          return -ENOENT;
        }
        from->peer = audio_port;
        audio_port->peer = from;
      }
    } else if (port->type == PORT_CONTROL) {
      control_port_array* control_port_array =
          (port->direction == PORT_INPUT) ? &instance->input_control_ports
                                          : &instance->output_control_ports;
      struct control_port* control_port = ARRAY_APPEND_ZERO(control_port_array);
      control_port->plugin = plugin;
      control_port->original_index = i;
      control_port->value = port->init_value;
      if (need_connect) {
        struct control_port* from;
        from = find_output_control_port(&pipeline->instances, origin,
                                        origin_index);
        if (!from) {
          return -ENOENT;
        }
        from->peer = control_port;
        control_port->peer = from;
      }
    }
  }

  return 0;
}

static struct plugin* find_enabled_builtin_plugin(struct ini* ini,
                                                  const char* label,
                                                  const char* purpose,
                                                  struct cras_expr_env* env) {
  int i;
  struct plugin *plugin, *found = NULL;

  ARRAY_ELEMENT_FOREACH (&ini->plugins, i, plugin) {
    if (strcmp(plugin->library, "builtin") != 0) {
      continue;
    }
    if (strcmp(plugin->label, label) != 0) {
      continue;
    }
    if (!plugin->purpose || strcmp(plugin->purpose, purpose) != 0) {
      continue;
    }
    if (is_disabled(plugin, env)) {
      continue;
    }
    if (found) {
      syslog(LOG_ERR, "two %s plugins enabled: %s and %s", label, found->title,
             plugin->title);
      return NULL;
    }
    found = plugin;
  }

  return found;
}

struct pipeline* cras_dsp_pipeline_create(struct ini* ini,
                                          struct cras_expr_env* env,
                                          const char* purpose) {
  struct pipeline* pipeline;
  int n;
  char* visited;
  int rc;
  struct plugin* source =
      find_enabled_builtin_plugin(ini, "source", purpose, env);
  struct plugin* sink = find_enabled_builtin_plugin(ini, "sink", purpose, env);

  if (!source || !sink) {
    syslog(LOG_DEBUG,
           "no enabled pipeline found in ini for %s. source(%p), sink(%p).",
           purpose, source, sink);
    return NULL;
  }

  pipeline = calloc(1, sizeof(*pipeline));
  if (!pipeline) {
    syslog(LOG_ERR, "no memory for pipeline");
    return NULL;
  }

  pipeline->ini = ini;
  pipeline->purpose = purpose;
  // create instances for needed plugins, in the order of dependency
  n = ARRAY_COUNT(&ini->plugins);
  visited = calloc(1, n);
  rc = topological_sort(pipeline, env, sink, visited);
  free(visited);

  if (rc < 0) {
    syslog(LOG_ERR, "failed to construct pipeline");
    cras_dsp_pipeline_free(pipeline);
    return NULL;
  }

  pipeline->source_instance =
      find_instance_by_plugin(&pipeline->instances, source);
  pipeline->sink_instance = find_instance_by_plugin(&pipeline->instances, sink);

  if (!pipeline->source_instance || !pipeline->sink_instance) {
    syslog(LOG_ERR, "source(%p) or sink(%p) missing/disabled?", source, sink);
    cras_dsp_pipeline_free(pipeline);
    return NULL;
  }

  pipeline->input_channels =
      ARRAY_COUNT(&pipeline->source_instance->output_audio_ports);
  pipeline->output_channels =
      ARRAY_COUNT(&pipeline->sink_instance->input_audio_ports);
  if (pipeline->output_channels > pipeline->input_channels) {
    // Can't increase channel count, no where to put them.
    syslog(LOG_ERR, "DSP output more channels than input\n");
    cras_dsp_pipeline_free(pipeline);
    return NULL;
  }

  return pipeline;
}

static int load_module(struct plugin* plugin, struct instance* instance) {
  struct dsp_module* module;
  module = cras_dsp_module_load_builtin(plugin);
  if (!module) {
    return -ENOENT;
  }
  instance->module = module;
  instance->properties = module->get_properties(module);
  return 0;
}

static void use_buffers(char* busy, audio_port_array* audio_ports) {
  int i, k = 0;
  struct audio_port* audio_port;

  ARRAY_ELEMENT_FOREACH (audio_ports, i, audio_port) {
    while (busy[k]) {
      k++;
    }
    audio_port->buf_index = k;
    busy[k] = 1;
  }
}

static void unuse_buffers(char* busy, audio_port_array* audio_ports) {
  int i;
  struct audio_port* audio_port;

  ARRAY_ELEMENT_FOREACH (audio_ports, i, audio_port) {
    busy[audio_port->buf_index] = 0;
  }
}

// assign which buffer each audio port on each instance should use
static int allocate_buffers(struct pipeline* pipeline) {
  int i;
  struct instance* instance;
  int need_buf = 0, peak_buf = 0;
  char* busy;

  // first figure out how many buffers do we need
  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    int in = ARRAY_COUNT(&instance->input_audio_ports);
    int out = ARRAY_COUNT(&instance->output_audio_ports);

    if (instance->properties & MODULE_INPLACE_BROKEN) {
      /* We cannot reuse input buffer as output
       * buffer, so we need to use extra buffers */
      need_buf += out;
      peak_buf = MAX(peak_buf, need_buf);
      need_buf -= in;
    } else {
      need_buf += out - in;
      peak_buf = MAX(peak_buf, need_buf);
    }
  }
  /*
   * cras_dsp_pipeline_create creates pipeline with source and sink and it
   * makes sure all ports could be accessed from some sources, which means
   * that there is at least one source with out > 0 and in == 0.
   * This will give us peak_buf > 0 in the previous calculation.
   */
  if (peak_buf <= 0) {
    syslog(LOG_ERR, "peak_buf = %d, which must be greater than 0.", peak_buf);
    return -EINVAL;
  }

  // then allocate the buffers
  pipeline->peak_buf = peak_buf;
  pipeline->buffers = (float**)calloc(peak_buf, sizeof(float*));

  if (!pipeline->buffers) {
    syslog(LOG_ERR, "failed to allocate buffers");
    return -ENOMEM;
  }

  for (i = 0; i < peak_buf; i++) {
    size_t size = DSP_BUFFER_SIZE * sizeof(float);
    float* buf = calloc(1, size);
    if (!buf) {
      syslog(LOG_ERR, "failed to allocate buf");
      return -ENOMEM;
    }
    pipeline->buffers[i] = buf;
  }

  // Now assign buffer index for each instance's input/output ports
  busy = calloc(peak_buf, sizeof(*busy));
  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    int j;
    struct audio_port* audio_port;

    // Collect input buffers from upstream
    ARRAY_ELEMENT_FOREACH (&instance->input_audio_ports, j, audio_port) {
      audio_port->buf_index = audio_port->peer->buf_index;
    }

    /* If the module has the MODULE_INPLACE_BROKEN flag,
     * we cannot reuse input buffers as output buffers, so
     * we need to use extra buffers. For example, in this graph
     *
     * [A]
     * output_0={x}
     * output_1={y}
     * output_2={z}
     * output_3={w}
     * [B]
     * input_0={x}
     * input_1={y}
     * input_2={z}
     * input_3={w}
     * output_4={u}
     *
     * Then peak_buf for this pipeline is 4. However if
     * plugin B has the MODULE_INPLACE_BROKEN flag, then
     * peak_buf is 5 because plugin B cannot output to the
     * same buffer used for input.
     *
     * This means if we don't have the flag, we can free
     * the input buffers then allocate the output buffers,
     * but if we have the flag, we have to allocate the
     * output buffers before freeing the input buffers.
     */
    if (instance->properties & MODULE_INPLACE_BROKEN) {
      use_buffers(busy, &instance->output_audio_ports);
      unuse_buffers(busy, &instance->input_audio_ports);
    } else {
      unuse_buffers(busy, &instance->input_audio_ports);
      use_buffers(busy, &instance->output_audio_ports);
    }
  }
  free(busy);

  return 0;
}

int cras_dsp_pipeline_load(struct pipeline* pipeline) {
  int i, ret;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct plugin* plugin = instance->plugin;
    ret = load_module(plugin, instance);
    if (ret != 0) {
      return ret;
    }
  }

  return allocate_buffers(pipeline);
}

// Calculates the total buffering delay of each instance from the source
static void calculate_audio_delay(struct pipeline* pipeline) {
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    audio_port_array* audio_in = &instance->input_audio_ports;
    struct audio_port* audio_port;
    int delay = 0;
    int j;

    /* Finds the max delay of all modules that provide input to this
     * instance. */
    ARRAY_ELEMENT_FOREACH (audio_in, j, audio_port) {
      struct instance* upstream = find_instance_by_plugin(
          &pipeline->instances, audio_port->peer->plugin);
      delay = MAX(upstream->total_delay, delay);
    }

    instance->total_delay = delay + module->get_delay(module);
  }
}

int cras_dsp_pipeline_instantiate(struct pipeline* pipeline,
                                  int sample_rate,
                                  struct cras_expr_env* env) {
  int i, ret;
  struct instance* instance;

  if (!env) {
    syslog(LOG_ERR, "invalid cras_expr_env");
    return -EINVAL;
  }

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    ret = module->instantiate(module, sample_rate, env);
    if (ret < 0) {
      return ret;
    }
    instance->instantiated = 1;
    syslog(LOG_DEBUG, "instantiate %s", instance->plugin->label);
  }
  pipeline->sample_rate = sample_rate;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    audio_port_array* audio_in = &instance->input_audio_ports;
    audio_port_array* audio_out = &instance->output_audio_ports;
    control_port_array* control_in = &instance->input_control_ports;
    control_port_array* control_out = &instance->output_control_ports;
    int j;
    struct audio_port* audio_port;
    struct control_port* control_port;
    struct dsp_module* module = instance->module;

    // connect audio ports
    ARRAY_ELEMENT_FOREACH (audio_in, j, audio_port) {
      float* buf = pipeline->buffers[audio_port->buf_index];
      module->connect_port(module, audio_port->original_index, buf);
      syslog(LOG_DEBUG, "connect audio buf %d to %s:%d (in)",
             audio_port->buf_index, instance->plugin->title,
             audio_port->original_index);
    }
    ARRAY_ELEMENT_FOREACH (audio_out, j, audio_port) {
      float* buf = pipeline->buffers[audio_port->buf_index];
      module->connect_port(module, audio_port->original_index, buf);
      syslog(LOG_DEBUG, "connect audio buf %d to %s:%d (out)",
             audio_port->buf_index, instance->plugin->title,
             audio_port->original_index);
    }

    // connect control ports
    ARRAY_ELEMENT_FOREACH (control_in, j, control_port) {
      /* Note for input control ports which has a
       * peer, we use &control_port->peer->value, so
       * we can get the peer port's output value
       * directly */
      float* value = control_port->peer ? &control_port->peer->value
                                        : &control_port->value;
      module->connect_port(module, control_port->original_index, value);
      syslog(LOG_DEBUG, "connect control (val=%g) to %s:%d (in)",
             control_port->value, instance->plugin->title,
             control_port->original_index);
    }
    ARRAY_ELEMENT_FOREACH (control_out, j, control_port) {
      module->connect_port(module, control_port->original_index,
                           &control_port->value);
      syslog(LOG_DEBUG, "connect control (val=%g) to %s:%d (out)",
             control_port->value, instance->plugin->title,
             control_port->original_index);
    }
  }

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    module->configure(module);
  }

  calculate_audio_delay(pipeline);
  return 0;
}

void cras_dsp_pipeline_deinstantiate(struct pipeline* pipeline) {
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    if (instance->instantiated) {
      module->deinstantiate(module);
      instance->instantiated = 0;
    }
  }
  pipeline->sample_rate = 0;
}

int cras_dsp_pipeline_get_delay(struct pipeline* pipeline) {
  return pipeline->sink_instance->total_delay;
}

int cras_dsp_pipeline_get_sample_rate(struct pipeline* pipeline) {
  return pipeline->sample_rate;
}

int cras_dsp_pipeline_get_num_input_channels(struct pipeline* pipeline) {
  return pipeline->input_channels;
}

int cras_dsp_pipeline_get_num_output_channels(struct pipeline* pipeline) {
  return pipeline->output_channels;
}

int cras_dsp_pipeline_get_peak_audio_buffers(struct pipeline* pipeline) {
  return pipeline->peak_buf;
}

static int find_buf_index(audio_port_array* audio_ports, int index) {
  int i;
  struct audio_port* audio_port;

  ARRAY_ELEMENT_FOREACH (audio_ports, i, audio_port) {
    if (audio_port->original_index == index) {
      return audio_port->buf_index;
    }
  }
  return -EINVAL;
}

static float* find_buffer(struct pipeline* pipeline,
                          audio_port_array* audio_ports,
                          int index) {
  int buf_index = find_buf_index(audio_ports, index);
  if (buf_index < 0) {
    return NULL;
  }
  return pipeline->buffers[buf_index];
}

float* cras_dsp_pipeline_get_source_buffer(struct pipeline* pipeline,
                                           int index) {
  if (pipeline->offload_applied) {
    // Audio samples will be written straight to the sink while offloaded.
    return cras_dsp_pipeline_get_sink_buffer(pipeline, index);
  }
  return find_buffer(pipeline, &pipeline->source_instance->output_audio_ports,
                     index);
}

float* cras_dsp_pipeline_get_sink_buffer(struct pipeline* pipeline, int index) {
  return find_buffer(pipeline, &pipeline->sink_instance->input_audio_ports,
                     index);
}

void cras_dsp_pipeline_set_sink_ext_module(struct pipeline* pipeline,
                                           struct ext_dsp_module* ext_module) {
  cras_dsp_module_set_sink_ext_module(pipeline->sink_instance->module,
                                      ext_module);
}

void cras_dsp_pipeline_set_sink_lr_swapped(struct pipeline* pipeline,
                                           bool left_right_swapped) {
  cras_dsp_module_set_sink_lr_swapped(pipeline->sink_instance->module,
                                      left_right_swapped);
}

struct ini* cras_dsp_pipeline_get_ini(struct pipeline* pipeline) {
  return pipeline->ini;
}

void cras_dsp_pipeline_apply_offload(struct pipeline* pipeline, bool applied) {
  if (!pipeline) {
    return;
  }
  if (pipeline->input_channels != pipeline->output_channels) {
    syslog(LOG_ERR,
           "Unable to apply offload for channel-variant pipeline. "
           "(in: %d-ch, out: %d-ch)",
           pipeline->input_channels, pipeline->output_channels);
    return;
  }

  syslog(LOG_DEBUG, "cras_dsp_pipeline->offload_applied = %d", applied);
  pipeline->offload_applied = applied;
}

// If label is equal to "source" or "sink".
static bool is_endpoint(const char* label) {
  return str_equals(label, "source") || str_equals(label, "sink");
}

char* cras_dsp_pipeline_get_pattern(const struct pipeline* pipeline) {
  char dsp_pattern[DSP_PATTERN_MAX_SIZE];
  size_t len = 0;
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    const char* label = instance->plugin->label;
    if (is_endpoint(label)) {
      continue;  // don't print out source or sink
    }
    if (len > 0) {
      // catenate the delimiter ">"
      strlcpy(dsp_pattern + len, ">", sizeof(dsp_pattern) - len);
      len++;
    }
    // catenate the label of DSP module
    size_t n = strlcpy(dsp_pattern + len, label, sizeof(dsp_pattern) - len);
    if (n >= sizeof(dsp_pattern) - len) {
      break;  // pattern is too long
    }
    len += n;
  }
  return strndup(dsp_pattern, DSP_PATTERN_MAX_SIZE - 1);
}

int cras_dsp_pipeline_config_offload(struct dsp_offload_map* offload_map,
                                     struct pipeline* pipeline) {
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    const char* label = instance->plugin->label;
    if (is_endpoint(label)) {
      continue;
    }
    int rc =
        cras_dsp_offload_config_module(offload_map, instance->module, label);
    if (rc) {
      syslog(LOG_ERR, "pipeline_config_offload: Error configuring module %s",
             label);
      return rc;
    }
  }
  return 0;
}

int cras_dsp_pipeline_run(struct pipeline* pipeline, int sample_count) {
  int i;
  struct instance* instance;

  if (pipeline->offload_applied) {
    // Skip all DSP modules during pipeline run except for the sink.
    struct dsp_module* module = pipeline->sink_instance->module;
    if (!module) {
      syslog(LOG_ERR, "No module found for sink instance");
      return -EINVAL;
    }
    module->run(module, sample_count);
    return 0;
  }

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    if (!module) {
      syslog(LOG_ERR, "No module found for %s instance",
             instance->plugin->title);
      return -EINVAL;
    }
    if (!module->run) {
      syslog(LOG_ERR, "No processing function found for %s instance",
             instance->plugin->title);
      return -EINVAL;
    }
    module->run(module, sample_count);
  }
  return 0;
}

void cras_dsp_pipeline_add_statistic(struct pipeline* pipeline,
                                     const struct timespec* time_delta,
                                     int samples) {
  int64_t t;
  if (samples <= 0) {
    return;
  }

  t = time_delta->tv_sec * 1000000000LL + time_delta->tv_nsec;

  if (pipeline->total_blocks == 0) {
    pipeline->max_time = t;
    pipeline->min_time = t;
  } else {
    pipeline->max_time = MAX(pipeline->max_time, t);
    pipeline->min_time = MIN(pipeline->min_time, t);
  }

  pipeline->total_blocks++;
  pipeline->total_samples += samples;
  pipeline->total_time += t;
}

int cras_dsp_pipeline_apply(struct pipeline* pipeline,
                            uint8_t* buf,
                            snd_pcm_format_t format,
                            unsigned int frames) {
  size_t remaining;
  size_t chunk;
  size_t i;
  struct timespec begin, end, delta;
  int rc;

  if (!pipeline || frames == 0) {
    return 0;
  }
  unsigned int input_channels = pipeline->input_channels;
  unsigned int output_channels = pipeline->output_channels;
  float* source[input_channels];
  float* sink[output_channels];

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &begin);

  // get pointers to source and sink buffers
  for (i = 0; i < input_channels; i++) {
    source[i] = cras_dsp_pipeline_get_source_buffer(pipeline, i);
    if (!source[i]) {
      syslog(LOG_ERR, "No source buffer found for index %zu", i);
      return -EINVAL;
    }
  }
  for (i = 0; i < output_channels; i++) {
    sink[i] = cras_dsp_pipeline_get_sink_buffer(pipeline, i);
    if (!sink[i]) {
      syslog(LOG_ERR, "No sink buffer found for index %zu", i);
      return -EINVAL;
    }
  }

  remaining = frames;

  // process at most DSP_BUFFER_SIZE frames each loop
  while (remaining > 0) {
    chunk = MIN(remaining, (size_t)DSP_BUFFER_SIZE);

    if (!buf) {
      syslog(LOG_ERR,
             "%s: NULL sample buffer received, total frames = %u, remaining "
             "frames = %zu",
             __func__, frames, remaining);
      return -EINVAL;
    }

    // deinterleave and convert to float
    rc = dsp_util_deinterleave(buf, source, input_channels, format, chunk);
    if (rc) {
      return rc;
    }

    // Run the pipeline
    rc = cras_dsp_pipeline_run(pipeline, chunk);
    if (rc) {
      return rc;
    }

    // interleave and convert back to int16_t
    rc = dsp_util_interleave(sink, buf, output_channels, format, chunk);
    if (rc) {
      return rc;
    }

    buf += chunk * output_channels * PCM_FORMAT_WIDTH(format) / 8;
    remaining -= chunk;
  }

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
  subtract_timespecs(&end, &begin, &delta);
  cras_dsp_pipeline_add_statistic(pipeline, &delta, frames);
  return 0;
}

void cras_dsp_pipeline_free(struct pipeline* pipeline) {
  int i;
  struct instance* instance;

  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    instance->plugin = NULL;
    ARRAY_FREE(&instance->input_audio_ports);
    ARRAY_FREE(&instance->input_control_ports);
    ARRAY_FREE(&instance->output_audio_ports);
    ARRAY_FREE(&instance->output_control_ports);

    if (module) {
      if (instance->instantiated) {
        module->deinstantiate(module);
        instance->instantiated = 0;
      }
      module->free_module(module);
      instance->module = NULL;
    }
  }

  pipeline->ini = NULL;
  ARRAY_FREE(&pipeline->instances);

  for (i = 0; i < pipeline->peak_buf; i++) {
    free(pipeline->buffers[i]);
  }
  free(pipeline->buffers);
  free(pipeline);
}

static void dump_audio_ports(struct dumper* d,
                             const char* name,
                             audio_port_array* audio_ports) {
  int i;
  struct audio_port* audio_port;
  int n = ARRAY_COUNT(audio_ports);

  if (n == 0) {
    return;
  }
  dumpf(d, "   %s (%d) =\n", name, n);

  ARRAY_ELEMENT_FOREACH (audio_ports, i, audio_port) {
    dumpf(d, "   %p, peer %p, orig=%d, buf=%d\n", audio_port, audio_port->peer,
          audio_port->original_index, audio_port->buf_index);
  }
}

static void dump_control_ports(struct dumper* d,
                               const char* name,
                               control_port_array* control_ports) {
  int i;
  struct control_port* control_port;
  int n = ARRAY_COUNT(control_ports);

  if (n == 0) {
    return;
  }
  dumpf(d, "   %s (%d) =\n", name, ARRAY_COUNT(control_ports));

  ARRAY_ELEMENT_FOREACH (control_ports, i, control_port) {
    dumpf(d, "   %p, peer %p, orig=%d, value=%g\n", control_port,
          control_port->peer, control_port->original_index,
          control_port->value);
  }
}

void cras_dsp_pipeline_dump(struct dumper* d, struct pipeline* pipeline) {
  int i;
  struct instance* instance;

  dumpf(d, "---- pipeline dump begin ----\n");
  dumpf(d, "pipeline (%s):\n", pipeline->purpose);
  dumpf(d, " input channels: %d\n", pipeline->input_channels);
  dumpf(d, " output channels: %d\n", pipeline->output_channels);
  dumpf(d, " sample_rate: %d\n", pipeline->sample_rate);
  dumpf(d, " offload_applied: %d\n", pipeline->offload_applied);
  dumpf(d, " processed samples: %" PRId64 "\n", pipeline->total_samples);
  dumpf(d, " processed blocks: %" PRId64 "\n", pipeline->total_blocks);
  dumpf(d, " total processing time: %" PRId64 "ns\n", pipeline->total_time);
  if (pipeline->total_blocks) {
    dumpf(d, " average block size: %" PRId64 "\n",
          pipeline->total_samples / pipeline->total_blocks);
    dumpf(d, " avg processing time per block: %" PRId64 "ns\n",
          pipeline->total_time / pipeline->total_blocks);
  }
  dumpf(d, " min processing time per block: %" PRId64 "ns\n",
        pipeline->min_time);
  dumpf(d, " max processing time per block: %" PRId64 "ns\n",
        pipeline->max_time);
  if (pipeline->total_samples) {
    dumpf(d, " cpu load: %g%%\n",
          pipeline->total_time * 1e-9 / pipeline->total_samples *
              pipeline->sample_rate * 100);
  }
  dumpf(d, " instances (%d):\n", ARRAY_COUNT(&pipeline->instances));
  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    dumpf(d, "  [%d]%s mod=%p, total delay=%d\n", i, instance->plugin->title,
          module, instance->total_delay);
    if (module) {
      module->dump(module, d);
    }
    dump_audio_ports(d, "input_audio_ports", &instance->input_audio_ports);
    dump_audio_ports(d, "output_audio_ports", &instance->output_audio_ports);
    dump_control_ports(d, "input_control_ports",
                       &instance->input_control_ports);
    dump_control_ports(d, "output_control_ports",
                       &instance->output_control_ports);
  }
  dumpf(d, " peak_buf = %d\n", pipeline->peak_buf);
  dumpf(d, "---- pipeline dump end ----\n");
}

CRAS_STREAM_ACTIVE_AP_EFFECT cras_dsp_pipeline_get_active_ap_effects(
    const struct pipeline* pipeline) {
  if (!pipeline) {
    return 0;
  }

  int i;
  struct instance* instance;
  CRAS_STREAM_ACTIVE_AP_EFFECT effects = 0;
  ARRAY_ELEMENT_FOREACH (&pipeline->instances, i, instance) {
    struct dsp_module* module = instance->module;
    if (module) {
      if (str_equals(instance->plugin->label, CRAS_DSP_MOD_LABEL_GEN_ECHO) ||
          str_equals(instance->plugin->label,
                     CRAS_DSP_MOD_LABEL_SPEAKER_PLUGIN) ||
          str_equals(instance->plugin->label,
                     CRAS_DSP_MOD_LABEL_HEADPHONE_PLUGIN)) {
        effects |= cras_processor_effect_to_active_ap_effects(
            module->get_properties(module));
      }
    }
  }
  return effects;
}

int cras_dsp_pipeline_validate(const struct pipeline* pipeline,
                               const struct cras_audio_format* format) {
  if (!pipeline) {
    return 0;
  }
  const unsigned int input_channels = pipeline->input_channels;
  const unsigned int output_channels = pipeline->output_channels;

  if (input_channels != format->num_channels) {
    syslog(LOG_ERR,
           "Pipeline source channel count %u does not match device channel "
           "count %zu",
           input_channels, format->num_channels);
    return -EINVAL;
  }
  if (output_channels != format->num_channels) {
    syslog(LOG_ERR,
           "Pipeline sink channel count %u does not match device channel "
           "count %zu",
           output_channels, format->num_channels);
    return -EINVAL;
  }

  return 0;
}
