/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include "cras_dsp_module.h"
#include "dsp_util.h"
#include "eq.h"

/*
 *  empty module functions (for source and sink)
 */
static int empty_instantiate(struct dsp_module *module,
			     unsigned long sample_rate)
{
	return 0;
}

static void empty_connect_port(struct dsp_module *module, unsigned long port,
			       float *data_location) {}

static void empty_run(struct dsp_module *module, unsigned long sample_count) {}

static void empty_deinstantiate(struct dsp_module *module) {}

static void empty_free_module(struct dsp_module *module)
{
	free(module);
}

static int empty_get_properties(struct dsp_module *module) { return 0; }

static void empty_dump(struct dsp_module *module, struct dumper *d)
{
	dumpf(d, "built-in module\n");
}

static void empty_init_module(struct dsp_module *module)
{
	module->instantiate = &empty_instantiate;
	module->connect_port = &empty_connect_port;
	module->run = &empty_run;
	module->deinstantiate = &empty_deinstantiate;
	module->free_module = &empty_free_module;
	module->get_properties = &empty_get_properties;
	module->dump = &empty_dump;
}

/*
 *  mix_stereo module functions
 */
static int mix_stereo_instantiate(struct dsp_module *module,
				  unsigned long sample_rate)
{
	module->data = calloc(4, sizeof(float*));
	return 0;
}

static void mix_stereo_connect_port(struct dsp_module *module,
				    unsigned long port, float *data_location)
{
	float **ports;
	ports = (float **)module->data;
	ports[port] = data_location;
}

static void mix_stereo_run(struct dsp_module *module,
			   unsigned long sample_count)
{
	size_t i;
	float tmp;
	float **ports = (float **)module->data;

	for (i = 0; i < sample_count; i++) {
		tmp = ports[0][i] + ports[1][i];
		ports[2][i] = tmp;
		ports[3][i] = tmp;
	}
}

static void mix_stereo_deinstantiate(struct dsp_module *module)
{
	free(module->data);
}

static void mix_stereo_init_module(struct dsp_module *module)
{
	module->instantiate = &mix_stereo_instantiate;
	module->connect_port = &mix_stereo_connect_port;
	module->run = &mix_stereo_run;
	module->deinstantiate = &mix_stereo_deinstantiate;
	module->free_module = &empty_free_module;
	module->get_properties = &empty_get_properties;
	module->dump = &empty_dump;
}

/*
 *  eq module functions
 */
struct eq_data {
	int sample_rate;
	struct eq *eq;  /* Initialized in the first call of eq_run() */

	/* One port for input, one for output, and 4 parameters per eq */
	float *ports[2 + MAX_BIQUADS_PER_EQ * 4];
};

static int eq_instantiate(struct dsp_module *module, unsigned long sample_rate)
{
	struct eq_data *data;

	module->data = calloc(1, sizeof(struct eq_data));
	data = (struct eq_data *) module->data;
	data->sample_rate = (int) sample_rate;
	return 0;
}

static void eq_connect_port(struct dsp_module *module,
			    unsigned long port, float *data_location)
{
	struct eq_data *data = (struct eq_data *) module->data;
	data->ports[port] = data_location;
}

static void eq_run(struct dsp_module *module, unsigned long sample_count)
{
	struct eq_data *data = (struct eq_data *) module->data;
	if (!data->eq) {
		float nyquist = data->sample_rate / 2;
		int i;

		data->eq = eq_new();
		for (i = 2; i < 2 + MAX_BIQUADS_PER_EQ * 4; i += 4) {
			if (!data->ports[i])
				break;
			int type = (int) *data->ports[i];
			float freq = *data->ports[i+1];
			float Q = *data->ports[i+2];
			float gain = *data->ports[i+3];
			eq_append_biquad(data->eq, type, freq / nyquist, Q,
					 gain);
		}
	}
	if (data->ports[0] != data->ports[1])
		memcpy(data->ports[1], data->ports[0],
		       sizeof(float) * sample_count);
	eq_process(data->eq, data->ports[1], (int) sample_count);
}

static void eq_deinstantiate(struct dsp_module *module)
{
	struct eq_data *data = (struct eq_data *) module->data;
	if (data->eq)
		eq_free(data->eq);
	free(data);
}

static void eq_init_module(struct dsp_module *module)
{
	module->instantiate = &eq_instantiate;
	module->connect_port = &eq_connect_port;
	module->run = &eq_run;
	module->deinstantiate = &eq_deinstantiate;
	module->free_module = &empty_free_module;
	module->get_properties = &empty_get_properties;
	module->dump = &empty_dump;
}

/*
 *  builtin module dispatcher
 */
struct dsp_module *cras_dsp_module_load_builtin(struct plugin *plugin)
{
	struct dsp_module *module;
	if (strcmp(plugin->library, "builtin") != 0)
		return NULL;

	module = calloc(1, sizeof(struct dsp_module));

	if (strcmp(plugin->label, "mix_stereo") == 0) {
		mix_stereo_init_module(module);
	} else if (strcmp(plugin->label, "eq") == 0) {
		eq_init_module(module);
	} else {
		empty_init_module(module);
	}

	return module;
}
