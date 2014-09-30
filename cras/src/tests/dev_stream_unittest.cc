// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "audio_thread_log.h"
#include "byte_buffer.h"
#include "cras_audio_area.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "dev_stream.h"
}

namespace {

extern "C" {
struct audio_thread_event_log *atlog;
};

static struct timespec clock_gettime_retspec;

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

struct cras_audio_area_copy_call {
  const struct cras_audio_area *dst;
  unsigned int dst_offset;
  unsigned int dst_format_bytes;
  const struct cras_audio_area *src;
  unsigned int src_index;
};

struct fmt_conv_call {
  struct cras_fmt_conv *conv;
  uint8_t *in_buf;
  uint8_t *out_buf;
  size_t in_frames;
  size_t out_frames;
};

static int config_format_converter_called;
static struct cras_fmt_conv *config_format_converter_conv;
static struct cras_audio_format in_fmt;
static struct cras_audio_format out_fmt;
static struct cras_audio_area_copy_call copy_area_call;
static struct fmt_conv_call conv_frames_call;
static size_t conv_frames_ret;

class CreateSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      in_fmt.format = SND_PCM_FORMAT_S16_LE;
      out_fmt.format = SND_PCM_FORMAT_S16_LE;
      in_fmt.num_channels = 2;
      out_fmt.num_channels = 2;

      SetupShm(&rstream_.shm);

      rstream_.stream_id = 0x10001;
      rstream_.buffer_frames = kBufferFrames;
      rstream_.cb_threshold = kBufferFrames / 2;
      rstream_.is_draining = 0;
      rstream_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
      rstream_.direction = CRAS_STREAM_OUTPUT;
      rstream_.format.format = SND_PCM_FORMAT_S16_LE;
      rstream_.format.num_channels = 2;;

      config_format_converter_called = 0;

      memset(&copy_area_call, 0xff, sizeof(copy_area_call));
      memset(&conv_frames_call, 0xff, sizeof(conv_frames_call));

      atlog = audio_thread_event_log_init();
    }

    virtual void TearDown() {
      free(rstream_.shm.area);
      audio_thread_event_log_deinit(atlog);
    }

    void SetupShm(struct cras_audio_shm *shm) {
      int16_t *buf;

      shm->area = static_cast<struct cras_audio_shm_area *>(
          calloc(1, kBufferFrames * 4 + sizeof(cras_audio_shm_area)));
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(shm,
                             kBufferFrames * cras_shm_frame_bytes(shm));

      buf = (int16_t *)shm->area->samples;
      for (size_t i = 0; i < kBufferFrames * 2; i++)
        buf[i] = i;
      cras_shm_set_mute(shm, 0);
      cras_shm_set_volume_scaler(shm, 1.0);
    }

  int16_t *compare_buffer_;
  struct cras_rstream rstream_;
};

TEST_F(CreateSuite, CaptureNoSRC) {
  struct dev_stream devstr;
  struct cras_audio_area *area;
  struct cras_audio_area *stream_area;
  int16_t cap_buf[kBufferFrames * 2];

  devstr.stream = &rstream_;
  devstr.conv = NULL;
  devstr.conv_buffer = NULL;
  devstr.conv_buffer_size_frames = 0;
  devstr.skip_mix = 0;

  area = (struct cras_audio_area*)calloc(1, sizeof(*area) +
                                               2 * sizeof(*area->channels));
  area->num_channels = 2;
  channel_area_set_channel(&area->channels[0], CRAS_CH_FL);
  channel_area_set_channel(&area->channels[1], CRAS_CH_FR);
  area->channels[0].step_bytes = 4;
  area->channels[0].buf = (uint8_t *)(cap_buf);
  area->channels[1].step_bytes = 4;
  area->channels[1].buf = (uint8_t *)(cap_buf + 1);

  stream_area = (struct cras_audio_area*)calloc(1, sizeof(*area) +
                                                  2 * sizeof(*area->channels));
  stream_area->num_channels = 2;
  rstream_.audio_area = stream_area;
  int16_t *shm_samples = (int16_t *)rstream_.shm.area->samples;
  stream_area->channels[0].step_bytes = 4;
  stream_area->channels[0].buf = (uint8_t *)(shm_samples);
  stream_area->channels[1].step_bytes = 4;
  stream_area->channels[1].buf = (uint8_t *)(shm_samples + 1);

  dev_stream_capture(&devstr, area, 0);

  EXPECT_EQ(stream_area, copy_area_call.dst);
  EXPECT_EQ(0, copy_area_call.dst_offset);
  EXPECT_EQ(4, copy_area_call.dst_format_bytes);
  EXPECT_EQ(area, copy_area_call.src);
  EXPECT_EQ(1, copy_area_call.src_index);

  free(area);
  free(stream_area);
}

TEST_F(CreateSuite, CaptureSRC) {
  struct dev_stream devstr;
  struct cras_audio_area *area;
  struct cras_audio_area *stream_area;
  int16_t cap_buf[kBufferFrames * 2];

  devstr.stream = &rstream_;
  devstr.conv = (struct cras_fmt_conv *)0xdead;
  devstr.conv_buffer =
      (struct byte_buffer *)byte_buffer_create(kBufferFrames * 2 * 4);
  devstr.conv_buffer_size_frames = kBufferFrames * 2;
  devstr.skip_mix = 0;

  area = (struct cras_audio_area*)calloc(1, sizeof(*area) +
                                               2 * sizeof(*area->channels));
  area->num_channels = 2;
  channel_area_set_channel(&area->channels[0], CRAS_CH_FL);
  channel_area_set_channel(&area->channels[1], CRAS_CH_FR);
  area->channels[0].step_bytes = 4;
  area->channels[0].buf = (uint8_t *)(cap_buf);
  area->channels[1].step_bytes = 4;
  area->channels[1].buf = (uint8_t *)(cap_buf + 1);
  area->frames = kBufferFrames;

  stream_area = (struct cras_audio_area*)calloc(1, sizeof(*area) +
                                                  2 * sizeof(*area->channels));
  stream_area->num_channels = 2;
  rstream_.audio_area = stream_area;
  int16_t *shm_samples = (int16_t *)rstream_.shm.area->samples;
  stream_area->channels[0].step_bytes = 4;
  stream_area->channels[0].buf = (uint8_t *)(shm_samples);
  stream_area->channels[1].step_bytes = 4;
  stream_area->channels[1].buf = (uint8_t *)(shm_samples + 1);
  rstream_.audio_area = stream_area;

  devstr.conv_area = (struct cras_audio_area*)calloc(1, sizeof(*area) +
                                                  2 * sizeof(*area->channels));
  devstr.conv_area->num_channels = 2;
  devstr.conv_area->channels[0].step_bytes = 4;
  devstr.conv_area->channels[0].buf = (uint8_t *)(devstr.conv_buffer->bytes);
  devstr.conv_area->channels[1].step_bytes = 4;
  devstr.conv_area->channels[1].buf =
      (uint8_t *)(devstr.conv_buffer->bytes + 1);

  conv_frames_ret = kBufferFrames / 2;

  dev_stream_capture(&devstr, area, 0);

  EXPECT_EQ((struct cras_fmt_conv *)0xdead, conv_frames_call.conv);
  EXPECT_EQ((uint8_t *)cap_buf, conv_frames_call.in_buf);
  EXPECT_EQ(devstr.conv_buffer->bytes, conv_frames_call.out_buf);
  EXPECT_EQ(kBufferFrames, conv_frames_call.in_frames);
  EXPECT_EQ(kBufferFrames * 2, conv_frames_call.out_frames);

  EXPECT_EQ(stream_area, copy_area_call.dst);
  EXPECT_EQ(0, copy_area_call.dst_offset);
  EXPECT_EQ(4, copy_area_call.dst_format_bytes);
  EXPECT_EQ(devstr.conv_area, copy_area_call.src);
  EXPECT_EQ(1, copy_area_call.src_index);
  EXPECT_EQ(conv_frames_ret, devstr.conv_area->frames);

  free(area);
  free(stream_area);
  free(devstr.conv_area);
}

TEST_F(CreateSuite, CreateNoSRCOutput) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  in_fmt.frame_rate = 44100;
  out_fmt.frame_rate = 44100;
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_44_1);
  dev_stream_destroy(dev_stream);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_EQ(NULL, dev_stream->conv_buffer);
  EXPECT_EQ(0, dev_stream->conv_buffer_size_frames);
}

TEST_F(CreateSuite, CreateNoSRCInput) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  rstream_.direction = CRAS_STREAM_INPUT;
  in_fmt.frame_rate = 44100;
  out_fmt.frame_rate = 44100;
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_44_1);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_EQ(NULL, dev_stream->conv_buffer);
  EXPECT_EQ(0, dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(CreateSuite, CreateSRC44to48) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  in_fmt.frame_rate = 44100;
  out_fmt.frame_rate = 48000;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_48);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<byte_buffer*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_fmt.frame_rate, kBufferFrames,
                                out_fmt.frame_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(CreateSuite, CreateSRC44to48Input) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_44_1;
  rstream_.direction = CRAS_STREAM_INPUT;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 44100;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_48);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<byte_buffer*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_fmt.frame_rate, kBufferFrames,
                                out_fmt.frame_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(CreateSuite, CreateSRC48to44) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_48;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 44100;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_44_1);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<byte_buffer*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_fmt.frame_rate, kBufferFrames,
                                out_fmt.frame_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

TEST_F(CreateSuite, CreateSRC48to44Input) {
  struct dev_stream *dev_stream;

  rstream_.format = fmt_s16le_48;
  rstream_.direction = CRAS_STREAM_INPUT;
  in_fmt.frame_rate = 44100;
  out_fmt.frame_rate = 48000;
  config_format_converter_conv = reinterpret_cast<struct cras_fmt_conv*>(0x33);
  dev_stream = dev_stream_create(&rstream_, 0, &fmt_s16le_44_1);
  EXPECT_EQ(1, config_format_converter_called);
  EXPECT_NE(static_cast<byte_buffer*>(NULL), dev_stream->conv_buffer);
  EXPECT_LE(cras_frames_at_rate(in_fmt.frame_rate, kBufferFrames,
                                out_fmt.frame_rate),
            dev_stream->conv_buffer_size_frames);
  dev_stream_destroy(dev_stream);
}

//  Test set_playback_timestamp.
TEST(DevStreamTimimg, SetPlaybackTimeStampSimple) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(DevStreamTimimg, SetPlaybackTimeStampWrap) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(DevStreamTimimg, SetPlaybackTimeStampWrapTwice) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_set_playback_timestamp(48000, 72000, &ts);
  EXPECT_EQ(3, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

//  Test set_capture_timestamp.
TEST(DevStreamTimimg, SetCaptureTimeStampSimple) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(DevStreamTimimg, SetCaptureTimeStampWrap) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(DevStreamTimimg, SetCaptureTimeStampWrapPartial) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 2;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_set_capture_timestamp(48000, 72000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

/* Stubs */
extern "C" {

int cras_rstream_audio_ready(struct cras_rstream *stream, size_t count) {
  return 0;
}

int cras_rstream_request_audio(const struct cras_rstream *stream) {
  return 0;
}

void cras_rstream_update_input_write_pointer(struct cras_rstream *rstream) {
}

void cras_rstream_update_output_read_pointer(struct cras_rstream *rstream) {
}

void cras_rstream_dev_offset_update(struct cras_rstream *rstream,
					unsigned int frames,
					unsigned int dev_id) {
}

void cras_rstream_dev_attach(struct cras_rstream *rstream, unsigned int dev_id)
{
}

void cras_rstream_dev_detach(struct cras_rstream *rstream, unsigned int dev_id)
{
}

unsigned int cras_rstream_dev_offset(const struct cras_rstream *rstream,
                                     unsigned int dev_id) {
  return 0;
}

unsigned int cras_rstream_playable_frames(struct cras_rstream *rstream,
					  unsigned int dev_id) {
  return 0;
}

int config_format_converter(struct cras_fmt_conv **conv,
			    enum CRAS_STREAM_DIRECTION dir,
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
				    unsigned int *in_frames,
				    unsigned int out_frames) {
  conv_frames_call.conv = conv;
  conv_frames_call.in_buf = in_buf;
  conv_frames_call.out_buf = out_buf;
  conv_frames_call.in_frames = *in_frames;
  conv_frames_call.out_frames = out_frames;

  return conv_frames_ret;
}

void cras_mix_add(int16_t *dst, int16_t *src,
		  unsigned int count, unsigned int index,
		  int mute, float mix_vol) {
}

struct cras_audio_area *cras_audio_area_create(int num_channels) {
  return NULL;
}

void cras_audio_area_destroy(struct cras_audio_area *area) {
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
                                         const struct cras_audio_format *fmt,
                                         uint8_t *base_buffer) {
}

void cras_audio_area_config_channels(struct cras_audio_area *area,
				     const struct cras_audio_format *fmt) {
}

void cras_audio_area_copy(const struct cras_audio_area *dst,
                          unsigned int dst_offset,
                          unsigned int dst_format_bytes,
                          const struct cras_audio_area *src,
                          unsigned int src_index) {
  copy_area_call.dst = dst;
  copy_area_call.dst_offset = dst_offset;
  copy_area_call.dst_format_bytes = dst_format_bytes;
  copy_area_call.src = src;
  copy_area_call.src_index = src_index;
}

size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames)
{
  return cras_frames_at_rate(in_fmt.frame_rate,
                             in_frames,
                             out_fmt.frame_rate);
}

size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
                                      size_t out_frames) {
  return cras_frames_at_rate(out_fmt.frame_rate,
                             out_frames,
                             in_fmt.frame_rate);
}

const struct cras_audio_format *cras_fmt_conv_in_format(
    const struct cras_fmt_conv *conv) {
  return &in_fmt;
}

const struct cras_audio_format *cras_fmt_conv_out_format(
    const struct cras_fmt_conv *conv) {
  return &out_fmt;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
