/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/dsp/am.h"

#include <errno.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>

extern "C" {

// An audio model context.
struct am_context {
  // the audio model.
  std::unique_ptr<tflite::FlatBufferModel> model;
  // the tflite interpreter.
  std::unique_ptr<tflite::Interpreter> interpreter;
};

static inline const char* get_tflite_error_string(const TfLiteStatus status) {
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
  return "Unknown";
}

static inline struct am_context* am_new_failed(struct am_context* am_context) {
  am_free(am_context);
  return NULL;
}

struct am_context* am_new(const char* model_path) {
  struct am_context* am_context =
      static_cast<struct am_context*>(calloc(1, sizeof(struct am_context)));
  if (!am_context) {
    syslog(LOG_ERR, "calloc am_context got NULL.");
    return NULL;
  }

  am_context->model = tflite::FlatBufferModel::BuildFromFile(model_path);
  if (!am_context->model) {
    syslog(LOG_ERR, "BuildFromFile got NULL.");
    return am_new_failed(am_context);
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;
  tflite::InterpreterBuilder builder(*am_context->model, resolver);
  if (const TfLiteStatus status = builder(&am_context->interpreter);
      status != kTfLiteOk) {
    syslog(LOG_ERR, "Built interpreter got not ok status: %s.",
           get_tflite_error_string(status));
    return am_new_failed(am_context);
  }
  am_context->interpreter->SetNumThreads(1);

  if (const TfLiteStatus status = am_context->interpreter->AllocateTensors();
      status != kTfLiteOk) {
    syslog(LOG_ERR, "AllocateTensors got not ok status: %s.",
           get_tflite_error_string(status));
    return am_new_failed(am_context);
  }

  return am_context;
}

void am_free(struct am_context* am_context) {
  if (am_context) {
    am_context->interpreter.reset(nullptr);
    am_context->model.reset(nullptr);
    free(am_context);
  }
}

int am_process(struct am_context* am_context,
               const float* inputs,
               const size_t num_inputs,
               float* outputs,
               const size_t num_outputs) {
  if (const size_t input_bytes =
          am_context->interpreter->input_tensor(0)->bytes;
      input_bytes != sizeof(float) * num_inputs) {
    syslog(LOG_ERR,
           "input tensor bytes must be equal to sizeof(float) * num_inputs, "
           "got %zu != %zu.",
           input_bytes, sizeof(float) * num_inputs);
    return -ENOENT;
  }

  float* input_buf = am_context->interpreter->typed_input_tensor<float>(0);
  if (!input_buf) {
    syslog(LOG_ERR, "typed_input_tensor got null.");
    return -ENOENT;
  }
  memcpy(input_buf, inputs, sizeof(float) * num_inputs);

  if (const TfLiteStatus status = am_context->interpreter->Invoke();
      status != kTfLiteOk) {
    syslog(LOG_ERR, "%s", get_tflite_error_string(status));
    return -ENOENT;
  }

  if (const size_t output_bytes =
          am_context->interpreter->output_tensor(0)->bytes;
      output_bytes != sizeof(float) * num_outputs) {
    syslog(LOG_ERR,
           "output tensor bytes must be equal to sizeof(float) * num_outputs, "
           "got %zu != %zu.",
           output_bytes, sizeof(float) * num_outputs);
    return -ENOENT;
  }

  const float* output_buf =
      am_context->interpreter->typed_output_tensor<float>(0);
  if (!output_buf) {
    syslog(LOG_ERR, "typed_output_tensor got null.");
    return -ENOENT;
  }
  memcpy(outputs, output_buf, sizeof(float) * num_outputs);

  return 0;
}

}  // extern "C"
