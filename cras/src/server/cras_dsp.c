/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <semaphore.h>
#include <syslog.h>
#include "dumper.h"
#include "cras_expr.h"
#include "cras_dsp_ini.h"
#include "cras_dsp_pipeline.h"
#include "dsp_util.h"
#include "utlist.h"

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
	struct pipeline *pipeline;

	struct cras_expr_env env;
	int sample_rate;
	const char *purpose;
	struct cras_dsp_context *prev, *next;
};

enum dsp_command {
	DSP_CMD_SET_VARIABLE,
	DSP_CMD_LOAD_PIPELINE,
	DSP_CMD_ADD_CONTEXT,
	DSP_CMD_FREE_CONTEXT,
	DSP_CMD_RELOAD_INI,
	DSP_CMD_DUMP_INFO,
	DSP_CMD_SYNC,
	DSP_CMD_QUIT,
};

struct dsp_request {
	enum dsp_command code;
	struct cras_dsp_context *ctx;
	const char *key;  /* for DSP_CMD_SET_VARIABLE */
	const char *value;  /* for DSP_CMD_SET_VARIABLE */
	sem_t *finished;  /* for DSP_CMD_SYNC */
	struct dsp_request *prev, *next;
};

static struct dumper *syslog_dumper;
static pthread_t dsp_thread;
static const char *ini_filename;
static struct ini *ini;
static struct cras_dsp_context *context_list;

/* The request list can be accessed by multiple threads */
static pthread_mutex_t req_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t req_cond = PTHREAD_COND_INITIALIZER;
static struct dsp_request *req_list;

static void initialize_environment(struct cras_expr_env *env)
{
	cras_expr_env_install_builtins(env);
	cras_expr_env_set_variable_boolean(env, "disable_eq", 0);
	cras_expr_env_set_variable_boolean(env, "disable_drc", 0);
	cras_expr_env_set_variable_string(env, "dsp_name", "");
}

static struct pipeline *prepare_pipeline(struct cras_dsp_context *ctx)
{
	struct pipeline *pipeline;
	const char *purpose = ctx->purpose;

	if (!ini)
		return NULL;

	pipeline = cras_dsp_pipeline_create(ini, &ctx->env, purpose);

	if (pipeline) {
		syslog(LOG_ERR, "pipeline created");
	} else {
		syslog(LOG_ERR, "cannot create pipeline");
		goto bail;
	}

	if (cras_dsp_pipeline_load(pipeline) != 0) {
		syslog(LOG_ERR, "cannot load pipeline");
		goto bail;
	}

	if (cras_dsp_pipeline_instantiate(pipeline, ctx->sample_rate) != 0) {
		syslog(LOG_ERR, "cannot instantiate pipeline");
		goto bail;
	}

	if (cras_dsp_pipeline_get_sample_rate(pipeline) != ctx->sample_rate) {
		syslog(LOG_ERR, "pipeline sample rate mismatch (%d vs %d)",
		       cras_dsp_pipeline_get_sample_rate(pipeline),
		       ctx->sample_rate);
		goto bail;
	}

	return pipeline;

bail:
	if (pipeline)
		cras_dsp_pipeline_free(pipeline);
	return NULL;
}

static void cmd_set_variable(struct cras_dsp_context *ctx, const char *key,
			     const char *value)
{
	cras_expr_env_set_variable_string(&ctx->env, key, value);
}

static void cmd_load_pipeline(struct cras_dsp_context *ctx)
{
	struct pipeline *pipeline, *old_pipeline;

	pipeline = prepare_pipeline(ctx);

	/* This locking is short to avoild blocking audio thread. */
	pthread_mutex_lock(&ctx->mutex);
	old_pipeline = ctx->pipeline;
	ctx->pipeline = pipeline;
	pthread_mutex_unlock(&ctx->mutex);

	if (old_pipeline)
		cras_dsp_pipeline_free(old_pipeline);
}

static void cmd_add_context(struct cras_dsp_context *ctx)
{
	DL_APPEND(context_list, ctx);
}

static void cmd_free_context(struct cras_dsp_context *ctx)
{
	DL_DELETE(context_list, ctx);

	pthread_mutex_destroy(&ctx->mutex);
	if (ctx->pipeline) {
		cras_dsp_pipeline_free(ctx->pipeline);
		ctx->pipeline = NULL;
	}
	cras_expr_env_free(&ctx->env);
	free((char *)ctx->purpose);
	free(ctx);
}

static void cmd_sync(struct dsp_request *req)
{
	sem_post(req->finished);
}

static void cmd_reload_ini()
{
	struct ini *old_ini = ini;
	struct cras_dsp_context *ctx;

	ini = cras_dsp_ini_create(ini_filename);
	if (!ini)
		syslog(LOG_ERR, "cannot create dsp ini");

	DL_FOREACH(context_list, ctx) {
		cmd_load_pipeline(ctx);
	}

	if (old_ini)
		cras_dsp_ini_free(old_ini);
}

static void cmd_dump_info()
{
	struct pipeline *pipeline;
	struct cras_dsp_context *ctx;

	if (ini)
		cras_dsp_ini_dump(syslog_dumper, ini);
	DL_FOREACH(context_list, ctx) {
		cras_expr_env_dump(syslog_dumper, &ctx->env);
		pipeline = ctx->pipeline;
		if (pipeline)
			cras_dsp_pipeline_dump(syslog_dumper, pipeline);
	}
}

static void send_dsp_request(enum dsp_command code,
			     struct cras_dsp_context *ctx,
			     const char *key, const char *value,
			     sem_t *finished)
{
	struct dsp_request *req = calloc(1, sizeof(*req));

	req->code = code;
	req->ctx = ctx;
	req->key = key ? strdup(key) : NULL;
	req->value = value ? strdup(value) : NULL;
	req->finished = finished;

	pthread_mutex_lock(&req_mutex);
	DL_APPEND(req_list, req);
	pthread_cond_signal(&req_cond);
	pthread_mutex_unlock(&req_mutex);
}

static void send_dsp_request_simple(enum dsp_command code,
				    struct cras_dsp_context *ctx)
{
	send_dsp_request(code, ctx, NULL, NULL, NULL);
}

static void *dsp_thread_function(void *arg)
{
	struct dsp_request *req;
	int quit = 0;

	do {
		pthread_mutex_lock(&req_mutex);
		while (req_list == NULL)
			pthread_cond_wait(&req_cond, &req_mutex);
		req = req_list;
		DL_DELETE(req_list, req);
		pthread_mutex_unlock(&req_mutex);

		switch (req->code) {
		case DSP_CMD_SET_VARIABLE:
			cmd_set_variable(req->ctx, req->key, req->value);
			break;
		case DSP_CMD_LOAD_PIPELINE:
			cmd_load_pipeline(req->ctx);
			break;
		case DSP_CMD_ADD_CONTEXT:
			cmd_add_context(req->ctx);
			break;
		case DSP_CMD_FREE_CONTEXT:
			cmd_free_context(req->ctx);
			break;
		case DSP_CMD_RELOAD_INI:
			cmd_reload_ini();
			break;
		case DSP_CMD_SYNC:
			cmd_sync(req);
			break;
		case DSP_CMD_DUMP_INFO:
			cmd_dump_info();
			break;
		case DSP_CMD_QUIT:
			quit = 1;
			break;
		}

		free((char *)req->key);
		free((char *)req->value);
		free(req);
	} while (!quit);
	return NULL;
}

/* Exported functions */

void cras_dsp_init(const char *filename)
{
	dsp_enable_flush_denormal_to_zero();
	ini_filename = strdup(filename);
	syslog_dumper = syslog_dumper_create(LOG_ERR);
	pthread_create(&dsp_thread, NULL, dsp_thread_function, NULL);
	send_dsp_request_simple(DSP_CMD_RELOAD_INI, NULL);
}

void cras_dsp_stop()
{
	send_dsp_request_simple(DSP_CMD_QUIT, NULL);
	pthread_join(dsp_thread, NULL);
	syslog_dumper_free(syslog_dumper);
	free((char *)ini_filename);
	if (ini) {
		cras_dsp_ini_free(ini);
		ini = NULL;
	}
}

struct cras_dsp_context *cras_dsp_context_new(int sample_rate,
					      const char *purpose)
{
	struct cras_dsp_context *ctx = calloc(1, sizeof(*ctx));

	pthread_mutex_init(&ctx->mutex, NULL);
	initialize_environment(&ctx->env);
	ctx->sample_rate = sample_rate;
	ctx->purpose = strdup(purpose);

	send_dsp_request_simple(DSP_CMD_ADD_CONTEXT, ctx);
	return ctx;
}

void cras_dsp_context_free(struct cras_dsp_context *ctx)
{
	send_dsp_request_simple(DSP_CMD_FREE_CONTEXT, ctx);
}

void cras_dsp_set_variable(struct cras_dsp_context *ctx, const char *key,
			   const char *value)
{
	send_dsp_request(DSP_CMD_SET_VARIABLE, ctx, key, value, NULL);
}

void cras_dsp_load_pipeline(struct cras_dsp_context *ctx)
{
	send_dsp_request_simple(DSP_CMD_LOAD_PIPELINE, ctx);
}

struct pipeline *cras_dsp_get_pipeline(struct cras_dsp_context *ctx)
{
	pthread_mutex_lock(&ctx->mutex);
	if (!ctx->pipeline) {
		pthread_mutex_unlock(&ctx->mutex);
		return NULL;
	}
	return ctx->pipeline;
}

void cras_dsp_put_pipeline(struct cras_dsp_context *ctx)
{
	pthread_mutex_unlock(&ctx->mutex);
}

void cras_dsp_reload_ini()
{
	send_dsp_request_simple(DSP_CMD_RELOAD_INI, NULL);
}

void cras_dsp_dump_info()
{
	send_dsp_request_simple(DSP_CMD_DUMP_INFO, NULL);
}

unsigned int cras_dsp_num_output_channels(const struct cras_dsp_context *ctx)
{
	return cras_dsp_pipeline_get_num_output_channels(ctx->pipeline);
}

unsigned int cras_dsp_num_input_channels(const struct cras_dsp_context *ctx)
{
	return cras_dsp_pipeline_get_num_input_channels(ctx->pipeline);
}

void cras_dsp_sync()
{
	sem_t finished;
	sem_init(&finished, 0, 0);
	send_dsp_request(DSP_CMD_SYNC, NULL, NULL, NULL, &finished);
	sem_wait(&finished);
	sem_destroy(&finished);
}
