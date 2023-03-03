// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "extract_member_comments.h"
#include "gtest/gtest.h"

namespace devtools {

TEST(CommentTextTest, Indent) {
  EXPECT_EQ(comment_text("abc", ""), "// abc\n");
  EXPECT_EQ(comment_text("abc", "  "), "  // abc\n");
  EXPECT_EQ(comment_text("abc", "\t"), "\t// abc\n");
  EXPECT_EQ(comment_text("abc\ndef", ""), "// abc\n// def\n");
  EXPECT_EQ(comment_text("abc\ndef", "  "), "  // abc\n  // def\n");
  EXPECT_EQ(comment_text("abc\ndef", "\t"), "\t// abc\n\t// def\n");
  EXPECT_EQ(comment_text("abc\ndef\n", ""), "// abc\n// def\n");
  EXPECT_EQ(comment_text("abc\ndef\n", "  "), "  // abc\n  // def\n");
  EXPECT_EQ(comment_text("abc\ndef\n", "\t"), "\t// abc\n\t// def\n");
}

TEST(SimplifyTest, CollapseToSingleLine) {
  EXPECT_EQ(simplify("/* a */"), "/* a */");
  EXPECT_EQ(simplify("/* a\n */"), "/* a */");
  EXPECT_EQ(simplify("/* a\n *\n */"), "/* a */");
}

TEST(SimplifyTest, DoNotChangeMultiLine) {
  EXPECT_EQ(simplify("/* a\n * b */"), "/* a\n * b */");
  EXPECT_EQ(simplify("/* a\n * b\n */"), "/* a\n * b\n */");
}

TEST(SimplifyTest, CollapseTrailers) {
  EXPECT_EQ(simplify("/* a\n * b\n *\n */"), "/* a\n * b\n */");
}

struct ExtractCommentsTestParam {
  std::string name;                  // Name of the test.
  std::string huge_comment_block;    // Input comment.
  std::vector<std::string> members;  // Struct or enum members.
  std::string expected_replacement;  // Refactored comment.
  std::map<std::string, std::string>
      expected_member_comments;  // Comment for members.
};

class ExtractCommentsTest
    : public testing::TestWithParam<ExtractCommentsTestParam> {};

TEST_P(ExtractCommentsTest, ExtractComments) {
  const ExtractCommentsTestParam& param = GetParam();

  auto [replacement, field_comments_unordered] =
      extract_comments(param.huge_comment_block, param.members);
  std::map<std::string, std::string> field_comments(
      field_comments_unordered.begin(), field_comments_unordered.end());

  EXPECT_EQ(replacement, param.expected_replacement);
  EXPECT_EQ(field_comments, param.expected_member_comments);
}

const ExtractCommentsTestParam params[] = {
    {
        .name = "cras_channel_area",
        .huge_comment_block = R"(/*
 * Descriptor of the memory area holding a channel of audio.
 * Members:
 *    ch_set - Bit set of channels this channel area could map to.
 *    step_bytes - The number of bytes between adjacent samples.
 *    buf - A pointer to the start address of this area.
 */)",
        .expected_replacement = R"(/*
 * Descriptor of the memory area holding a channel of audio.
 */)",
        .expected_member_comments =
            {
                {"ch_set",
                 "Bit set of channels this channel area could map to."},
                {"step_bytes", "The number of bytes between adjacent samples."},
                {"buf", "A pointer to the start address of this area."},
            },
    },
    {
        .name = "cras_audio_area",
        .huge_comment_block = R"(/*
 * Descriptor of the memory area that provides various access to audio channels.
 * Members:
 *    frames - The size of the audio buffer in frames.
 *    num_channels - The number of channels in the audio area.
 *    channels - array of channel areas.
 */)",
        .expected_replacement = R"(/*
 * Descriptor of the memory area that provides various access to audio channels.
 */)",
        .expected_member_comments =
            {
                {"frames", "The size of the audio buffer in frames."},
                {"num_channels", "The number of channels in the audio area."},
                {"channels", "array of channel areas."},
            },
    },
    {
        .name = "cras_bt_profile",
        .huge_comment_block =
            R"(/* Structure in cras to represent an external profile of bluez. All members
 * and functions are documented in bluez/doc/profile-api.txt, more options
 * can be put into this structure when we need it.
 */)",
        .expected_replacement =
            R"(/* Structure in cras to represent an external profile of bluez. All members
 * and functions are documented in bluez/doc/profile-api.txt, more options
 * can be put into this structure when we need it.
 */)",
        .expected_member_comments = {},
    },
    {
        .name = "cras_rclient",
        .huge_comment_block = R"(/* An attached client.
 *  id - The id of the client.
 *  fd - Connection for client communication.
 *  ops - cras_rclient_ops for the cras_rclient.
 *  supported_directions - Bit mask for supported stream directions.
 *  client_type - Client type of this rclient. If this is set to value other
 *                than CRAS_CLIENT_TYPE_UNKNOWN, rclient will overwrite incoming
 *                messages' client type.
 */)",
        .members = {"id", "fd", "ops", "supported_directions", "client_type"},
        .expected_replacement = R"(/* An attached client.
 */)",
        .expected_member_comments =
            {
                {"id", "The id of the client."},
                {"fd", "Connection for client communication."},
                {"ops", "cras_rclient_ops for the cras_rclient."},
                {"supported_directions",
                 "Bit mask for supported stream directions."},
                {"client_type",
                 R"(Client type of this rclient. If this is set to value other
than CRAS_CLIENT_TYPE_UNKNOWN, rclient will overwrite incoming
messages' client type.)"},
            },
    },
    {
        .name = "dev_stream",
        .huge_comment_block = R"(/*
 * Linked list of streams of audio from/to a client.
 * Args:
 *    dev_id - Index of the hw device.
 *    iodev - The iodev |stream| is attaching to.
 *    stream - The rstream attached to a device.
 *    conv - Sample rate or format converter.
 *    conv_buffer - The buffer for converter if needed.
 *    conv_buffer_size_frames - Size of conv_buffer in frames.
 *    dev_rate - Sampling rate of device. This is set when dev_stream is
 *               created.
 *    is_running - For input stream, it should be set to true after it is added
 *                 into device. For output stream, it should be set to true
 *                 just before its first fetch to avoid affecting other existing
 *                 streams.
 */
)",
        .expected_replacement = R"(/*
 * Linked list of streams of audio from/to a client.
 */)",
        .expected_member_comments =
            {
                {"dev_id", "Index of the hw device."},
                {"iodev", "The iodev |stream| is attaching to."},
                {"stream", "The rstream attached to a device."},
                {"conv", "Sample rate or format converter."},
                {"conv_buffer", "The buffer for converter if needed."},
                {"conv_buffer_size_frames", "Size of conv_buffer in frames."},
                {"dev_rate",
                 "Sampling rate of device. This is set when dev_stream "
                 "is\ncreated."},
                {"is_running",
                 "For input stream, it should be set to true after it is "
                 "added\n"
                 "into device. For output stream, it should be set to true\n"
                 "just before its first fetch to avoid affecting other "
                 "existing\n"
                 "streams."},
            },
    },
    {
        .name = "input_data",
        .huge_comment_block = R"(/*
 * Structure holding the information used when a chunk of input buffer
 * is accessed by multiple streams with different properties and
 * processing requirements.
 * Member:
 *    ext - Provides interface to read and process buffer in dsp pipeline.
 *    idev - Pointer to the associated input iodev.
 *    area - The audio area used for deinterleaved data copy.
 *    fbuffer - Floating point buffer from input device.
 */)",
        .expected_replacement = R"(/*
 * Structure holding the information used when a chunk of input buffer
 * is accessed by multiple streams with different properties and
 * processing requirements.
 */)",
        .expected_member_comments =
            {
                {"ext",
                 "Provides interface to read and process buffer in dsp "
                 "pipeline."},
                {"idev", "Pointer to the associated input iodev."},
                {"area", "The audio area used for deinterleaved data copy."},
                {"fbuffer", "Floating point buffer from input device."},
            },
    },
    {
        .name = "suspend_policy",
        .huge_comment_block =
            R"(/*    suspend_reason - The reason code for why suspend is scheduled. */)",
        .members =
            {
                "device",
                "suspend_reason",
                "timer",
                "prev",
                "next",
            },
        .expected_replacement = "",
        .expected_member_comments =
            {
                {"suspend_reason",
                 "The reason code for why suspend is scheduled."},
            },
    },
    {
        .name = "cras_mix_ops",
        .huge_comment_block =
            R"(/* Struct containing ops to implement mix/scale on a buffer of samples.
 * Different architecture can provide different implementations and wraps
 * the implementations into cras_mix_ops.
 * Different sample formats will be handled by different implementations.
 * The usage of each operation is explained in cras_mix.h
 *
 * Members:
 *   scale_buffer_increment: See cras_scale_buffer_increment.
 *   scale_buffer: See cras_scale_buffer.
 *   add: See cras_mix_add.
 *   add_scale_stride: See cras_mix_add_scale_stride.
 *   mute_buffer: cras_mix_mute_buffer.
 */)",
        .expected_replacement =
            R"(/* Struct containing ops to implement mix/scale on a buffer of samples.
 * Different architecture can provide different implementations and wraps
 * the implementations into cras_mix_ops.
 * Different sample formats will be handled by different implementations.
 * The usage of each operation is explained in cras_mix.h
 *
 */)",
        .expected_member_comments =
            {
                {"scale_buffer_increment", "See cras_scale_buffer_increment."},
                {"scale_buffer", "See cras_scale_buffer."},
                {"add", "See cras_mix_add."},
                {"add_scale_stride", "See cras_mix_add_scale_stride."},
                {"mute_buffer", "cras_mix_mute_buffer."},
            },
    },
    {
        .name = "cras_audio_shm_header",
        .huge_comment_block =
            R"(/* Structure containing stream metadata shared between client and server.
 *
 *  config - Size config data.  A copy of the config shared with clients.
 *  read_buf_idx - index of the current buffer to read from (0 or 1 if double
 *    buffered).
 *  write_buf_idx - index of the current buffer to write to (0 or 1 if double
 *    buffered).
 *  read_offset - offset of the next sample to read (one per buffer).
 *  write_offset - offset of the next sample to write (one per buffer).
 *  write_in_progress - non-zero when a write is in progress.
 *  volume_scaler - volume scaling factor (0.0-1.0).
 *  muted - bool, true if stream should be muted.
 *  num_overruns - Starting at 0 this is incremented very time data is over
 *    written because too much accumulated before a read.
 *  ts - For capture, the time stamp of the next sample at read_index.  For
 *    playback, this is the time that the next sample written will be played.
 *    This is only valid in audio callbacks.
 *  buffer_offset - Offset of each buffer from start of samples area.
 *                  Valid range: 0 <= buffer_offset <= shm->samples_info.length
 */)",
        .members =
            {
                "config",
                "read_buf_idx",
                "write_buf_idx",
                "read_offset",
                "write_offset",
                "write_in_progress",
                "volume_scaler",
                "mute",
                "callback_pending",
                "num_overruns",
                "cras_timespec ts",
                "buffer_offset",
            },
        .expected_replacement =
            R"(/* Structure containing stream metadata shared between client and server.
 *
 */)",
        .expected_member_comments =
            {
                {"config",
                 "Size config data.  A copy of the config shared with "
                 "clients."},
                {"read_buf_idx",
                 R"(index of the current buffer to read from (0 or 1 if double
buffered).)"},
                {"write_buf_idx",
                 R"(index of the current buffer to write to (0 or 1 if double
buffered).)"},
                {"read_offset",
                 "offset of the next sample to read (one per buffer)."},
                {"write_offset",
                 "offset of the next sample to write (one per buffer)."},
                {"write_in_progress", "non-zero when a write is in progress."},
                {"volume_scaler", "volume scaling factor (0.0-1.0)."},
                {"muted", "bool, true if stream should be muted."},
                {
                    "num_overruns",
                    R"(Starting at 0 this is incremented very time data is over
written because too much accumulated before a read.)",
                },
                {"ts",
                 R"(For capture, the time stamp of the next sample at read_index.  For
playback, this is the time that the next sample written will be played.
This is only valid in audio callbacks.)"},
                {"buffer_offset",
                 R"(Offset of each buffer from start of samples area.
Valid range: 0 <= buffer_offset <= shm->samples_info.length)"},
            },
    },
};

INSTANTIATE_TEST_SUITE_P(
    Examples,
    ExtractCommentsTest,
    testing::ValuesIn(params),
    [](const testing::TestParamInfo<ExtractCommentsTestParam>& info) {
      return info.param.name;
    });

}  // namespace devtools
