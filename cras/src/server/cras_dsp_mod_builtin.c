/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_dsp_module.h"

static int instantiate(struct dsp_module *module, unsigned long sample_rate)
{
	return 0;
}

static void connect_port(struct dsp_module *module, unsigned long port,
			 float *data_location) {}
static void run(struct dsp_module *module, unsigned long sample_count) {}
static void deinstantiate(struct dsp_module *module) {}
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
	return module;
}
