// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras_dsp.h"

#define FILENAME_TEMPLATE "DspTest.XXXXXX"

namespace {

extern "C" {
struct dsp_module *cras_dsp_module_load_ladspa(struct plugin *plugin)
{
  return NULL;
}
}

class DspTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    strcpy(filename,  FILENAME_TEMPLATE);
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
  FILE *fp;
};

TEST_F(DspTestSuite, Simple) {
  const char *content =
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
  struct cras_dsp_context *ctx1, *ctx2, *ctx3, *ctx4;
  ctx1 = cras_dsp_context_new(1, 44100, "playback");  /* wrong purpose */
  ctx2 = cras_dsp_context_new(2, 44100, "capture");  /* wrong channels */
  ctx3 = cras_dsp_context_new(1, 44100, "capture");
  ctx4 = cras_dsp_context_new(1, 44100, "capture");

  cras_dsp_set_variable(ctx1, "variable", "foo");
  cras_dsp_set_variable(ctx2, "variable", "foo");
  cras_dsp_set_variable(ctx3, "variable", "bar");  /* wrong value */
  cras_dsp_set_variable(ctx4, "variable", "foo");

  cras_dsp_load_pipeline(ctx1);
  cras_dsp_load_pipeline(ctx2);
  cras_dsp_load_pipeline(ctx3);
  cras_dsp_load_pipeline(ctx4);
  cras_dsp_sync();

  /* only ctx4 should load the pipeline successfully */
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx1));
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx2));
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx3));

  struct pipeline *pipeline = cras_dsp_get_pipeline(ctx4);
  ASSERT_TRUE(pipeline);
  cras_dsp_put_pipeline(ctx4);

  /* change the variable to a wrong value, and we should fail to reload. */
  cras_dsp_set_variable(ctx4, "variable", "bar");
  cras_dsp_load_pipeline(ctx4);
  cras_dsp_sync();
  ASSERT_EQ(NULL, cras_dsp_get_pipeline(ctx4));

  /* change the variable back, and we should reload successfully. */
  cras_dsp_set_variable(ctx4, "variable", "foo");
  cras_dsp_reload_ini();
  cras_dsp_sync();
  ASSERT_TRUE(cras_dsp_get_pipeline(ctx4));

  cras_dsp_context_free(ctx1);
  cras_dsp_context_free(ctx2);
  cras_dsp_context_free(ctx3);
  cras_dsp_context_free(ctx4);
  cras_dsp_stop();
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
