// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "audio_thread.c"
}

#include <gtest/gtest.h>

// Test streams and devices manipulation.
class StreamDeviceSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      SetupDevice(&fallback_output_, CRAS_STREAM_OUTPUT);
      SetupDevice(&fallback_input_, CRAS_STREAM_INPUT);
      thread_ = audio_thread_create(&fallback_output_, &fallback_input_);
    }

    virtual void TearDown() {
      audio_thread_destroy(thread_);
    }

    virtual void SetupDevice(cras_iodev *iodev,
                             enum CRAS_STREAM_DIRECTION direction) {
      memset(iodev, 0, sizeof(*iodev));
      iodev->direction = direction;
      iodev->open_dev = open_dev;
      iodev->close_dev = close_dev;
      iodev->dev_running = dev_running;
      iodev->is_open = is_open;
      iodev->frames_queued = frames_queued;
      iodev->delay_frames = delay_frames;
      iodev->get_buffer = get_buffer;
      iodev->put_buffer = put_buffer;
      iodev->ext_format = &format_;
    }

    void SetupRstream(struct cras_rstream *rstream,
                      enum CRAS_STREAM_DIRECTION direction) {
      memset(rstream, 0, sizeof(*rstream));
      rstream->direction = direction;
    }

    static int open_dev(cras_iodev* iodev) {
      open_dev_called_++;
      return 0;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      return 0;
    }

    static int dev_running(const cras_iodev* iodev) {
      dev_running_called_++;
      return 1;
    }

    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }

    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned int* num) {
      size_t sz = sizeof(*area_) + sizeof(struct cras_channel_area) * 2;

      if (audio_buffer_size_ < *num)
        *num = audio_buffer_size_;

      area_ = (cras_audio_area*)calloc(1, sz);
      area_->frames = *num;
      area_->num_channels = 2;
      area_->channels[0].buf = audio_buffer_;
      channel_area_set_channel(&area_->channels[0], CRAS_CH_FL);
      area_->channels[0].step_bytes = 4;
      area_->channels[1].buf = audio_buffer_ + 2;
      channel_area_set_channel(&area_->channels[1], CRAS_CH_FR);
      area_->channels[1].step_bytes = 4;

      *area = area_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev, unsigned int num) {
      free(area_);
      return 0;
    }

    struct cras_iodev fallback_output_;
    struct cras_iodev fallback_input_;
    struct audio_thread *thread_;

    static int open_dev_called_;
    static int close_dev_called_;
    static int dev_running_called_;
    static int is_open_;
    static int frames_queued_;
    static int delay_frames_;
    static struct cras_audio_format format_;
    static struct cras_audio_area *area_;
    static uint8_t audio_buffer_[8192];
    static unsigned int audio_buffer_size_;
};

int StreamDeviceSuite::open_dev_called_ = 0;
int StreamDeviceSuite::close_dev_called_ = 0;
int StreamDeviceSuite::dev_running_called_ = 0;
int StreamDeviceSuite::is_open_ = 0;
int StreamDeviceSuite::frames_queued_ = 0;
int StreamDeviceSuite::delay_frames_ = 0;
struct cras_audio_format StreamDeviceSuite::format_;
struct cras_audio_area *StreamDeviceSuite::area_;
uint8_t StreamDeviceSuite::audio_buffer_[8192];
unsigned int StreamDeviceSuite::audio_buffer_size_ = 0;

TEST_F(StreamDeviceSuite, AddRemoveActiveOutputDevice) {
  struct cras_iodev iodev;
  struct active_dev *adev;

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);

  // Check fallback device is default active.
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &fallback_output_);
  EXPECT_EQ(fallback_output_.is_active, 1);

  // Check the newly added device is active and fallback device is inactive.
  thread_add_active_dev(thread_, &iodev);
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &iodev);
  EXPECT_EQ(iodev.is_active, 1);
  EXPECT_EQ(fallback_output_.is_active, 0);

  // Check fallback device is active after device removal.
  thread_rm_active_dev(thread_, &iodev);
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &fallback_output_);
  EXPECT_EQ(fallback_output_.is_active, 1);
}

TEST_F(StreamDeviceSuite, AddRemoveActiveInputDevice) {
  struct cras_iodev iodev;
  struct active_dev *adev;

  SetupDevice(&iodev, CRAS_STREAM_INPUT);

  // Check fallback device is default active.
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &fallback_input_);
  EXPECT_EQ(fallback_input_.is_active, 1);

  // Check the newly added device is active and fallback device is inactive.
  thread_add_active_dev(thread_, &iodev);
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &iodev);
  EXPECT_EQ(iodev.is_active, 1);
  EXPECT_EQ(fallback_input_.is_active, 0);

  // Check fallback device is active after device removal.
  thread_rm_active_dev(thread_, &iodev);
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &fallback_input_);
  EXPECT_EQ(fallback_input_.is_active, 1);
}

TEST_F(StreamDeviceSuite, AddRemoveMultipleActiveDevices) {
  struct cras_iodev odev;
  struct cras_iodev odev2;
  struct cras_iodev odev3;
  struct cras_iodev idev;
  struct cras_iodev idev2;
  struct cras_iodev idev3;
  struct active_dev *adev;

  SetupDevice(&odev, CRAS_STREAM_OUTPUT);
  SetupDevice(&odev2, CRAS_STREAM_OUTPUT);
  SetupDevice(&odev3, CRAS_STREAM_OUTPUT);
  SetupDevice(&idev, CRAS_STREAM_INPUT);
  SetupDevice(&idev2, CRAS_STREAM_INPUT);
  SetupDevice(&idev3, CRAS_STREAM_INPUT);

  // Add 2 active devices and check both are active.
  thread_add_active_dev(thread_, &odev);
  thread_add_active_dev(thread_, &odev2);
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev);
  EXPECT_EQ(odev.is_active, 1);
  EXPECT_EQ(adev->next->dev, &odev2);
  EXPECT_EQ(odev2.is_active, 1);

  // Remove first active device and check the second one is still active.
  thread_rm_active_dev(thread_, &odev);
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev2);
  EXPECT_EQ(odev2.is_active, 1);

  // Add another active device and check both are active.
  thread_add_active_dev(thread_, &odev3);
  adev = thread_->active_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev2);
  EXPECT_EQ(odev2.is_active, 1);
  EXPECT_EQ(adev->next->dev, &odev3);
  EXPECT_EQ(odev3.is_active, 1);

  // Add 2 active devices and check both are active.
  thread_add_active_dev(thread_, &idev);
  thread_add_active_dev(thread_, &idev2);
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev);
  EXPECT_EQ(idev.is_active, 1);
  EXPECT_EQ(adev->next->dev, &idev2);
  EXPECT_EQ(idev2.is_active, 1);

  // Remove first active device and check the second one is still active.
  thread_rm_active_dev(thread_, &idev);
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev2);
  EXPECT_EQ(idev2.is_active, 1);

  // Add and remove another active device and check still active.
  thread_add_active_dev(thread_, &idev3);
  thread_rm_active_dev(thread_, &idev3);
  adev = thread_->active_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev2);
  EXPECT_EQ(idev2.is_active, 1);
}

TEST_F(StreamDeviceSuite, AddRemoveMultipleStreamsOnMultipleDevices) {
  struct cras_iodev iodev;
  struct cras_iodev iodev2;
  struct cras_iodev iodev3;
  struct cras_rstream rstream;
  struct cras_rstream rstream2;
  struct cras_rstream rstream3;
  struct dev_stream *dev_stream;

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);
  SetupDevice(&iodev2, CRAS_STREAM_OUTPUT);
  SetupDevice(&iodev3, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream2, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream3, CRAS_STREAM_OUTPUT);

  // Add first device as active and check 2 streams can be added.
  thread_add_active_dev(thread_, &iodev);
  thread_add_stream(thread_, &rstream);
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  thread_add_stream(thread_, &rstream2);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);

  // Add second device as active and check 2 streams are copied over.
  thread_add_active_dev(thread_, &iodev2);
  dev_stream = iodev2.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  // Also check the 2 streams on first device remain intact.
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);

  // Add one more stream and check each active device has 3 streams.
  thread_add_stream(thread_, &rstream3);
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);
  dev_stream = iodev2.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);

  // Remove first device from active and check 3 streams on second device remain
  // intact.
  thread_rm_active_dev(thread_, &iodev);
  dev_stream = iodev2.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);

  // Add third device as active and check 3 streams are copied over.
  thread_add_active_dev(thread_, &iodev3);
  dev_stream = iodev3.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);
  // Also check the 3 streams on second device remain intact.
  dev_stream = iodev2.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);

  // Remove active devices and check 3 streams are on fallback device.
  thread_rm_active_dev(thread_, &iodev2);
  thread_rm_active_dev(thread_, &iodev3);
  dev_stream = fallback_output_.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(dev_stream->next->next->stream, &rstream3);
}

extern "C" {

const char kStreamTimeoutMilliSeconds[] = "Cras.StreamTimeoutMilliSeconds";

int cras_iodev_add_stream(struct cras_iodev *iodev, struct dev_stream *stream)
{
  DL_APPEND(iodev->streams, stream);
  return 0;
}

unsigned int cras_iodev_all_streams_written(struct cras_iodev *iodev)
{
  return 0;
}

int cras_iodev_close(struct cras_iodev *iodev)
{
  return 0;
}

double cras_iodev_get_est_rate_ratio(const struct cras_iodev *iodev)
{
  return 1.0;
}

unsigned int cras_iodev_max_stream_offset(const struct cras_iodev *iodev)
{
  return 0;
}

int cras_iodev_open(struct cras_iodev *iodev)
{
  return 0;
}

int cras_iodev_put_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
  return 0;
}

int cras_iodev_rm_stream(struct cras_iodev *iodev,
                         const struct dev_stream *stream)
{
  DL_DELETE(iodev->streams, stream);
  return 0;
}

int cras_iodev_set_format(struct cras_iodev *iodev,
                          struct cras_audio_format *fmt)
{
  return 0;
}

unsigned int cras_iodev_stream_offset(struct cras_iodev *iodev,
                                      struct dev_stream *stream)
{
  return 0;
}

void cras_iodev_stream_written(struct cras_iodev *iodev,
                               struct dev_stream *stream,
                               unsigned int nwritten)
{
}

int cras_iodev_update_rate(struct cras_iodev *iodev, unsigned int level)
{
  return 0;
}

int cras_iodev_put_input_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
  return 0;
}

int cras_iodev_put_output_buffer(struct cras_iodev *iodev, uint8_t *frames,
				 unsigned int nframes)
{
  return 0;
}

int cras_iodev_get_input_buffer(struct cras_iodev *iodev,
				struct cras_audio_area **area,
				unsigned *frames)
{
  return 0;
}

int cras_iodev_get_output_buffer(struct cras_iodev *iodev,
				 struct cras_audio_area **area,
				 unsigned *frames)
{
  return 0;
}

int cras_iodev_get_dsp_delay(const struct cras_iodev *iodev)
{
  return 0;
}

void cras_metrics_log_histogram(const char *name, int sample, int min,
                                int max, int nbuckets)
{
}

void cras_rstream_destroy(struct cras_rstream *stream)
{
}

int cras_set_rt_scheduling(int rt_lim)
{
  return 0;
}

int cras_set_thread_priority(int priority)
{
  return 0;
}

int cras_system_add_select_fd(int fd,
                              void (*callback)(void *data),
                              void *callback_data)
{
  return 0;
}

void cras_system_rm_select_fd(int fd)
{
}

unsigned int dev_stream_capture(struct dev_stream *dev_stream,
                                const struct cras_audio_area *area,
                                unsigned int area_offset,
                                unsigned int dev_index)
{
  return 0;
}

unsigned int dev_stream_capture_avail(const struct dev_stream *dev_stream)
{
  return 0;
}

int dev_stream_capture_update_rstream(struct dev_stream *dev_stream)
{
  return 0;
}

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
                                     unsigned int dev_id,
                                     const struct cras_audio_format *dev_fmt,
                                     void *dev_ptr)
{
  struct dev_stream *out = static_cast<dev_stream*>(calloc(1, sizeof(*out)));
  out->stream = stream;
  return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
  free(dev_stream);
}

int dev_stream_mix(struct dev_stream *dev_stream,
		   const struct cras_audio_format *fmt,
                   uint8_t *dst,
                   unsigned int num_to_write)
{
  return num_to_write;
}

int dev_stream_playback_frames(const struct dev_stream *dev_stream)
{
  return 0;
}

int dev_stream_playback_update_rstream(struct dev_stream *dev_stream)
{
  return 0;
}

int dev_stream_poll_stream_fd(const struct dev_stream *dev_stream)
{
  return dev_stream->stream->fd;
}

int dev_stream_request_playback_samples(struct dev_stream *dev_stream)
{
  return 0;
}

void dev_stream_set_delay(const struct dev_stream *dev_stream,
                          unsigned int delay_frames)
{
}

void dev_stream_set_dev_rate(struct dev_stream *dev_stream,
                             unsigned int dev_rate,
                             double dev_rate_ratio,
                             double master_rate_ratio,
                             int coarse_rate_adjust)
{
}

}  // extern "C"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
