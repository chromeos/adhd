// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "cras/src/server/buffer_share.c"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/input_data.h"
}

namespace {

#define FAKE_CRAS_APM_PTR reinterpret_cast<struct cras_apm*>(0x99)

static struct cras_audio_area apm_area;
static unsigned int cras_stream_apm_process_offset_val;
static unsigned int cras_stream_apm_process_called;
static struct cras_apm* cras_stream_apm_get_active_ret = NULL;
static bool cras_stream_apm_get_use_tuned_settings_val;
static float cras_rstream_get_volume_scaler_val;

TEST(InputData, GetForInputStream) {
  struct cras_iodev* idev = reinterpret_cast<struct cras_iodev*>(0x123);
  struct input_data* data;
  struct cras_rstream stream;
  struct buffer_share* offsets;
  struct cras_audio_area* area;
  struct cras_audio_area dev_area;
  unsigned int offset;

  cras_stream_apm_process_called = 0;
  stream.stream_id = 111;

  data = input_data_create(idev);
  data->ext.configure(&data->ext, 8192, 2, 48000);

  // Prepare offsets data for 2 streams.
  offsets = buffer_share_create(8192);
  buffer_share_add_id(offsets, 111, NULL);
  buffer_share_add_id(offsets, 222, NULL);
  buffer_share_offset_update(offsets, 111, 2048);

  dev_area.frames = 600;
  data->area = &dev_area;

  stream.stream_apm = NULL;
  input_data_get_for_stream(data, &stream, offsets, 1.0f, &area, &offset);

  // Assert offset is clipped by area->frames
  EXPECT_EQ(600, area->frames);
  EXPECT_EQ(600, offset);

#if HAVE_WEBRTC_APM
  EXPECT_EQ(0, cras_stream_apm_process_called);
  cras_stream_apm_get_active_ret = FAKE_CRAS_APM_PTR;
#endif  // HAVE_WEBRTC_APM

  input_data_get_for_stream(data, &stream, offsets, 1.0f, &area, &offset);

#if HAVE_WEBRTC_APM
  // Assert APM process uses correct stream offset not the clipped one
  // used for audio area.
  EXPECT_EQ(1, cras_stream_apm_process_called);
  EXPECT_EQ(2048, cras_stream_apm_process_offset_val);
  EXPECT_EQ(0, offset);
#else
  // Without the APM, the offset shouldn't be changed.
  EXPECT_EQ(600, offset);
#endif  // HAVE_WEBRTC_APM

  input_data_destroy(&data);
  buffer_share_destroy(offsets);
}

TEST(InputData, Gains) {
  struct cras_iodev* idev = reinterpret_cast<struct cras_iodev*>(0x123);
  struct input_data* data = input_data_create(idev);
  struct cras_rstream stream;

  float ui_gain_scalar = 0.5;
  float idev_sw_gain_scaler = 0.6;
  cras_rstream_get_volume_scaler_val = 0.7;

  {
    // No APM. All gains applied in postprocessing.
    cras_stream_apm_get_active_ret = nullptr;
    struct input_data_gain gains = input_data_get_software_gain_scaler(
        data, ui_gain_scalar, idev_sw_gain_scaler, &stream);
    EXPECT_FLOAT_EQ(gains.preprocessing_scalar, 1);
    EXPECT_FLOAT_EQ(gains.postprocessing_scalar, 0.21);
  }

  {
    // APM active. Intrinsic gain applied before APM.
    cras_stream_apm_get_active_ret = FAKE_CRAS_APM_PTR;
    cras_stream_apm_get_use_tuned_settings_val = false;
    struct input_data_gain gains = input_data_get_software_gain_scaler(
        data, ui_gain_scalar, idev_sw_gain_scaler, &stream);
    EXPECT_FLOAT_EQ(gains.preprocessing_scalar, 0.6);
    EXPECT_FLOAT_EQ(gains.postprocessing_scalar, 0.35);
  }

  {
    // Tuned APM. Intrinsic gain and stream gain ignored.
    cras_stream_apm_get_active_ret = FAKE_CRAS_APM_PTR;
    cras_stream_apm_get_use_tuned_settings_val = true;
    struct input_data_gain gains = input_data_get_software_gain_scaler(
        data, ui_gain_scalar, idev_sw_gain_scaler, &stream);
    EXPECT_FLOAT_EQ(gains.preprocessing_scalar, 1);
    EXPECT_FLOAT_EQ(gains.postprocessing_scalar, 0.5);
  }

  input_data_destroy(&data);
}

TEST(InputData, RunWithChannelsExceedingLimit) {
  struct cras_iodev* idev = reinterpret_cast<struct cras_iodev*>(0x123);

  struct input_data* data = input_data_create(idev);

  const int nframes = 8192;
  const int claimed_channels = MAX_EXT_DSP_PORTS * 2;

  data->ext.configure(&data->ext, nframes, claimed_channels, 48000);

  for (int c = 0; c < MAX_EXT_DSP_PORTS; ++c) {
    data->ext.ports[c] = (float*)calloc(nframes, sizeof(float));
    for (int f = 0; f < nframes; ++f) {
      int x = c * nframes + f;
      *(data->ext.ports[c] + f) = *reinterpret_cast<float*>(&x);
    }
  }

  data->ext.run(&data->ext, nframes);

  unsigned int readable = nframes;
  float* const* buff = float_buffer_read_pointer(data->fbuffer, 0, &readable);
  ASSERT_EQ(readable, nframes);

  for (int c = 0; c < MAX_EXT_DSP_PORTS; ++c) {
    ASSERT_EQ(memcmp(data->ext.ports[c], buff[c], nframes), 0);
  }

  for (int c = 0; c < MAX_EXT_DSP_PORTS; ++c) {
    free(data->ext.ports[c]);
  }

  input_data_destroy(&data);
}

extern "C" {
struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  return cras_stream_apm_get_active_ret;
}
int cras_stream_apm_process(struct cras_apm* apm,
                            struct float_buffer* input,
                            unsigned int offset,
                            float preprocessing_gain_scalar) {
  cras_stream_apm_process_called++;
  cras_stream_apm_process_offset_val = offset;
  return 0;
}

struct cras_audio_area* cras_stream_apm_get_processed(struct cras_apm* apm) {
  return &apm_area;
}
void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev) {}
void cras_stream_apm_put_processed(struct cras_apm* apm, unsigned int frames) {}
bool cras_stream_apm_get_use_tuned_settings(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  return cras_stream_apm_get_use_tuned_settings_val;
}

float cras_rstream_get_volume_scaler(struct cras_rstream* rstream) {
  return cras_rstream_get_volume_scaler_val;
}
}  // extern "C"
}  // namespace
