/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <tensorflow/lite/c/c_api.h>
#include <tensorflow/lite/c/c_api_types.h>

#include "am.h"

/* An audio model context.
 *  options - the tflite interpreter options.
 *  interpreter - the tflite interpreter.
 *  model - the audio model.
 *  input_tensor - the input tensor of the model.
 */
struct am_context {
	TfLiteInterpreterOptions *options;
	TfLiteInterpreter *interpreter;
	TfLiteModel *model;
	TfLiteTensor *input_tensor;
};

struct am_context *am_new(const char *model_path)
{
	struct am_context *am_context = calloc(1, sizeof(struct am_context));
	if (!am_context) {
		syslog(LOG_ERR, "calloc am_context got NULL.");
		return NULL;
	}

	am_context->model = TfLiteModelCreateFromFile(model_path);
	if (!am_context->model) {
		syslog(LOG_ERR, "TfLiteModelCreateFromFile got NULL.");
		goto fail;
	}

	am_context->options = TfLiteInterpreterOptionsCreate();
	if (!am_context->options) {
		syslog(LOG_ERR, "TfLiteInterpreterOptionsCreate got NULL.");
		goto fail;
	}
	TfLiteInterpreterOptionsSetNumThreads(am_context->options, 1);

	am_context->interpreter =
		TfLiteInterpreterCreate(am_context->model, am_context->options);
	if (!am_context->interpreter) {
		syslog(LOG_ERR, "TfLiteInterpreterCreate got NULL.");
		goto fail;
	}

	TfLiteStatus status =
		TfLiteInterpreterAllocateTensors(am_context->interpreter);
	if (status != kTfLiteOk) {
		syslog(LOG_ERR,
		       "TfLiteInterpreterAllocateTensors returned status is "
		       "not kTfLiteOk.");
		goto fail;
	}

	am_context->input_tensor =
		TfLiteInterpreterGetInputTensor(am_context->interpreter, 0);
	if (!am_context->input_tensor) {
		syslog(LOG_ERR, "TfLiteInterpreterGetInputTensor got NULL.");
		goto fail;
	}

	return am_context;

fail:
	am_free(am_context);
	return NULL;
}

void am_free(struct am_context *am_context)
{
	if (am_context) {
		if (am_context->interpreter)
			TfLiteInterpreterDelete(am_context->interpreter);
		if (am_context->options)
			TfLiteInterpreterOptionsDelete(am_context->options);
		if (am_context->model)
			TfLiteModelDelete(am_context->model);
		free(am_context);
	}
}

static inline const char *get_tflite_error_string(const TfLiteStatus status)
{
	switch (status) {
	case kTfLiteOk:
		return "kTfLiteOk";
	case kTfLiteError:
		return "kTfLiteError";
	case kTfLiteDelegateError:
		return "kTfLiteDelegateError";
	case kTfLiteApplicationError:
		return "kTfLiteApplicationError";
	case kTfLiteDelegateDataNotFound:
		return "kTfLiteDelegateDataNotFound";
	case kTfLiteDelegateDataWriteError:
		return "kTfLiteDelegateDataWriteError";
	case kTfLiteDelegateDataReadError:
		return "kTfLiteDelegateDataReadError";
	case kTfLiteUnresolvedOps:
		return "kTfLiteUnresolvedOps";
	}
}

int am_process(struct am_context *am_context, const float *inputs,
	       const size_t num_inputs, float *outputs,
	       const size_t num_outputs)
{
	TfLiteStatus status;
	status = TfLiteTensorCopyFromBuffer(am_context->input_tensor,
					    (const void *)inputs,
					    num_inputs * sizeof(float));
	if (status != kTfLiteOk) {
		syslog(LOG_ERR, "%s", get_tflite_error_string(status));
		return -ENOENT;
	}

	status = TfLiteInterpreterInvoke(am_context->interpreter);
	if (status != kTfLiteOk) {
		syslog(LOG_ERR, "%s", get_tflite_error_string(status));
		return -ENOENT;
	}

	const TfLiteTensor *output_tensor =
		TfLiteInterpreterGetOutputTensor(am_context->interpreter, 0);
	if (!output_tensor) {
		status = kTfLiteError;
		syslog(LOG_ERR, "%s", get_tflite_error_string(status));
		return -ENOENT;
	}

	status = TfLiteTensorCopyToBuffer(output_tensor, (void *)outputs,
					  num_outputs * sizeof(float));
	if (status != kTfLiteOk) {
		syslog(LOG_ERR, "%s", get_tflite_error_string(status));
		return -ENOENT;
	}

	return 0;
}
