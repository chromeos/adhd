/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_MODULE_H_
#define CRAS_SRC_SERVER_CRAS_DSP_MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "cras/src/server/cras_dsp_ini.h"

// TODO: Study feasibility of increasing this limit.
#define MAX_EXT_DSP_PORTS 8

static const char CRAS_DSP_MOD_LABEL_GEN_ECHO[] = "gen_echo2";
static const char CRAS_DSP_MOD_LABEL_SPEAKER_PLUGIN[] = "speaker_plugin_effect";
static const char CRAS_DSP_MOD_LABEL_HEADPHONE_PLUGIN[] =
    "headphone_plugin_effect";

// Holds the functions we can use on a dsp module.
struct dsp_module {
  // Opaque data used by the implementation of this module
  void* data;

  /* Initializes the module for a given sampling rate. To change
   * the sampling rate, deinstantiate() must be called before
   * calling instantiate again.
   * Args:
   *    sample_rate - The sampling rate for the audio data, like 44100.
   *    env         - The expression environment.
   * Returns:
   *    0 if the initialization is successful. A negative error code
   *    otherwise.
   */
  int (*instantiate)(struct dsp_module* mod,
                     unsigned long sample_rate,
                     struct cras_expr_env* env);

  /* Assigns the memory location for a port of this module.
   * Args:
   *    port - The index of the port.
   *    data_location - The memory address of the data for this port.
   */
  void (*connect_port)(struct dsp_module* mod,
                       unsigned long port,
                       float* data_location);

  /* Configures parameters from the data in control ports if any, and
   * initializes the dsp engine if necessary. It should be called once
   * prior to the run() call.
   */
  void (*configure)(struct dsp_module* mod);

  /* Generates the config blob of this module for DSP offload.
   * If needed, it should be called right after the configure() call.
   * Args:
   *    config - The pointer of the config blob buffer to be returned. The
   *             buffer will be allocated in this function. Clients should help
   *             on release after use.
   *    config_size - The config blob size in bytes.
   * Returns:
   *    0 if the generation is successful. A negative error code otherwise.
   */
  int (*get_offload_blob)(struct dsp_module* mod,
                          uint32_t** config,
                          size_t* config_size);

  /* Returns the buffering delay of this module. This should be called
   * only after all input control ports have been connected.
   * Returns:
   *     The buffering delay in frames. The value returned should only be
   * based on the sampling rate and the input control ports values and not
   * the audio data itself.
   */
  int (*get_delay)(struct dsp_module* mod);

  /* Processes a block of samples using this module. The memory
   * location for the input and output data are assigned by the
   * connect_port() call.
   * Args:
   *    sample_count - The number of samples to be processed.
   */
  void (*run)(struct dsp_module* mod, unsigned long sample_count);

  /* Free resources used by the module. This module can be used
   * again by calling instantiate() */
  void (*deinstantiate)(struct dsp_module* mod);

  /* Frees all resources used by this module. After calling
   * free_module(), this struct dsp_module cannot be used
   * anymore.
   */
  void (*free_module)(struct dsp_module* mod);

  /* Returns special properties of this module, see the enum
   * below for details */
  int (*get_properties)(struct dsp_module* mod);

  // Dumps the information about current state of this module
  void (*dump)(struct dsp_module* mod, struct dumper* d);
};

/* An external module interface working with existing dsp pipeline.
 * __________  ___________        ____________      __________
 * |        |  |         |        |          |      |        |
 * |        |->| dsp mod |-> ...->| dsp mod  | ---> |        |
 * | device |  |_________|        |__________|      | stream |
 * |        |                      | ___________    |        |
 * |        |                      | | ext     |    |        |
 * |        |                      ->| dsp mod | -> |        |
 * |________|                        |_________|    |________|
 *
 * According to above diagram, an ext_dsp_module works by appending to
 * the sink of existing dsp pipeline. For audio input, this creates a
 * multiple output pipeline that stream can read processed buffer from.
 * This is useful for a stream to apply special processing effects while
 * sharing the common dsp with the other streams.
 */
struct ext_dsp_module {
  // A list of ports can connect to existing dsp ports in a pipeline.
  float* ports[MAX_EXT_DSP_PORTS];
  // Processes |nframes| of data.
  void (*run)(struct ext_dsp_module* ext, unsigned int nframes);
  // Configures given external dsp module by the device buffer
  // size, rate, and number of channels of the format of the device that
  // the associated pipeline runs for.
  void (*configure)(struct ext_dsp_module* ext,
                    unsigned int buffer_size,
                    unsigned int num_channels,
                    unsigned int rate);
};

enum { MODULE_INPLACE_BROKEN = 1 };  // See ladspa.h for explanation

// Connects an external dsp module to a builtin sink module.
void cras_dsp_module_set_sink_ext_module(struct dsp_module* module,
                                         struct ext_dsp_module* ext_module);

// Sets the flag of swapping left/right channel to a builtin sink module.
void cras_dsp_module_set_sink_lr_swapped(struct dsp_module* module,
                                         bool left_right_swapped);

struct dsp_module* cras_dsp_module_load_builtin(struct plugin* plugin);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_MODULE_H_
