// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>
#include <unistd.h>

extern "C" {
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
}

//  Stub data.
static struct cras_iodev *get_iodev_return;
static int cras_rstream_create_return;
static struct cras_rstream *cras_rstream_create_stream_out;
static int cras_rstream_destroy_called;
static int cras_server_connect_retval;
static int cras_iodev_attach_stream_retval;
static size_t cras_system_set_volume_value;
static int cras_system_set_volume_called;
static size_t cras_system_set_capture_gain_value;
static int cras_system_set_capture_gain_called;
static size_t cras_system_set_mute_value;
static int cras_system_set_mute_called;
static size_t cras_system_set_capture_mute_value;
static int cras_system_set_capture_mute_called;
static cras_system_volume_changed_cb cras_system_register_volume_cb_value;
static void * cras_system_register_volume_cb_arg;
static size_t cras_system_register_volume_cb_called;
static size_t cras_system_remove_volume_cb_called;
static size_t cras_system_get_volume_called;
static size_t cras_system_get_volume_return_value;
static cras_system_volume_changed_cb cras_system_register_mute_cb_value;
static void * cras_system_register_mute_cb_arg;
static size_t cras_system_register_mute_cb_called;
static size_t cras_system_remove_mute_cb_called;
static size_t cras_system_get_mute_called;
static int cras_system_get_mute_return_value;
static size_t cras_make_fd_nonblocking_called;
static cras_system_volume_changed_cb cras_system_register_capture_gain_cb_value;
static void * cras_system_register_capture_gain_cb_arg;
static size_t cras_system_register_capture_gain_cb_called;
static size_t cras_system_remove_capture_gain_cb_called;
static size_t cras_system_get_capture_gain_called;
static size_t cras_system_get_capture_gain_return_value;
static cras_system_volume_changed_cb cras_system_register_capture_mute_cb_value;
static void * cras_system_register_capture_mute_cb_arg;
static size_t cras_system_register_capture_mute_cb_called;
static size_t cras_system_remove_capture_mute_cb_called;
static size_t cras_system_get_capture_mute_called;
static size_t cras_system_get_capture_mute_return_value;
static size_t cras_system_get_min_volume_called;
static size_t cras_system_get_max_volume_called;
static size_t cras_system_register_volume_limits_changed_cb_called;
static size_t cras_system_remove_volume_limits_changed_cb_called;

void ResetStubData() {
  cras_rstream_create_return = 0;
  cras_rstream_create_stream_out = (struct cras_rstream *)NULL;
  cras_server_connect_retval = 0;
  cras_rstream_destroy_called = 0;
  cras_iodev_attach_stream_retval = 0;
  cras_system_set_volume_value = 0;
  cras_system_set_volume_called = 0;
  cras_system_set_capture_gain_value = 0;
  cras_system_set_capture_gain_called = 0;
  cras_system_set_mute_value = 0;
  cras_system_set_mute_called = 0;
  cras_system_set_capture_mute_value = 0;
  cras_system_set_capture_mute_called = 0;
  cras_system_register_volume_cb_called = 0;
  cras_system_remove_volume_cb_called = 0;
  cras_system_get_volume_called = 0;
  cras_system_register_mute_cb_called = 0;
  cras_system_remove_mute_cb_called = 0;
  cras_system_get_mute_called = 0;
  cras_make_fd_nonblocking_called = 0;
  cras_system_register_capture_gain_cb_called = 0;
  cras_system_remove_capture_gain_cb_called = 0;
  cras_system_get_capture_gain_called = 0;
  cras_system_register_capture_mute_cb_called = 0;
  cras_system_remove_capture_mute_cb_called = 0;
  cras_system_get_capture_mute_called = 0;
  cras_system_get_min_volume_called = 0;
  cras_system_get_max_volume_called = 0;
  cras_system_register_volume_limits_changed_cb_called = 0;
  cras_system_remove_volume_limits_changed_cb_called = 0;
}

namespace {

TEST(RClientSuite, CreateSendMessage) {
  struct cras_rclient *rclient;
  int rc;
  struct cras_client_connected msg;
  struct cras_client_volume_status vol_msg;
  int pipe_fds[2];

  ResetStubData();

  rc = pipe(pipe_fds);
  ASSERT_EQ(0, rc);

  rclient = cras_rclient_create(pipe_fds[1], 800);
  ASSERT_NE((void *)NULL, rclient);
  EXPECT_EQ(1, cras_system_register_volume_cb_called);
  EXPECT_EQ(1, cras_system_register_capture_gain_cb_called);
  EXPECT_EQ(1, cras_system_register_capture_mute_cb_called);
  EXPECT_EQ(1, cras_system_register_mute_cb_called);
  EXPECT_EQ(1, cras_system_register_volume_limits_changed_cb_called);
  EXPECT_EQ(1, cras_system_get_min_volume_called);
  EXPECT_EQ(1, cras_system_get_max_volume_called);

  rc = read(pipe_fds[0], &msg, sizeof(msg));
  EXPECT_EQ(sizeof(msg), rc);
  EXPECT_EQ(CRAS_CLIENT_CONNECTED, msg.header.id);
  EXPECT_EQ(CRAS_CLIENT_CONNECTED, msg.header.id);

  rc = read(pipe_fds[0], &vol_msg, sizeof(vol_msg));
  if (rc < 0)
	  return;

  cras_rclient_destroy(rclient);
  EXPECT_EQ(1, cras_system_remove_volume_cb_called);
  EXPECT_EQ(1, cras_system_remove_mute_cb_called);
  EXPECT_EQ(1, cras_system_remove_capture_gain_cb_called);
  EXPECT_EQ(1, cras_system_remove_capture_mute_cb_called);
  EXPECT_EQ(1, cras_system_remove_volume_limits_changed_cb_called);
  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

class RClientMessagesSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      int rc;
      struct cras_client_connected msg;
      struct cras_client_volume_status vol_msg;

      rc = pipe(pipe_fds_);
      if (rc < 0)
        return;
      rclient_ = cras_rclient_create(pipe_fds_[1], 800);
      rc = read(pipe_fds_[0], &msg, sizeof(msg));
      if (rc < 0)
        return;
      rc = read(pipe_fds_[0], &vol_msg, sizeof(vol_msg));
      if (rc < 0)
        return;

      rstream_ = (struct cras_rstream *)calloc(1, sizeof(*rstream_));
      rstream_->shm = &shm_;

      stream_id_ = 0x10002;
      connect_msg_.header.id = CRAS_SERVER_CONNECT_STREAM;
      connect_msg_.header.length = sizeof(connect_msg_);
      connect_msg_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
      connect_msg_.direction = CRAS_STREAM_OUTPUT;
      connect_msg_.stream_id = stream_id_;
      connect_msg_.buffer_frames = 480;
      connect_msg_.cb_threshold = 96;
      connect_msg_.min_cb_level = 240;
      connect_msg_.flags = 0;
      connect_msg_.format.num_channels = 2;
      connect_msg_.format.frame_rate = 48000;
      connect_msg_.format.format = SND_PCM_FORMAT_S16_LE;

      ResetStubData();
    }

    virtual void TearDown() {
      cras_rclient_destroy(rclient_);
      free(rstream_);
      close(pipe_fds_[0]);
      close(pipe_fds_[1]);
    }

    struct cras_connect_message connect_msg_;
    struct cras_rclient *rclient_;
    struct cras_rstream *rstream_;
    struct cras_audio_shm_area shm_;
    size_t stream_id_;
    int pipe_fds_[2];
};

TEST_F(RClientMessagesSuite, NoDevErrorReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  get_iodev_return = (struct cras_iodev *)NULL;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
}

TEST_F(RClientMessagesSuite, RstreamCreateErrorReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  get_iodev_return = (struct cras_iodev *)0xbaba;
  cras_rstream_create_return = -1;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
}

TEST_F(RClientMessagesSuite, AudSockConnectErrorReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  get_iodev_return = (struct cras_iodev *)0xbaba;
  cras_server_connect_retval = -1;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
  EXPECT_EQ(0, cras_rstream_destroy_called);
}

TEST_F(RClientMessagesSuite, IoDevAttachErrorReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  get_iodev_return = (struct cras_iodev *)0xbaba;
  cras_rstream_create_stream_out = rstream_;
  cras_iodev_attach_stream_retval = -1;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header);
  EXPECT_EQ(0, rc);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_NE(0, out_msg.err);
  EXPECT_EQ(1, cras_rstream_destroy_called);
}

TEST_F(RClientMessagesSuite, SuccessReply) {
  struct cras_client_stream_connected out_msg;
  int rc;

  get_iodev_return = (struct cras_iodev *)0xbaba;
  cras_rstream_create_stream_out = rstream_;
  cras_iodev_attach_stream_retval = 0;

  rc = cras_rclient_message_from_client(rclient_, &connect_msg_.header);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_make_fd_nonblocking_called);

  rc = read(pipe_fds_[0], &out_msg, sizeof(out_msg));
  EXPECT_EQ(sizeof(out_msg), rc);
  EXPECT_EQ(stream_id_, out_msg.stream_id);
  EXPECT_EQ(0, out_msg.err);
  EXPECT_EQ(0, cras_rstream_destroy_called);
}

TEST_F(RClientMessagesSuite, SetVolume) {
  struct cras_set_system_volume msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_VOLUME;
  msg.header.length = sizeof(msg);
  msg.volume = 66;

  rc = cras_rclient_message_from_client(rclient_, &msg.header);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_volume_called);
  EXPECT_EQ(66, cras_system_set_volume_value);
}

TEST_F(RClientMessagesSuite, SetCaptureVolume) {
  struct cras_set_system_volume msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN;
  msg.header.length = sizeof(msg);
  msg.volume = 66;

  rc = cras_rclient_message_from_client(rclient_, &msg.header);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_capture_gain_called);
  EXPECT_EQ(66, cras_system_set_capture_gain_value);
}

TEST_F(RClientMessagesSuite, SetMute) {
  struct cras_set_system_mute msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_MUTE;
  msg.header.length = sizeof(msg);
  msg.mute = 1;

  rc = cras_rclient_message_from_client(rclient_, &msg.header);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_mute_called);
  EXPECT_EQ(1, cras_system_set_mute_value);
}

TEST_F(RClientMessagesSuite, SetCaptureMute) {
  struct cras_set_system_mute msg;
  int rc;

  msg.header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE;
  msg.header.length = sizeof(msg);
  msg.mute = 1;

  rc = cras_rclient_message_from_client(rclient_, &msg.header);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_system_set_capture_mute_called);
  EXPECT_EQ(1, cras_system_set_capture_mute_value);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/* stubs */
extern "C" {

const char *cras_config_get_socket_file_dir()
{
  return "/tmp";
}

struct cras_iodev *cras_get_iodev_for_stream_type(uint32_t type,
						  uint32_t direction)
{
  return get_iodev_return;
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
  return 0;
}

int cras_rstream_create(cras_stream_id_t stream_id,
			enum CRAS_STREAM_TYPE stream_type,
			enum CRAS_STREAM_DIRECTION direction,
			const struct cras_audio_format *format,
			size_t buffer_frames,
			size_t cb_threshold,
			size_t min_cb_level,
			uint32_t flags,
			struct cras_rclient *client,
			struct cras_rstream **stream_out)
{
  *stream_out = cras_rstream_create_stream_out;
  return cras_rstream_create_return;
}

int cras_iodev_attach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
  return cras_iodev_attach_stream_retval;
}

void cras_rstream_destroy(struct cras_rstream *stream)
{
  cras_rstream_destroy_called++;
}

int cras_iodev_detach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
  return 0;
}

int cras_iodev_move_stream_type(uint32_t type, uint32_t index)
{
  return 0;
}

int cras_server_connect_to_client_socket(cras_stream_id_t stream_id)
{
  return cras_server_connect_retval;
}
int cras_make_fd_nonblocking(int fd)
{
  cras_make_fd_nonblocking_called++;
  return 0;
}

void cras_system_set_volume(size_t volume)
{
  cras_system_set_volume_value = volume;
  cras_system_set_volume_called++;
}

void cras_system_set_capture_gain(long gain)
{
  cras_system_set_capture_gain_value = gain;
  cras_system_set_capture_gain_called++;
}

//  From system_state.
void cras_system_set_mute(int mute)
{
  cras_system_set_mute_value = mute;
  cras_system_set_mute_called++;
}
void cras_system_set_capture_mute(int mute)
{
  cras_system_set_capture_mute_value = mute;
  cras_system_set_capture_mute_called++;
}
int cras_system_add_alsa_card(size_t alsa_card_index)
{
	return -1;
}

int cras_system_remove_alsa_card(size_t alsa_card_index)
{
	return -1;
}
size_t cras_system_get_volume()
{
  cras_system_get_volume_called++;
  return cras_system_get_volume_return_value;
}
long cras_system_get_capture_gain()
{
  cras_system_get_capture_gain_called++;
  return cras_system_get_capture_gain_return_value;
}
int cras_system_get_capture_mute()
{
  cras_system_get_capture_mute_called++;
  return cras_system_get_capture_mute_return_value;
}
long cras_system_get_min_volume()
{
  cras_system_get_min_volume_called++;
  return 0;
}
long cras_system_get_max_volume()
{
  cras_system_get_max_volume_called++;
  return 0;
}

int cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
                                           void *arg)
{
  cras_system_register_volume_cb_called++;
  cras_system_register_volume_cb_value = cb;
  cras_system_register_volume_cb_arg = arg;
  return 0;
}

int cras_system_remove_volume_changed_cb(cras_system_volume_changed_cb cb,
                                         void *arg)
{
  cras_system_remove_volume_cb_called++;
  return 0;
}

int cras_system_register_volume_limits_changed_cb(
    cras_system_volume_changed_cb cb, void *arg)
{
  cras_system_register_volume_limits_changed_cb_called++;
  return 0;
}

int cras_system_remove_volume_limits_changed_cb(
    cras_system_volume_changed_cb cb, void *arg)
{
  cras_system_remove_volume_limits_changed_cb_called++;
  return 0;
}

int cras_system_register_capture_gain_changed_cb(
		cras_system_volume_changed_cb cb, void *arg)
{
  cras_system_register_capture_gain_cb_called++;
  cras_system_register_capture_gain_cb_value = cb;
  cras_system_register_capture_gain_cb_arg = arg;
  return 0;
}

int cras_system_remove_capture_gain_changed_cb(cras_system_volume_changed_cb cb,
					       void *arg)
{
  cras_system_remove_capture_gain_cb_called++;
  return 0;
}

int cras_system_register_capture_mute_changed_cb(
    cras_system_volume_changed_cb cb, void *arg)
{
  cras_system_register_capture_mute_cb_called++;
  cras_system_register_capture_mute_cb_value = cb;
  cras_system_register_capture_mute_cb_arg = arg;
  return 0;
}

int cras_system_remove_capture_mute_changed_cb(cras_system_volume_changed_cb cb,
                                               void *arg)
{
  cras_system_remove_capture_mute_cb_called++;
  return 0;
}

int cras_system_get_mute()
{
  cras_system_get_mute_called++;
  return cras_system_get_mute_return_value;
}

int cras_system_register_mute_changed_cb(cras_system_volume_changed_cb cb,
					 void *arg)
{
  cras_system_register_mute_cb_called++;
  cras_system_register_mute_cb_value = cb;
  cras_system_register_mute_cb_arg = arg;
  return 0;
}

int cras_system_remove_mute_changed_cb(cras_system_volume_changed_cb cb,
				       void *arg)
{
  cras_system_remove_mute_cb_called++;
  return 0;
}
}  // extern "C"
