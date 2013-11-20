// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
// For static function test.
#include "cras_alsa_helpers.c"
}

namespace {

static snd_pcm_chmap_query_t *create_chmap_cap(snd_pcm_chmap_type type,
					       size_t channels)
{
  snd_pcm_chmap_query_t *c;
  c = (snd_pcm_chmap_query_t *)calloc(channels + 2, sizeof(int));
  c->type = type;
  c->map.channels = channels;
  return c;
}

TEST(AlsaHelper, MatchChannelMapCapabilityStereo) {
  snd_pcm_chmap_query_t **caps;
  snd_pcm_chmap_query_t *c;
  struct cras_audio_format *fmt;

  caps = (snd_pcm_chmap_query_t **)calloc(4, sizeof(*caps));

  /* Layout (CRAS_CH_RL, CRAS_CH_RR) corresponds to
   * ALSA channel map (5, 6)
   */
  int8_t channel_layout[CRAS_CH_MAX] =
      {-1, -1, 0, 1, -1, -1, -1, -1, -1, -1, -1};

  fmt = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 44100, 2);
  cras_audio_format_set_channel_layout(fmt, channel_layout);

  /* Create a list of capabilities */
  c = create_chmap_cap(SND_CHMAP_TYPE_FIXED, 3);
  c->map.pos[0] = 3;
  c->map.pos[1] = 4;
  c->map.pos[2] = 5;
  caps[0] = c;

  c = create_chmap_cap(SND_CHMAP_TYPE_VAR, 2);
  c->map.pos[0] = 5;
  c->map.pos[1] = 6;
  caps[1] = c;

  c = create_chmap_cap(SND_CHMAP_TYPE_VAR, 2);
  c->map.pos[0] = 9;
  c->map.pos[1] = 10;
  caps[2] = c;

  caps[3] = NULL;

  /* Test if there's a cap matches fmt */
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_NE((void *)NULL, c);

  caps[1]->map.pos[0] = 5;
  caps[1]->map.pos[1] = 7;

  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_EQ((void *)NULL, c);

  free(caps[0]);
  free(caps[1]);
  free(caps[2]);
  free(caps[3]);
  free(caps);
  cras_audio_format_destroy(fmt);
}

TEST(AlsaHelper, MatchChannelMapCapability51) {
  snd_pcm_chmap_query_t **caps = NULL;
  snd_pcm_chmap_query_t *c = NULL;
  struct cras_audio_format *fmt;

  caps = (snd_pcm_chmap_query_t **)calloc(4, sizeof(*caps));

  /* Layout (CRAS_CH_FL, CRAS_CH_FR, CRAS_CH_RL, CRAS_CH_RR, CRAS_CH_FC)
   * corresponds to ALSA channel map (3, 4, 5, 6, 7)
   */
  int8_t channel_layout[CRAS_CH_MAX] =
      {0, 1, 2, 3, 4, 5, -1, -1, -1, -1, -1};

  fmt = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 44100, 6);
  cras_audio_format_set_channel_layout(fmt, channel_layout);

  /* Create a list of capabilities */
  c = create_chmap_cap(SND_CHMAP_TYPE_FIXED, 6);
  c->map.pos[0] = 3;
  c->map.pos[1] = 4;
  c->map.pos[2] = 5;
  c->map.pos[3] = 6;
  c->map.pos[4] = 7;
  c->map.pos[5] = 8;
  caps[0] = c;

  c = create_chmap_cap(SND_CHMAP_TYPE_VAR, 2);
  c->map.pos[0] = 6;
  c->map.pos[1] = 4;
  caps[1] = c;

  c = create_chmap_cap(SND_CHMAP_TYPE_VAR, 6);
  c->map.pos[0] = 9;
  c->map.pos[1] = 10;
  c->map.pos[2] = 5;
  c->map.pos[3] = 6;
  c->map.pos[4] = 7;
  c->map.pos[5] = 8;
  caps[2] = c;
  caps[3] = NULL;

  /* Test if there's a cap matches fmt */
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_NE((void *)NULL, c);

  caps[0]->map.pos[0] = 7;
  caps[0]->map.pos[1] = 8;
  caps[0]->map.pos[4] = 3;
  caps[0]->map.pos[5] = 4;
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_EQ((void *)NULL, c);

  caps[0]->type = SND_CHMAP_TYPE_PAIRED;
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_NE((void *)NULL, c);

  caps[0]->map.pos[0] = 8;
  caps[0]->map.pos[1] = 7;
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_EQ((void *)NULL, c);

  caps[0]->type = SND_CHMAP_TYPE_VAR;
  c = cras_chmap_caps_match(caps, fmt);
  ASSERT_NE((void *)NULL, c);

  free(caps[0]);
  free(caps[1]);
  free(caps[2]);
  free(caps[3]);
  free(caps);
  cras_audio_format_destroy(fmt);
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
