/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cras/src/dsp/am.h"
#include "cras/src/server/cras_fmt_conv_ops.h"

#define NUM_FRAMES_PER_RUN (480)

#define CHECK_MSG(expression, message) \
  if (expression) {                    \
    fprintf(stderr, "%s\n", message);  \
    exit(EXIT_FAILURE);                \
  }

#define CHECK_ERROR(expression) CHECK_MSG(expression, strerror(errno))

static void read_int16_mono(const char* input_path,
                            size_t* size,
                            float** data) {
  FILE* input_file = NULL;
  int16_t* input_int16 = NULL;
  float* input_fp32 = NULL;

  input_file = fopen(input_path, "rb");
  CHECK_ERROR(!input_file);

  CHECK_ERROR(fseek(input_file, 0, SEEK_END) != 0)
  const long input_bytes = ftell(input_file);
  CHECK_ERROR(input_bytes == -1)
  CHECK_ERROR(fseek(input_file, 0, SEEK_SET) != 0)
  *size = input_bytes / sizeof(int16_t);

  input_int16 = (int16_t*)malloc(input_bytes);
  CHECK_ERROR(!input_int16);

  size_t num_read = fread(input_int16, 1, input_bytes, input_file);
  CHECK_MSG(num_read < input_bytes, "Read fewer bytes than expected.")

  input_fp32 = (float*)malloc(*size * sizeof(float));
  CHECK_ERROR(!input_fp32);

  convert_s16le_to_f32le(input_int16, *size, input_fp32);
  *data = input_fp32;

  fclose(input_file);
  free(input_int16);
}

static void write_int16_mono(const char* output_path,
                             const float* data,
                             const size_t size) {
  int16_t* output = NULL;
  FILE* output_file = NULL;

  output = (int16_t*)malloc(size * sizeof(int16_t));
  CHECK_ERROR(!output)

  convert_f32le_to_s16le(data, size, output);

  output_file = fopen(output_path, "wb");
  CHECK_ERROR(!output_file)

  size_t num_written = fwrite(output, sizeof(int16_t), size, output_file);
  CHECK_MSG(num_written != size, "Write fewer bytes than expected.");

  fclose(output_file);
  free(output);
}

static void test(const char* model_path,
                 const char* input_path,
                 const char* output_path) {
  struct am_context* am = NULL;
  float* data = NULL;

  am = am_new(model_path);
  CHECK_MSG(!am, "Failed at creating am.");

  size_t data_size = 0;
  read_int16_mono(input_path, &data_size, &data);

  for (size_t i = 0; i + NUM_FRAMES_PER_RUN <= data_size;
       i += NUM_FRAMES_PER_RUN) {
    int rc = am_process(am, data + i, NUM_FRAMES_PER_RUN, data + i,
                        NUM_FRAMES_PER_RUN);
    CHECK_MSG(rc != 0, "Failed at calling am_process.")
  }

  write_int16_mono(output_path, data, data_size);

  am_free(am);
  free(data);
}

int main(int argc, char** argv) {
  if (argc != 4) {
    printf("Usage: %s <model_path> <input_path> <output_path>\n", argv[0]);
    return 1;
  }

  const char* model_path = argv[1];
  const char* input_path = argv[2];
  const char* output_path = argv[3];

  test(model_path, input_path, output_path);
  return 0;
}
