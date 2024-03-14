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
static bool cras_feature_enabled_dsp_offload;
static struct ext_dsp_module stub_ext_mod;
static size_t stub_running_module_count;
static bool stub_sink_ext_dsp_module_adopted;

static void ResetStubData() {
  cras_alsa_config_probe_retval = -1;
  cras_alsa_config_drc_called = 0;
  cras_alsa_config_eq2_called = 0;
  cras_alsa_config_other_called = 0;
  cras_feature_enabled_dsp_offload = true;
  stub_running_module_count = 0;
  stub_sink_ext_dsp_module_adopted = false;
}

namespace {

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
  const char* content = R"([M1]
library=builtin
label=source
purpose=capture
output_0={audio}
disable=(not (equal? variable "foo"))
[M2]
library=builtin
label=sink
purpose=capture
input_0={audio}
)";
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

static struct cras_dsp_context* test_cras_iodev_alloc_dsp(
    struct dsp_offload_map* map) {
  struct cras_dsp_context* ctx = cras_dsp_context_new(48000, "playback");

  if (cras_feature_enabled_dsp_offload) {
    cras_dsp_offload_clear_disallow_bit(map, DISALLOW_OFFLOAD_BY_FLAG);
  } else {
    cras_dsp_offload_set_disallow_bit(map, DISALLOW_OFFLOAD_BY_FLAG);
  }
  cras_dsp_context_set_offload_map(ctx, map);

  return ctx;
}

static void test_cras_iodev_update_dsp(struct cras_dsp_context* ctx,
                                       struct dsp_offload_map* map,
                                       struct cras_ionode* node) {
  cras_dsp_set_variable_string(ctx, "dsp_name", node->dsp_name);
  cras_dsp_offload_clear_disallow_bit(map, DISALLOW_OFFLOAD_BY_PATTERN);
}

TEST_F(DspTestSuite, DspOffloadNodeSwitch) {
  const char* content = R"([M1]
library=builtin
label=source
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
output_0={a0}
output_1={a1}
[M2]
library=builtin
label=drc
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={a0}
input_1={a1}
output_2={b0}
output_3={b1}
[M3]
library=builtin
label=eq2
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={b0}
input_1={b1}
output_2={c0}
output_3={c1}
[M4]
library=builtin
label=sink
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={c0}
input_1={c1}

[M5]
library=builtin
label=source
purpose=playback
disable=(not (equal? dsp_name "eq_drc"))
output_0={d0}
output_1={d1}
[M6]
library=builtin
label=eq2
purpose=playback
disable=(not (equal? dsp_name "eq_drc"))
input_0={d0}
input_1={d1}
output_2={e0}
output_3={e1}
[M7]
library=builtin
label=drc
purpose=playback
disable=(not (equal? dsp_name "eq_drc"))
input_0={e0}
input_1={e1}
output_2={f0}
output_3={f1}
[M8]
library=builtin
disable=(not (equal? dsp_name "eq_drc"))
label=sink
purpose=playback
input_0={f0}
input_1={f1})";
  fprintf(fp, "%s", content);
  CloseFile();

  /* In this test example, 3 nodes are appended on a single playback device,
   * which is linked to the PCM endpoint of DSP DRC-EQ-integrated pipeline (DRC
   * before EQ). The information of 3 nodes is as below:
   * [idx] [type]           [dsp_name] [cras_dsp_pipeline graph] [DSP offload]
   *    0  INTERNAL_SPEAKER  drc_eq    src->drc->eq2->sink       can be applied
   *    1  HEADPHONE         eq_drc    src->eq2->drc->sink       cannot
   *    2  LINEOUT           n/a       n/a                       cannot
   *
   * The expected behavior while setting each node as active:
   * [idx] [cras_dsp_pipeline] [DSP DRC/EQ]
   *    0  offload_applied=1   configured offload blobs and enabled
   *    1  offload_applied=0   disabled
   *    2  nullptr             disabled
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
  node[2].dsp_name = "";
  node[2].dev = &dev;
  dev.active_node = &node[0];

  // dsp_offload_map should be stored and owned by iodev in practice.
  struct dsp_offload_map* map_dev;
  ASSERT_EQ(0, cras_dsp_offload_create_map(&map_dev, &node[0]));
  ASSERT_TRUE(map_dev);
  EXPECT_EQ(DSP_PROC_NOT_STARTED, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);

  // 1. open device while active_node is INTERNAL_SPEAKER
  dev.active_node = &node[0];
  struct cras_dsp_context* ctx;
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  struct pipeline* pipeline;

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 2. re-open device
  ResetStubData();
  cras_dsp_context_free(ctx);
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ is already configured and enabled, no need to configure again.
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 3. re-open device while toggling CRAS feature flag off
  ResetStubData();
  cras_feature_enabled_dsp_offload = false;
  cras_dsp_context_free(ctx);
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be disabled; offload is disallowed by feature flag.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_FLAG, map_dev->disallow_bits);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 4. re-open device while toggling CRAS feature flag on
  ResetStubData();
  cras_dsp_context_free(ctx);
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 5. switch active_node to HEADPHONE
  ResetStubData();
  dev.active_node = &node[1];
  // simulate update_dsp() call for switching node.
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be disabled; offload is disallowed by unapplicable pattern.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_PATTERN, map_dev->disallow_bits);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 6. switch active_node back to INTERNAL_SPEAKER
  ResetStubData();
  dev.active_node = &node[0];
  // simulate update_dsp() call for switching node.
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node[0].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  // 7. close device, switch node to LINEOUT and then open device
  ResetStubData();
  dev.active_node = &node[2];
  cras_dsp_context_free(ctx);
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be disabled; CRAS pipeline does not exist.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_PATTERN, map_dev->disallow_bits);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  ASSERT_EQ(nullptr, cras_dsp_get_pipeline(ctx));

  // 8. alternate the applied dsp as SPEAKER(node[0]), then reload dsp
  ResetStubData();
  cras_dsp_set_variable_string(ctx, "dsp_name", node[0].dsp_name);
  cras_dsp_reload_ini();

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  // the active node should be still node[2]; only dsp_name is tweaked.
  EXPECT_EQ(node[2].idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_EQ(0, cras_alsa_config_other_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx);

  cras_dsp_context_free(ctx);
  cras_dsp_stop();
  cras_dsp_offload_free_map(map_dev);
}

TEST_F(DspTestSuite, DspOffloadReadaptation) {
  const char* content = R"([M1]
library=builtin
label=source
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
output_0={a0}
output_1={a1}
[M2]
library=builtin
label=drc
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={a0}
input_1={a1}
output_2={b0}
output_3={b1}
[M3]
library=builtin
label=eq2
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={b0}
input_1={b1}
output_2={c0}
output_3={c1}
[M4]
library=builtin
label=sink
purpose=playback
disable=(not (equal? dsp_name "drc_eq"))
input_0={c0}
input_1={c1})";
  fprintf(fp, "%s", content);
  CloseFile();

  /* In this test example, the playback device has one node appended as below:
   * [idx] [type]           [dsp_name] [cras_dsp_pipeline graph] [DSP offload]
   *    0  INTERNAL_SPEAKER  drc_eq    src->drc->eq2->sink       can be applied
   *
   * Here are the summary for information of all 7 steps under testing:
   * [step][odev_state]  [idev_state][finch] [cras_dsp_pipeline]  [DSP offload]
   *     1  open          closed      on      src----------->sink  applied
   *     2  open(ext_mod) closed      on      src----------->sink  applied
   *     3  open(ext_mod) open        on      src->drc->eq2->sink  disallowed
   *     4  open(ext_mod) closed      on      src----------->sink  applied
   *     5  re-opened     closed      off     src->drc->eq2->sink  disallowed
   *     6  open(ext_mod) open        off     src->drc->eq2->sink  disallowed
   *     7  open(ext_mod) closed      off     src->drc->eq2->sink  disallowed
   */

  ResetStubData();
  cras_alsa_config_probe_retval = 0;
  cras_alsa_config_drc_enabled = false;
  cras_alsa_config_eq2_enabled = false;

  cras_dsp_init(filename);

  // Init iodev and ionode for testing purposes.
  struct cras_iodev dev;
  struct cras_ionode node;
  strncpy(node.name, INTERNAL_SPEAKER, sizeof(node.name) - 1);
  node.idx = 0;
  node.dsp_name = "drc_eq";
  node.dev = &dev;
  dev.active_node = &node;

  // dsp_offload_map should be stored and owned by iodev in practice.
  struct dsp_offload_map* map_dev;
  ASSERT_EQ(0, cras_dsp_offload_create_map(&map_dev, &node));
  ASSERT_TRUE(map_dev);
  EXPECT_EQ(DSP_PROC_NOT_STARTED, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);

  // 1. open device and load pipeline
  struct cras_dsp_context* ctx;
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node.idx, map_dev->applied_node_idx);
  EXPECT_EQ(1, cras_alsa_config_drc_called);
  EXPECT_EQ(1, cras_alsa_config_eq2_called);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  // while offloaded, pipeline runs on source and sink modules only
  struct pipeline* pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  ASSERT_EQ(0, cras_dsp_pipeline_run(pipeline, 0 /* sample_count */));
  EXPECT_EQ(1, stub_running_module_count);  // 1(sink)
  cras_dsp_put_pipeline(ctx);

  // 2. set ext_dsp_module to pipeline
  ResetStubData();
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_pipeline_set_sink_ext_module(pipeline, &stub_ext_mod);
  cras_dsp_put_pipeline(ctx);

  // DSP DRC/EQ will be configured and enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_EQ(node.idx, map_dev->applied_node_idx);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  // pipeline is still offloaded, while ext_dsp_module is adopted in sink
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  ASSERT_EQ(0, cras_dsp_pipeline_run(pipeline, 0 /* sample_count */));
  EXPECT_EQ(1, stub_running_module_count);  // 1(sink)
  EXPECT_TRUE(stub_sink_ext_dsp_module_adopted);
  cras_dsp_put_pipeline(ctx);

  // 3. set disallow_bits and readapt pipeline (any input dev is open)
  ResetStubData();
  cras_dsp_offload_set_disallow_bit(map_dev, DISALLOW_OFFLOAD_BY_AEC_REF);
  cras_dsp_readapt_pipeline(ctx);

  // DSP DRC/EQ will be disabled
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_AEC_REF, map_dev->disallow_bits);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  // pipeline runs through all modules
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  ASSERT_EQ(0, cras_dsp_pipeline_run(pipeline, 0 /* sample_count */));
  EXPECT_EQ(4, stub_running_module_count);  // 4(source, drc, eq2, sink)
  EXPECT_TRUE(stub_sink_ext_dsp_module_adopted);
  cras_dsp_put_pipeline(ctx);

  // 4. clear disallow_bits and readapt pipeline (the input dev is closed)
  ResetStubData();
  cras_dsp_offload_clear_disallow_bit(map_dev, DISALLOW_OFFLOAD_BY_AEC_REF);
  cras_dsp_readapt_pipeline(ctx);

  // DSP DRC/EQ will be enabled
  EXPECT_EQ(DSP_PROC_ON_DSP, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_NONE, map_dev->disallow_bits);
  EXPECT_TRUE(cras_alsa_config_drc_enabled);
  EXPECT_TRUE(cras_alsa_config_eq2_enabled);
  // pipeline runs on sink module only
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  ASSERT_EQ(0, cras_dsp_pipeline_run(pipeline, 0 /* sample_count */));
  EXPECT_EQ(1, stub_running_module_count);  // 1(sink)
  EXPECT_TRUE(stub_sink_ext_dsp_module_adopted);
  cras_dsp_put_pipeline(ctx);

  // 5. re-open device while toggling CRAS feature flag off
  ResetStubData();
  cras_feature_enabled_dsp_offload = false;
  cras_dsp_context_free(ctx);
  // simulate alloc_dsp() and update_dsp() calls for opening device.
  ctx = test_cras_iodev_alloc_dsp(map_dev);
  test_cras_iodev_update_dsp(ctx, map_dev, dev.active_node);
  cras_dsp_load_pipeline(ctx);
  // set ext_dsp_module to pipeline
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  cras_dsp_pipeline_set_sink_ext_module(pipeline, &stub_ext_mod);
  cras_dsp_put_pipeline(ctx);

  // DSP DRC/EQ will be disabled; offload is disallowed by feature flag.
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_FLAG, map_dev->disallow_bits);
  EXPECT_EQ(0, cras_alsa_config_drc_called);
  EXPECT_EQ(0, cras_alsa_config_eq2_called);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);
  // pipeline runs through all modules
  pipeline = cras_dsp_get_pipeline(ctx);
  ASSERT_TRUE(pipeline);
  ASSERT_EQ(0, cras_dsp_pipeline_run(pipeline, 0 /* sample_count */));
  EXPECT_EQ(4, stub_running_module_count);  // 4(source, drc, eq2, sink)
  EXPECT_TRUE(stub_sink_ext_dsp_module_adopted);
  cras_dsp_put_pipeline(ctx);

  // 6. set disallow_bits and readapt pipeline (any input dev is open)
  ResetStubData();
  cras_dsp_offload_set_disallow_bit(map_dev, DISALLOW_OFFLOAD_BY_AEC_REF);
  cras_dsp_readapt_pipeline(ctx);

  // DSP DRC/EQ will be disabled
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_AEC_REF | DISALLOW_OFFLOAD_BY_FLAG,
            map_dev->disallow_bits);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);

  // 7. clear disallow_bits and readapt pipeline (the input dev is closed)
  ResetStubData();
  cras_dsp_offload_clear_disallow_bit(map_dev, DISALLOW_OFFLOAD_BY_AEC_REF);
  cras_dsp_readapt_pipeline(ctx);

  // DSP DRC/EQ will be disabled still (due to feature flag)
  EXPECT_EQ(DSP_PROC_ON_CRAS, map_dev->state);
  EXPECT_EQ(DISALLOW_OFFLOAD_BY_FLAG, map_dev->disallow_bits);
  EXPECT_FALSE(cras_alsa_config_drc_enabled);
  EXPECT_FALSE(cras_alsa_config_eq2_enabled);

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

static void stub_run(struct dsp_module* module, unsigned long sample_count) {
  stub_running_module_count++;
}

static void sink_run(struct dsp_module* module, unsigned long sample_count) {
  stub_running_module_count++;
  if (&stub_ext_mod == static_cast<struct ext_dsp_module*>(module->data)) {
    stub_sink_ext_dsp_module_adopted = true;
  }
}

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
  module->run = &stub_run;
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
  } else if (strcmp(plugin->label, "sink") == 0) {
    module->run = &sink_run;
  }
  return module;
}
void cras_dsp_module_set_sink_ext_module(struct dsp_module* module,
                                         struct ext_dsp_module* ext_module) {
  if (!module) {
    return;
  }
  module->data = ext_module;
}
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
