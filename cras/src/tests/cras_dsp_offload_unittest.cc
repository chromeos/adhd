// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

#include "cras/src/server/cras_alsa_common_io.h"
#include "cras/src/server/cras_alsa_config.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"

static std::vector<std::string> alsa_config_probed_mixers;
static struct dsp_module* stub_dsp_module;
static size_t stub_dsp_mod_blob_config_size;
static size_t alsa_config_set_tlv_bytes_size;
static bool alsa_config_set_tlv_bytes_data_equal_to_stub;
static bool alsa_config_set_switch_val;
static size_t alsa_config_set_switch_called;
static const char* system_get_dsp_offload_map_str_ret;

static int StubDspModGetOffloadBlob(struct dsp_module* mod,
                                    uint32_t** config,
                                    size_t* config_size) {
  *config_size = stub_dsp_mod_blob_config_size;
  if (stub_dsp_mod_blob_config_size == 0) {
    *config = nullptr;
    return -ENOMEM;
  }

  uint8_t* blob = (uint8_t*)malloc(stub_dsp_mod_blob_config_size);
  for (int i = 0; i < stub_dsp_mod_blob_config_size; i++) {
    blob[i] = i & 0xff;
  }
  *config = (uint32_t*)blob;
  return 0;
}

static void ResetStubData() {
  alsa_config_probed_mixers.clear();
  stub_dsp_mod_blob_config_size = 16;
  alsa_config_set_tlv_bytes_size = 0;
  alsa_config_set_tlv_bytes_data_equal_to_stub = false;
  alsa_config_set_switch_val = false;
  alsa_config_set_switch_called = 0;
  system_get_dsp_offload_map_str_ret = "Speaker:(1,)";
}

namespace {

class DspOffloadTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    strncpy(node.name, INTERNAL_SPEAKER, sizeof(node.name) - 1);
    node.idx = 0;
    node.dev = &dev;
    dev.active_node = &node;

    stub_dsp_module = (struct dsp_module*)calloc(1, sizeof(*stub_dsp_module));
    stub_dsp_module->get_offload_blob = StubDspModGetOffloadBlob;
    cras_dsp_offload_create_map(&offload_map_spk, &node);
  }

  virtual void TearDown() {
    cras_dsp_offload_free_map(offload_map_spk);
    free(stub_dsp_module);
  }

  struct dsp_offload_map* offload_map_spk;
  struct cras_iodev dev;
  struct cras_ionode node;
};

TEST_F(DspOffloadTestSuite, ProbeOnMapCreate) {
  ASSERT_NE(offload_map_spk, nullptr);
  // Probed the blob and switch control for drc, and the blob control for eq2.
  EXPECT_EQ(3, alsa_config_probed_mixers.size());
}

TEST_F(DspOffloadTestSuite, OffloadProcess) {
  ASSERT_NE(offload_map_spk, nullptr);

  stub_dsp_mod_blob_config_size = 16;

  // Set offload config blob to DRC
  EXPECT_EQ(0, cras_dsp_offload_config_module(offload_map_spk, stub_dsp_module,
                                              "drc"));
  EXPECT_EQ(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_TRUE(alsa_config_set_tlv_bytes_data_equal_to_stub);

  stub_dsp_mod_blob_config_size = 32;

  // Set offload config blob to EQ2
  EXPECT_EQ(0, cras_dsp_offload_config_module(offload_map_spk, stub_dsp_module,
                                              "eq2"));
  EXPECT_EQ(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_TRUE(alsa_config_set_tlv_bytes_data_equal_to_stub);

  // Set mode to enable offload for both DRC and EQ2
  EXPECT_EQ(0, cras_dsp_offload_set_state(offload_map_spk, true));
  EXPECT_TRUE(alsa_config_set_switch_val);
  // Only call set_switch once (by DRC) given that there is no switch control
  // for EQ2
  EXPECT_EQ(1, alsa_config_set_switch_called);

  // Set mode to disable offload for both DRC and EQ2
  EXPECT_EQ(0, cras_dsp_offload_set_state(offload_map_spk, false));
  EXPECT_FALSE(alsa_config_set_switch_val);
  EXPECT_EQ(2, alsa_config_set_switch_called);
  // A built-in config blob for bypass mode is set to disable EQ2
  EXPECT_NE(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_FALSE(alsa_config_set_tlv_bytes_data_equal_to_stub);
}

TEST_F(DspOffloadTestSuite, StateTransition) {
  ASSERT_NE(offload_map_spk, nullptr);

  // Check the initial state
  EXPECT_EQ(offload_map_spk->pipeline_id, 1);
  EXPECT_STREQ(offload_map_spk->dsp_pattern, "drc>eq2");
  EXPECT_EQ(offload_map_spk->state, DSP_PROC_NOT_STARTED);

  // Set active node index to 1
  node.idx = 1;
  dev.active_node = &node;
  // Offload is not yet applied
  EXPECT_FALSE(cras_dsp_offload_is_already_applied(offload_map_spk));

  // Set offload state to enabled
  EXPECT_EQ(0, cras_dsp_offload_set_state(offload_map_spk, true));
  // Offload is applied for node_idx=1
  EXPECT_EQ(offload_map_spk->state, DSP_PROC_ON_DSP);
  EXPECT_EQ(offload_map_spk->applied_node_idx, node.idx);
  EXPECT_TRUE(cras_dsp_offload_is_already_applied(offload_map_spk));

  // Set offload state to disabled
  EXPECT_EQ(0, cras_dsp_offload_set_state(offload_map_spk, false));
  // Offload is disabled
  EXPECT_EQ(offload_map_spk->state, DSP_PROC_ON_CRAS);
  EXPECT_FALSE(cras_dsp_offload_is_already_applied(offload_map_spk));

  // Trigger the reset
  cras_dsp_offload_reset_map(offload_map_spk);
  // Reset to the initial state
  EXPECT_EQ(offload_map_spk->state, DSP_PROC_NOT_STARTED);
}

TEST_F(DspOffloadTestSuite, ParseDspOffloadMapFromConfig) {
  const char* test_cfg = "Speaker:(1,) Headphone:(6,eq2>drc) Line Out:(10,eq2)";
  system_get_dsp_offload_map_str_ret = test_cfg;
  struct dsp_offload_map* test_map = nullptr;

  ASSERT_EQ(0, cras_dsp_offload_create_map(&test_map, &node));
  ASSERT_NE(test_map, nullptr);
  EXPECT_EQ(test_map->pipeline_id, 1);
  EXPECT_STREQ(test_map->dsp_pattern, "drc>eq2");
  cras_dsp_offload_free_map(test_map);
  test_map = nullptr;

  strncpy(node.name, HEADPHONE, sizeof(node.name) - 1);
  ASSERT_EQ(0, cras_dsp_offload_create_map(&test_map, &node));
  ASSERT_NE(test_map, nullptr);
  EXPECT_EQ(test_map->pipeline_id, 6);
  EXPECT_STREQ(test_map->dsp_pattern, "eq2>drc");
  cras_dsp_offload_free_map(test_map);
  test_map = nullptr;

  strncpy(node.name, "Line Out", sizeof(node.name) - 1);
  ASSERT_EQ(0, cras_dsp_offload_create_map(&test_map, &node));
  ASSERT_NE(test_map, nullptr);
  EXPECT_EQ(test_map->pipeline_id, 10);
  EXPECT_STREQ(test_map->dsp_pattern, "eq2");
  cras_dsp_offload_free_map(test_map);
  test_map = nullptr;

  // The call with non-specified node name will still get rc = 0,
  // while the input map pointer will be a nullptr.
  strncpy(node.name, HDMI, sizeof(node.name) - 1);
  ASSERT_EQ(0, cras_dsp_offload_create_map(&test_map, &node));
  ASSERT_EQ(test_map, nullptr);
}

extern "C" {

int cras_alsa_config_probe(const char* name) {
  alsa_config_probed_mixers.push_back(std::string(name));
  return 0;
}

int cras_alsa_config_set_tlv_bytes(const char* name,
                                   const uint8_t* blob,
                                   size_t blob_size) {
  alsa_config_set_tlv_bytes_size = blob_size;
  alsa_config_set_tlv_bytes_data_equal_to_stub = true;
  for (size_t i = 0; i < blob_size; i++) {
    if (blob[i] != (i & 0xff)) {
      alsa_config_set_tlv_bytes_data_equal_to_stub = false;
      break;
    }
  }
  return 0;
}

int cras_alsa_config_set_switch(const char* name, bool enabled) {
  alsa_config_set_switch_val = enabled;
  alsa_config_set_switch_called++;
  return 0;
}

const char* cras_system_get_dsp_offload_map_str() {
  return system_get_dsp_offload_map_str_ret;
}

}  // extern "C"
}  //  namespace
