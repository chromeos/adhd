// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/server/cras_alsa_common_io.h"
#include "cras/src/server/cras_alsa_config.h"
#include "cras/src/server/cras_dsp.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"

#define FILENAME_TEMPLATE "DspTest.XXXXXX"

#define EQ2_BYPASS_BLOB_SIZE 88
#define STUB_BLOB_SIZE 8
#define STUB_BLOB_DRC_KEY 0xdc
#define STUB_BLOB_EQ2_KEY 0xe2

static int cras_alsa_config_probe_retval;
static bool cras_alsa_config_drc_enabled;
static bool cras_alsa_config_eq2_enabled;
static size_t cras_alsa_config_drc_called;
static size_t cras_alsa_config_eq2_called;
static size_t cras_alsa_config_other_called;
static const char* system_get_dsp_offload_map_str_ret = "Speaker:(1,)";

static void ResetStubData() {
  cras_alsa_config_probe_retval = -1;
  cras_alsa_config_drc_called = 0;
  cras_alsa_config_eq2_called = 0;
  cras_alsa_config_other_called = 0;
}

namespace {

extern "C" {
struct dsp_module* cras_dsp_module_load_ladspa(struct plugin* plugin) {
  return NULL;
}
}

class DspTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    strcpy(filename, FILENAME_TEMPLATE);
    int fd = mkstemp(filename);
    fp = fdopen(fd, "w");
  }

  virtual void TearDown() {
    CloseFile();
    unlink(filename);
  }

  virtual void CloseFile() {
    if (fp) {
      fclose(fp);
      fp = NULL;
    }
  }

  char filename[sizeof(FILENAME_TEMPLATE) + 1];
  FILE* fp;
};

TEST_F(DspTestSuite, Simple) {
  const char* content =
      "[M1]\n"
      "library=builtin\n"
      "label=source\n"
      "purpose=capture\n"
      "output_0={audio}\n"
      "disable=(not (equal? variable \"foo\"))\n"
      "[M2]\n"
      "library=builtin\n"
      "label=sink\n"
      "purpose=capture\n"
      "input_0={audio}\n"
      "\n";
  fprintf(fp, "%s", content);
  CloseFile();

  cras_dsp_init(filename);
  struct cras_dsp_context *ctx1, *ctx3, *ctx4;
  ctx1 = cras_dsp_context_new(44100, "playback");  // wrong purpose
  ctx3 = cras_dsp_context_new(44100, "capture");
  ctx4 = cras_dsp_context_new(44100, "capture");

  cras_dsp_set_variable_string(ctx1, "variable", "foo");
  cras_dsp_set_variable_string(ctx3, "variable", "bar");  // wrong value
  cras_dsp_set_variable_string(ctx4, "variable", "foo");

  cras_dsp_load_pipeline(ctx1);
  cras_dsp_load_pipeline(ctx3);
  cras_dsp_load_pipeline(ctx4);

  // only ctx4 should load the pipeline successfully
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx1));
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx3));

  struct pipeline* pipeline = cras_dsp_get_pipeline(ctx4);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx4);

  // change the variable to a wrong value, and we should fail to reload.
  cras_dsp_set_variable_string(ctx4, "variable", "bar");
  cras_dsp_load_pipeline(ctx4);
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx4));

  // change the variable back, and we should reload successfully.
  cras_dsp_set_variable_string(ctx4, "variable", "foo");
  cras_dsp_reload_ini();
  ASSERT_TRUE(cras_dsp_get_pipeline(ctx4));

  cras_dsp_context_free(ctx1);
  cras_dsp_context_free(ctx3);
  cras_dsp_context_free(ctx4);
  cras_dsp_stop();
}

TEST_F(DspTestSuite, DspOffloadExample) {
  const char* content =
      "[M1]\n"
      "library=builtin\n"
      "label=source\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"drc_eq\"))\n"
      "output_0={a0}\n"
      "output_1={a1}\n"
      "[M2]\n"
      "library=builtin\n"
      "label=drc\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"drc_eq\"))\n"
      "input_0={a0}\n"
      "input_1={a1}\n"
      "output_2={b0}\n"
      "output_3={b1}\n"
      "[M3]\n"
      "library=builtin\n"
      "label=eq2\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"drc_eq\"))\n"
      "input_0={b0}\n"
      "input_1={b1}\n"
      "output_2={c0}\n"
      "output_3={c1}\n"
      "[M4]\n"
      "library=builtin\n"
      "label=sink\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"drc_eq\"))\n"
      "input_0={c0}\n"
      "input_1={c1}\n"
      "\n"
      "[M5]\n"
      "library=builtin\n"
      "label=source\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"eq_drc\"))\n"
      "output_0={d0}\n"
      "output_1={d1}\n"
      "[M6]\n"
      "library=builtin\n"
      "label=eq2\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"eq_drc\"))\n"
      "input_0={d0}\n"
      "input_1={d1}\n"
      "output_2={e0}\n"
      "output_3={e1}\n"
      "[M7]\n"
      "library=builtin\n"
      "label=drc\n"
      "purpose=playback\n"
      "disable=(not (equal? dsp_name \"eq_drc\"))\n"
      "input_0={e0}\n"
      "input_1={e1}\n"
      "output_2={f0}\n"
      "output_3={f1}\n"
      "[M8]\n"
      "library=builtin\n"
      "disable=(not (equal? dsp_name \"eq_drc\"))\n"
      "label=sink\n"
      "purpose=playback\n"
      "input_0={f0}\n"
      "input_1={f1}\n";
  fprintf(fp, "%s", content);
  CloseFile();

  /* In this test example, 3 nodes ae appended on a single playback device,
   * which is linked to the PCM endpoint of DSP DRC-EQ-integrated pipeline (DRC
   * before EQ). The information of 3 nodes is as below:
   * [idx] [type]           [dsp_name] [cras_dsp_pipeline graph] [DSP offload]
   *    0  INTERNAL_SPEAKER  drc_eq    src->drc->eq2->sink       can be applied
   *    1  HEADPHONE         eq_drc    src->eq2->drc->sink       cannot
   *    2  LINEOUT           n/a       n/a                       cannot
   *
   * The expected behavior while setting each node as active:
   * [idx] [cras_dsp_pipeline] [DSP DRC/EQ]
   *    0  should be released  configured offload blobs and enabled
   *    1  should retains      disabled
   *    2  n/a                 disabled
   */

  ResetStubData();
  cras_alsa_config_probe_retval = 0;
  cras_alsa_config_drc_enabled = false;
  cras_alsa_config_eq2_enabled = false;

  cras_dsp_init(filename);

  // Init iodev and ionodes for testing purposes.
  struct cras_iodev dev;
  struct cras_ionode node[3];
  strncpy(node[0].name, INTERNAL_SPEAKER, sizeof(node[0].name) - 1);
  node[0].idx = 0;
  node[0].dsp_name = "drc_eq";
  node[0].dev = &dev;
  strncpy(node[1].name, HEADPHONE, sizeof(node[1].name) - 1);
  node[1].idx = 1;
  node[1].dsp_name = "eq_drc";
  node[1].dev = &dev;
  strncpy(node[2].name, "Line Out", sizeof(node[2].name) - 1);
  node[2].idx = 2;
  node[2].dsp_name = nullptr;
  node[2].dev = &dev;
  dev.active_node = &node[0];

  // dsp_offload_map should be stored and owned by iodev in practice.
  struct dsp_offload_map* map_dev;
  ASSERT_EQ(0, cras_dsp_offload_create_map(&map_dev, &node[0]));
  ASSERT_TRUE(map_dev);
  EXPECT_EQ(DSP_PROC_NOT_STARTED, map_dev->state);

  // 1. open device while active_node is INTERNAL_SPEAKER
  dev.active_node = &node[0];
  // on cras_iodev_alloc_dsp...
  struct cras_dsp_context* ctx;
  ctx = cras_dsp_context_new(48000, "playback");
  // on cras_iodev_update_dsp...
  cras_dsp_set_variable_string(ctx, "dsp_name", node[0].dsp_name);
  cras_dsp_context_set_offload_map(ctx, map_dev);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled; CRAS pipeline will be released.
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx));

  // 2. re-open device
  ResetStubData();
  // on cras_iodev_alloc_dsp...
  cras_dsp_context_free(ctx);
  ctx = cras_dsp_context_new(48000, "playback");
  // on cras_iodev_update_dsp...
  cras_dsp_set_variable_string(ctx, "dsp_name", node[0].dsp_name);
  cras_dsp_context_set_offload_map(ctx, map_dev);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ is already configured and enabled; CRAS pipeline will be
  // released. It is not needed to configure DSP every time CRAS opens the
  // device.
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx));

  // 3. switch active_node to HEADPHONE
  ResetStubData();
  dev.active_node = &node[1];
  // on cras_iodev_update_dsp...
  cras_dsp_set_variable_string(ctx, "dsp_name", node[1].dsp_name);
  cras_dsp_context_set_offload_map(ctx, map_dev);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be disabled; CRAS pipeline will retain.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  struct pipeline* pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 4. switch active_node back to INTERNAL_SPEAKER
  ResetStubData();
  dev.active_node = &node[0];
  // on cras_iodev_update_dsp...
  cras_dsp_set_variable_string(ctx, "dsp_name", node[0].dsp_name);
  cras_dsp_context_set_offload_map(ctx, map_dev);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled; CRAS pipeline will be released.
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx));

  // 5. close device, switch node to LINEOUT and then open device
  ResetStubData();
  dev.active_node = &node[2];
  // on cras_iodev_alloc_dsp...
  cras_dsp_context_free(ctx);
  ctx = cras_dsp_context_new(48000, "playback");
  // on cras_iodev_update_dsp...
  cras_dsp_context_set_offload_map(ctx, map_dev);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be disabled; CRAS pipeline does not exist.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx));

  // 6. alternate the applied dsp as like HEADPHONE, then reload dsp
  ResetStubData();
  cras_dsp_set_variable_string(ctx, "dsp_name", node[1].dsp_name);
  cras_dsp_reload_ini();

  // DSP DRC/EQ will be disabled; CRAS pipeline will retain.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  cras_dsp_context_free(ctx);
  cras_dsp_stop();
  cras_dsp_offload_free_map(map_dev);
}

static int empty_instantiate(struct dsp_module* module,
                             unsigned long sample_rate,
                             struct cras_expr_env* env) {
  return 0;
}

static void empty_connect_port(struct dsp_module* module,
                               unsigned long port,
                               float* data_location) {}

static void empty_configure(struct dsp_module* module) {}

static int empty_get_offload_blob(struct dsp_module* module,
                                  uint32_t** config,
                                  size_t* config_size) {
  return -EINVAL;
}

static int drc_get_offload_blob(struct dsp_module* module,
                                uint32_t** config,
                                size_t* config_size) {
  uint8_t* stub_blob = (uint8_t*)calloc(STUB_BLOB_SIZE, sizeof(uint8_t));
  for (int i = 0; i < STUB_BLOB_SIZE; i++) {
    stub_blob[i] = STUB_BLOB_DRC_KEY;
  }
  *config = (uint32_t*)stub_blob;
  *config_size = STUB_BLOB_SIZE;
  return 0;
}

static int eq2_get_offload_blob(struct dsp_module* module,
                                uint32_t** config,
                                size_t* config_size) {
  uint8_t* stub_blob = (uint8_t*)calloc(STUB_BLOB_SIZE, sizeof(uint8_t));
  for (int i = 0; i < STUB_BLOB_SIZE; i++) {
    stub_blob[i] = STUB_BLOB_EQ2_KEY;
  }
  *config = (uint32_t*)stub_blob;
  *config_size = STUB_BLOB_SIZE;
  return 0;
}

static int empty_get_delay(struct dsp_module* module) {
  return 0;
}

static void empty_run(struct dsp_module* module, unsigned long sample_count) {}

static void empty_deinstantiate(struct dsp_module* module) {}

static void empty_free_module(struct dsp_module* module) {
  free(module);
}

static int empty_get_properties(struct dsp_module* module) {
  return 0;
}

static void empty_dump(struct dsp_module* module, struct dumper* d) {
  dumpf(d, "built-in module\n");
}

static void empty_init_module(struct dsp_module* module) {
  module->instantiate = &empty_instantiate;
  module->connect_port = &empty_connect_port;
  module->configure = &empty_configure;
  module->get_offload_blob = &empty_get_offload_blob;
  module->get_delay = &empty_get_delay;
  module->run = &empty_run;
  module->deinstantiate = &empty_deinstantiate;
  module->free_module = &empty_free_module;
  module->get_properties = &empty_get_properties;
  module->dump = &empty_dump;
}

}  //  namespace

extern "C" {
struct main_thread_event_log* main_log;
struct dsp_module* cras_dsp_module_load_builtin(struct plugin* plugin) {
  struct dsp_module* module;
  module = (struct dsp_module*)calloc(1, sizeof(struct dsp_module));
  empty_init_module(module);

  if (strcmp(plugin->label, "drc") == 0) {
    module->get_offload_blob = &drc_get_offload_blob;
  } else if (strcmp(plugin->label, "eq2") == 0) {
    module->get_offload_blob = &eq2_get_offload_blob;
  }
  return module;
}
void cras_dsp_module_set_sink_ext_module(struct dsp_module* module,
                                         struct ext_dsp_module* ext_module) {}
void cras_dsp_module_set_sink_lr_swapped(struct dsp_module* module,
                                         bool left_right_swapped) {}

int cras_alsa_config_probe(const char* name) {
  return cras_alsa_config_probe_retval;
}

int cras_alsa_config_set_tlv_bytes(const char* name,
                                   const uint8_t* blob,
                                   size_t blob_size) {
  if (blob_size == EQ2_BYPASS_BLOB_SIZE) {
    // The EQ-bypass config blob is set to disable DSP EQ
    cras_alsa_config_eq2_enabled = false;
    return 0;
  }
  if (blob_size != STUB_BLOB_SIZE) {
    return -1;
  }

  if (blob[0] == STUB_BLOB_DRC_KEY) {
    cras_alsa_config_drc_called++;
  } else if (blob[0] == STUB_BLOB_EQ2_KEY) {
    cras_alsa_config_eq2_called++;
    cras_alsa_config_eq2_enabled = true;
  } else {
    cras_alsa_config_other_called++;
  }
  return 0;
}

int cras_alsa_config_set_switch(const char* name, bool enabled) {
  // Only DRC relies on switch control for enabling/disabling.
  cras_alsa_config_drc_enabled = enabled;
  return 0;
}

const char* cras_system_get_dsp_offload_map_str() {
  return system_get_dsp_offload_map_str_ret;
}

int cras_server_metrics_device_dsp_offload_status(
    const struct cras_iodev* iodev,
    enum CRAS_DEVICE_DSP_OFFLOAD_STATUS code) {
  return 0;
}

}  // extern "C"
