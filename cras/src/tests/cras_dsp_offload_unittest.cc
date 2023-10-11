// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

#include "cras/include/cras_types.h"
#include "cras/src/server/cras_alsa_config.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras/src/server/cras_dsp_offload.h"

static std::vector<std::string> alsa_config_probed_mixers;
static struct dsp_module* stub_dsp_module;
static size_t stub_dsp_mod_blob_config_size;
static size_t alsa_config_set_tlv_bytes_size;
static bool alsa_config_set_tlv_bytes_data_equal_to_stub;
static bool alsa_config_set_switch_val;
static size_t alsa_config_set_switch_called;

static int StubDspModGetOffloadBlob(struct dsp_module* mod,
                                    uint32_t** config,
                                    size_t* config_size) {
  *config_size = stub_dsp_mod_blob_config_size;
  if (stub_dsp_mod_blob_config_size == 0) {
    *config = NULL;
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
}

namespace {

class DspOffloadTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    stub_dsp_module = (struct dsp_module*)calloc(1, sizeof(*stub_dsp_module));
    stub_dsp_module->get_offload_blob = StubDspModGetOffloadBlob;
  }

  virtual void TearDown() { free(stub_dsp_module); }
};

TEST_F(DspOffloadTestSuite, ProbeOnInit) {
  cras_dsp_offload_init();
  // Probed the blob and switch control for drc, and the blob control for eq2.
  EXPECT_EQ(3, alsa_config_probed_mixers.size());
}

TEST_F(DspOffloadTestSuite, OffloadDrc) {
  stub_dsp_mod_blob_config_size = 16;

  // Set offload config blob to DRC
  EXPECT_EQ(0, cras_dsp_offload_config_module(stub_dsp_module, "drc"));
  EXPECT_EQ(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_TRUE(alsa_config_set_tlv_bytes_data_equal_to_stub);

  // Set mode to enable offload for DRC
  EXPECT_EQ(0, cras_dsp_offload_set_mode(true, "drc"));
  EXPECT_TRUE(alsa_config_set_switch_val);
  EXPECT_EQ(1, alsa_config_set_switch_called);

  // Set mode to disable offload for DRC
  EXPECT_EQ(0, cras_dsp_offload_set_mode(false, "drc"));
  EXPECT_FALSE(alsa_config_set_switch_val);
  EXPECT_EQ(2, alsa_config_set_switch_called);
}

TEST_F(DspOffloadTestSuite, OffloadEq2) {
  stub_dsp_mod_blob_config_size = 32;

  // Set offload config blob to EQ2
  EXPECT_EQ(0, cras_dsp_offload_config_module(stub_dsp_module, "eq2"));
  EXPECT_EQ(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_TRUE(alsa_config_set_tlv_bytes_data_equal_to_stub);

  // Set mode to enable offload for EQ2
  EXPECT_EQ(0, cras_dsp_offload_set_mode(true, "eq2"));
  // No ops because EQ2 doesn't have the switch control.
  EXPECT_EQ(0, alsa_config_set_switch_called);

  // Set mode to disable offload for EQ2
  EXPECT_EQ(0, cras_dsp_offload_set_mode(false, "eq2"));
  // To disable EQ2, a built-in config blob for bypass mode will be set
  EXPECT_EQ(0, alsa_config_set_switch_called);
  EXPECT_NE(stub_dsp_mod_blob_config_size, alsa_config_set_tlv_bytes_size);
  EXPECT_FALSE(alsa_config_set_tlv_bytes_data_equal_to_stub);
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

}  // extern "C"
}  //  namespace
