/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "cras_dsp_module.h"


static int instantiate(struct dsp_module *module, unsigned long sample_rate)
{
	return 0;
}

static int instantiate_mix_stereo(struct dsp_module *module,
				  unsigned long sample_rate)
{
	module->data = calloc(4, sizeof(float*));

	return 0;
}

static void connect_port(struct dsp_module *module, unsigned long port,
			 float *data_location) {}

static void connect_port_mix_stereo(struct dsp_module *module,
		unsigned long port, float *data_location)
{
	float **ports;
	ports = (float **)module->data;
	ports[port] = data_location;
}

static void run(struct dsp_module *module, unsigned long sample_count) {}

static void run_mix_stereo(struct dsp_module *module,
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

static void deinstantiate(struct dsp_module *module) {}

static void deinstantiate_mix_stereo(struct dsp_module *module)
{
	free(module->data);
}

static int get_properties(struct dsp_module *module) { return 0; }
static void dump(struct dsp_module *module, struct dumper *d)
{
	dumpf(d, "built-in module\n");
}

static void free_module(struct dsp_module *module)
{
	free(module);
}

struct dsp_module *cras_dsp_module_load_builtin(struct plugin *plugin)
{
	struct dsp_module *module;
	if (strcmp(plugin->library, "builtin") != 0)
		return NULL;

	module = calloc(1, sizeof(struct dsp_module));
	module->instantiate = &instantiate;
	module->connect_port = &connect_port;
	module->run = &run;
	module->deinstantiate = &deinstantiate;
	module->free_module = &free_module;
	module->get_properties = &get_properties;
	module->dump = &dump;

	if (strcmp(plugin->label, "mix_stereo") == 0) {
		module->instantiate = &instantiate_mix_stereo;
		module->run = &run_mix_stereo;
		module->connect_port = &connect_port_mix_stereo;
		module->deinstantiate = &deinstantiate_mix_stereo;
	}

	return module;
}
