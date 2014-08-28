// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "dev_stream.h"
}

namespace {

static const int kBufferFrames = 1024;
static const struct cras_audio_format fmt_s16le_44_1 = {
	SND_PCM_FORMAT_S16_LE,
	44100,
	2,
};
static const struct cras_audio_format fmt_s16le_48 = {
	SND_PCM_FORMAT_S16_LE,
	48000,
	2,
};

static int config_format_converter_called;
static struct cras_fmt_conv *config_format_converter_conv;
static int in_rate;
static int out_rate;

class OutputTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      int16_t *buf;
      struct cras_audio_shm *shm = &rstream_.output_shm;

      shm->area = static_cast<struct cras_audio_shm_area *>(
          calloc(1, kBufferFrames * 4 + sizeof(cras_audio_shm_area)));
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(shm,
                             kBufferFrames * cras_shm_frame_bytes(shm));

      buf = (int16_t *)shm->area->samples;
      for (size_t i = 0; i < kBufferFrames * 2; i++)
        buf[i] = i;
      shm->area->write_offset[0] = kBufferFrames * 4;
      cras_shm_set_mute(shm, 0);
      cras_shm_set_volume_scaler(shm, 1.0);

      rstream_.stream_id = 0x10001;
      rstream_.buffer_frames = kBufferFrames;
      rstream_.cb_threshold = kBufferFrames / 2;
      rstream_.is_draining = 0;
      rstream_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
      rstream_.direction = CRAS_STREAM_OUTPUT;

      config_format_converter_called = 0;
    }

    virtual void TearDown() {
      free(rstream_.output_shm.area);
    }

  int16_t *compare_buffer_;
  struct cras_rstream rstream_;
};

TEST_F(OutputTestSuite, CreateNoSRC) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  in_rate = 44100;
  out_rate = 44100;
  dev_stream = dev_stream_create(&rstream_, &fmt_s16le_44_1);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_EQ(NULL, dev_stream->conv_buffer);
  EXPECT_EQ(0, dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(OutputTestSuite, CreateSRC44to48) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  in_rate = 44100;
  out_rate = 48000;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, &fmt_s16le_48);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<uint8_t*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_rate, kBufferFrames, out_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(OutputTestSuite, CreateSRC48to44) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_48;
  in_rate = 44100;
  out_rate = 48000;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, &fmt_s16le_44_1);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<uint8_t*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_rate, kBufferFrames, out_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

/* Stubs */
extern "C" {
struct audio_thread_event_log *atlog;

int config_format_converter(struct cras_fmt_conv **conv,
			    const struct cras_audio_format *from,
			    const struct cras_audio_format *to,
			    unsigned int frames) {
  config_format_converter_called++;
  *conv = config_format_converter_conv;
  return 0;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv) {
}

size_t cras_fmt_conv_convert_frames(struct cras_fmt_conv *conv,
				    uint8_t *in_buf,
				    uint8_t *out_buf,
				    size_t in_frames,
				    size_t out_frames) {
  return in_frames;
}

void cras_mix_add(int16_t *dst, int16_t *src,
		  unsigned int count, unsigned int index,
		  int mute, float mix_vol) {
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
                                         const struct cras_audio_format *fmt,
                                         uint8_t *base_buffer) {
}

void cras_audio_area_copy(const struct cras_audio_area *dst,
                          unsigned int dst_offset,
                          unsigned int dst_format_bytes,
                          const struct cras_audio_area *src,
                          unsigned int src_index) {
}

size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames)
{
  return cras_frames_at_rate(in_rate,
                             in_frames,
                             out_rate);
}

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
