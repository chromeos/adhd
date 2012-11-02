// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

#include "cras_dsp_ini.h"

#define FILENAME_TEMPLATE "DspIniTest.XXXXXX"

namespace {

class DspIniTestSuite : public testing::Test {
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

TEST_F(DspIniTestSuite, EmptyIni) {
  struct ini *ini = cras_dsp_ini_create(filename);
  EXPECT_EQ(0, ARRAY_COUNT(&ini->plugins));
  EXPECT_EQ(0, ARRAY_COUNT(&ini->flows));
  cras_dsp_ini_free(ini);
}

TEST_F(DspIniTestSuite, NoLibraryOrLabel) {
  fprintf(fp, "[Test]\n");
  CloseFile();

  struct ini *ini = cras_dsp_ini_create(filename);
  /* NULL because a plugin doesn't have library or label */
  EXPECT_EQ(NULL, ini);
}

TEST_F(DspIniTestSuite, OneSimplePlugin) {
  fprintf(fp, "[Test]\n");
  fprintf(fp, "library=foo.so\n");
  fprintf(fp, "label=bar\n");
  fprintf(fp, "disable=\"#f\"\n");
  CloseFile();

  struct ini *ini = cras_dsp_ini_create(filename);
  EXPECT_EQ(1, ARRAY_COUNT(&ini->plugins));
  EXPECT_EQ(0, ARRAY_COUNT(&ini->flows));

  struct plugin *plugin = ARRAY_ELEMENT(&ini->plugins, 0);
  EXPECT_STREQ("test", plugin->title);
  EXPECT_STREQ("foo.so", plugin->library);
  EXPECT_STREQ("bar", plugin->label);
  EXPECT_TRUE(plugin->disable_expr);
  EXPECT_EQ(0, ARRAY_COUNT(&plugin->ports));

  cras_dsp_ini_free(ini);
}

TEST_F(DspIniTestSuite, BuiltinPlugin) {
  fprintf(fp, "[foo]\n");
  fprintf(fp, "library=builtin\n");
  fprintf(fp, "label=source\n");
  fprintf(fp, "purpose=playback\n");
  fprintf(fp, "[bar]\n");
  fprintf(fp, "library=builtin\n");
  fprintf(fp, "label=sink\n");
  fprintf(fp, "purpose=capture\n");
  CloseFile();

  struct ini *ini = cras_dsp_ini_create(filename);
  EXPECT_EQ(2, ARRAY_COUNT(&ini->plugins));
  EXPECT_EQ(0, ARRAY_COUNT(&ini->flows));
  EXPECT_STREQ(ARRAY_ELEMENT(&ini->plugins, 0)->purpose, "playback");
  EXPECT_STREQ(ARRAY_ELEMENT(&ini->plugins, 1)->purpose, "capture");
  cras_dsp_ini_free(ini);
}

TEST_F(DspIniTestSuite, Ports) {
  fprintf(fp, "[foo]\n");
  fprintf(fp, "library=bar\n");
  fprintf(fp, "label=baz\n");
  fprintf(fp, "input_0=10\n");
  CloseFile();

  struct ini *ini = cras_dsp_ini_create(filename);
  EXPECT_EQ(1, ARRAY_COUNT(&ini->plugins));
  EXPECT_EQ(0, ARRAY_COUNT(&ini->flows));
  struct plugin *plugin = ARRAY_ELEMENT(&ini->plugins, 0);
  EXPECT_EQ(1, ARRAY_COUNT(&plugin->ports));
  struct port *port = ARRAY_ELEMENT(&plugin->ports, 0);
  EXPECT_EQ(PORT_INPUT, port->direction);
  EXPECT_EQ(PORT_CONTROL, port->type);
  EXPECT_EQ(INVALID_FLOW_ID, port->flow_id);
  EXPECT_EQ(10, port->init_value);
  cras_dsp_ini_free(ini);
}

TEST_F(DspIniTestSuite, Flows) {
  fprintf(fp, "[foo]\n");
  fprintf(fp, "library=foo\n");
  fprintf(fp, "label=foo\n");
  fprintf(fp, "output_0=<control>\n");
  fprintf(fp, "output_1={audio}\n");
  fprintf(fp, "[bar]\n");
  fprintf(fp, "library=bar\n");
  fprintf(fp, "label=bar\n");
  fprintf(fp, "input_0={audio}\n");
  fprintf(fp, "input_1=<control>\n");

  CloseFile();

  struct ini *ini = cras_dsp_ini_create(filename);
  EXPECT_EQ(2, ARRAY_COUNT(&ini->plugins));
  struct plugin *foo = ARRAY_ELEMENT(&ini->plugins, 0);
  struct plugin *bar = ARRAY_ELEMENT(&ini->plugins, 1);
  EXPECT_EQ(2, ARRAY_COUNT(&foo->ports));
  EXPECT_EQ(2, ARRAY_COUNT(&bar->ports));

  struct port *foo0 = ARRAY_ELEMENT(&foo->ports, 0);
  struct port *foo1 = ARRAY_ELEMENT(&foo->ports, 1);
  EXPECT_EQ(PORT_OUTPUT, foo0->direction);
  EXPECT_EQ(PORT_CONTROL, foo0->type);
  EXPECT_EQ(PORT_OUTPUT, foo1->direction);
  EXPECT_EQ(PORT_AUDIO, foo1->type);
  EXPECT_EQ(0, foo0->flow_id);
  EXPECT_EQ(1, foo1->flow_id);

  struct port *bar0 = ARRAY_ELEMENT(&bar->ports, 0);
  struct port *bar1 = ARRAY_ELEMENT(&bar->ports, 1);
  EXPECT_EQ(PORT_INPUT, bar0->direction);
  EXPECT_EQ(PORT_AUDIO, bar0->type);
  EXPECT_EQ(PORT_INPUT, bar1->direction);
  EXPECT_EQ(PORT_CONTROL, bar1->type);
  EXPECT_EQ(1, bar0->flow_id);
  EXPECT_EQ(0, bar1->flow_id);

  EXPECT_EQ(2, ARRAY_COUNT(&ini->flows));
  struct flow *flow0 = ARRAY_ELEMENT(&ini->flows, 0);
  struct flow *flow1 = ARRAY_ELEMENT(&ini->flows, 1);

  EXPECT_EQ(PORT_CONTROL, flow0->type);
  EXPECT_STREQ("<control>", flow0->name);

  EXPECT_EQ(PORT_AUDIO, flow1->type);
  EXPECT_STREQ("{audio}", flow1->name);

  EXPECT_EQ(flow0->from, foo);
  EXPECT_EQ(flow0->to, bar);
  EXPECT_EQ(flow0->from_port, 0);
  EXPECT_EQ(flow0->to_port, 1);

  EXPECT_EQ(flow1->from, foo);
  EXPECT_EQ(flow1->to, bar);
  EXPECT_EQ(flow1->from_port, 1);
  EXPECT_EQ(flow1->to_port, 0);

  cras_dsp_ini_free(ini);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
