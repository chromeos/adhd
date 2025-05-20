// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cras/common/rust_common.h"
#include "cras/server/s2/s2.h"
#include "cras/src/common/dumper.h"
#include "cras/src/server/cras_dsp_module.h"

struct plugin plugin;
struct dsp_module* module;

namespace {

class DspModBuiltinTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    d = mem_dumper_create();
    cras_s2_reset_for_testing();
    plugin = {};
  }

  virtual void TearDown() {
    mem_dumper_free(d);
    if (module && module->free_module) {
      module->free_module(module);
    }
    module = NULL;
  }

  struct dumper* d;
};

void reload_module() {
  ASSERT_FALSE(cras_s2_is_locked_for_test());

  if (module && module->free_module) {
    module->free_module(module);
  }
  module = cras_dsp_module_load_builtin(&plugin);
}

TEST_F(DspModBuiltinTestSuite, DspCrasProcessorPlugin) {
  plugin = {
      .library = "builtin",
      .label = "speaker_plugin_effect",
  };
  module = cras_dsp_module_load_builtin(&plugin);
  cras_s2_set_reload_output_plugin_processor(reload_module);

  char* buf;
  int size;
  module->dump(module, d);
  mem_dumper_get(d, &buf, &size);
  EXPECT_THAT(buf,
              testing::HasSubstr(cras_processor_effect_to_str(SpeakerPlugin)));

  mem_dumper_consume(d, size);

  // Disable the output plugin processor, the module should be reloaded.
  cras_s2_set_output_plugin_processor_enabled(false);
  module->dump(module, d);
  mem_dumper_get(d, &buf, &size);
  EXPECT_THAT(buf, testing::HasSubstr(cras_processor_effect_to_str(NoEffects)));
  mem_dumper_consume(d, size);

  // Enable the output plugin processor, the module should be reloaded.
  plugin.label = "headphone_plugin_effect";
  cras_s2_set_output_plugin_processor_enabled(true);
  module->dump(module, d);
  mem_dumper_get(d, &buf, &size);
  EXPECT_THAT(
      buf, testing::HasSubstr(cras_processor_effect_to_str(HeadphonePlugin)));
  mem_dumper_consume(d, size);
}

}  //  namespace
