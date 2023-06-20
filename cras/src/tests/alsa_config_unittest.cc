/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <vector>

#include "cras/include/cras_types.h"
#include "cras/src/common/blob_wrapper.h"
#include "cras/src/server/cras_alsa_config.h"

#define CARD_0_NAME "hw:0"
#define CARD_1_NAME "hw:1"
#define CARD_8_NAME "hw:8"

#define MAX_ALSA_CARDS 32
#define MAX_CARD_NAME_LEN 6
#define MAX_CTL_NAME_LEN 30
#define MAX_ELEM_CTL_NAME_LEN 100
#define MAX_CONFIG_BYTE_LEN 80

#define SOF_ABI_HEADER_SIZE 32
#define SOF_ABI_HEADER_SAMPLE(size)                                         \
  0x21, 0x43, 0x65, 0x87, 0x01, 0x00, 0x00, 0x00, (size), 0x00, 0x00, 0x00, \
      0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

struct MockConfigControl {
  char card_name[MAX_CARD_NAME_LEN];
  char control_name[MAX_CTL_NAME_LEN];
  snd_ctl_elem_type_t type;
  bool is_readable;
  bool is_writable;

  // Configurable for switch-typed control only.
  bool state;

  // Configurable for bytes-typed control only.
  unsigned int max_bytes;
  unsigned int config_bytes;
  uint8_t config_data[MAX_CONFIG_BYTE_LEN];
};

static struct MockConfigControl card0_switch = {
    .card_name = CARD_0_NAME,
    .control_name = "SWITCH0.1",
    .type = SND_CTL_ELEM_TYPE_BOOLEAN,
    .is_readable = 1,
    .is_writable = 1,
};

static struct MockConfigControl card0_bytes = {
    .card_name = CARD_0_NAME,
    .control_name = "BYTES0.2",
    .type = SND_CTL_ELEM_TYPE_BYTES,
    .is_readable = 1,
    .is_writable = 1,
    .max_bytes = 48,
    .config_bytes = 16,
    .config_data = {SOF_ABI_HEADER_SAMPLE(16), 0x01, 0x02, 0x03, 0x04, 0x05,
                    0x06, 0x07, 0x08, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0,
                    0xf0},
};

static struct MockConfigControl card1_switch = {
    .card_name = CARD_1_NAME,
    .control_name = "SWITCH1.3",
    .type = SND_CTL_ELEM_TYPE_BOOLEAN,
    .is_readable = 1,
    .is_writable = 1,
};

static struct MockConfigControl card1_bytes = {
    .card_name = CARD_1_NAME,
    .control_name = "BYTES1.4",
    .type = SND_CTL_ELEM_TYPE_BYTES,
    .is_readable = 1,
    .is_writable = 1,
    .max_bytes = 40,
    .config_bytes = 8,
    .config_data =
        {
            SOF_ABI_HEADER_SAMPLE(8),
        },
};

static struct MockConfigControl card1_bytes_ro = {
    .card_name = CARD_1_NAME,
    .control_name = "BYTES1.5",
    .type = SND_CTL_ELEM_TYPE_BYTES,
    .is_readable = 1,
    .is_writable = 0,
    .max_bytes = 38,
    .config_bytes = 6,
    .config_data = {SOF_ABI_HEADER_SAMPLE(6), 0x01, 0x02, 0x04, 0x08, 0x10,
                    0x20},
};

static struct MockConfigControl card8_bytes = {
    .card_name = CARD_8_NAME,
    .control_name = "BYTES8.6",
    .type = SND_CTL_ELEM_TYPE_BYTES,
    .is_readable = 1,
    .is_writable = 1,
    .max_bytes = 72,
    .config_bytes = 16,
    .config_data = {SOF_ABI_HEADER_SAMPLE(16), 0x01, 0x02, 0x03, 0x04, 0x10,
                    0x20, 0x30, 0x40, 0x01, 0x02, 0x03, 0x04, 0x10, 0x20, 0x30,
                    0x40},
};

static snd_ctl_t* snd_ctl_ptr_val;
static snd_ctl_elem_id_t* snd_ctl_elem_id_ptr_val;
static snd_ctl_elem_info_t* snd_ctl_elem_info_ptr_val;
static snd_ctl_elem_value_t* snd_ctl_elem_value_ptr_val;
static struct MockConfigControl* stub_control_ptr;
static char snd_ctl_opened_card_name[MAX_CARD_NAME_LEN];
static size_t snd_ctl_elem_tlv_read_called;
static size_t snd_ctl_elem_tlv_write_called;

static void ResetStubData() {
  snd_ctl_ptr_val = reinterpret_cast<snd_ctl_t*>(0x4323);
  snd_ctl_elem_id_ptr_val = reinterpret_cast<snd_ctl_elem_id_t*>(1);
  snd_ctl_elem_info_ptr_val = reinterpret_cast<snd_ctl_elem_info_t*>(2);
  snd_ctl_elem_value_ptr_val = reinterpret_cast<snd_ctl_elem_value_t*>(3);
  stub_control_ptr = NULL;
  memset(snd_ctl_opened_card_name, 0, MAX_CARD_NAME_LEN);
  snd_ctl_elem_tlv_read_called = 0;
  snd_ctl_elem_tlv_write_called = 0;
}

namespace {

class AlsaConfigTestSuite : public testing::Test {
 protected:
  virtual void SetUp() { ResetStubData(); }

  virtual void TearDown() {
    cras_alsa_config_release_controls_on_card(0);
    cras_alsa_config_release_controls_on_card(1);
    cras_alsa_config_release_controls_on_card(8);
  }
};

TEST_F(AlsaConfigTestSuite, GetSetSwitch) {
  bool state;
  int rc;

  // Assign default states.
  card0_switch.state = 0;
  card1_switch.state = 1;

  stub_control_ptr = &card0_switch;
  rc = cras_alsa_config_get_switch(card0_switch.control_name, &state);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(card0_switch.state, state);

  stub_control_ptr = &card1_switch;
  rc = cras_alsa_config_get_switch(card1_switch.control_name, &state);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(card1_switch.state, state);

  // Set "SWITCH0.1" as enabled.
  state = 1;
  stub_control_ptr = &card0_switch;
  rc = cras_alsa_config_set_switch(card0_switch.control_name, state);
  EXPECT_EQ(0, rc);

  // Set "SWITCH1.3" as disabled.
  state = 0;
  stub_control_ptr = &card1_switch;
  rc = cras_alsa_config_set_switch(card1_switch.control_name, state);
  EXPECT_EQ(0, rc);

  // Get "SWITCH0.1" state for verification.
  stub_control_ptr = &card0_switch;
  rc = cras_alsa_config_get_switch(card0_switch.control_name, &state);
  EXPECT_EQ(0, rc);
  EXPECT_TRUE(state);

  // Get "SWITCH1.3" state for verification.
  stub_control_ptr = &card1_switch;
  rc = cras_alsa_config_get_switch(card1_switch.control_name, &state);
  EXPECT_EQ(0, rc);
  EXPECT_FALSE(state);
}

TEST_F(AlsaConfigTestSuite, GetBytes) {
  uint8_t buf[MAX_CONFIG_BYTE_LEN] = {0};
  unsigned int i;
  int rc;

  stub_control_ptr = &card0_bytes;
  rc = cras_alsa_config_get_tlv_bytes_maxcount(card0_bytes.control_name);
  EXPECT_EQ((int)card0_bytes.max_bytes, rc);
  rc = cras_alsa_config_get_tlv_bytes_data(card0_bytes.control_name, buf,
                                           MAX_CONFIG_BYTE_LEN);
  EXPECT_EQ((int)card0_bytes.config_bytes, rc);
  for (i = 0; i < card0_bytes.config_bytes; i++) {
    EXPECT_EQ(buf[i], card0_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 1);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 0);

  stub_control_ptr = &card1_bytes_ro;
  rc = cras_alsa_config_get_tlv_bytes_maxcount(card1_bytes_ro.control_name);
  EXPECT_EQ((int)card1_bytes_ro.max_bytes, rc);
  rc = cras_alsa_config_get_tlv_bytes_data(card1_bytes_ro.control_name, buf,
                                           MAX_CONFIG_BYTE_LEN);
  EXPECT_EQ((int)card1_bytes_ro.config_bytes, rc);
  for (i = 0; i < card1_bytes_ro.config_bytes; i++) {
    EXPECT_EQ(buf[i], card1_bytes_ro.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 2);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 0);

  stub_control_ptr = &card8_bytes;
  rc = cras_alsa_config_get_tlv_bytes_maxcount(card8_bytes.control_name);
  EXPECT_EQ((int)card8_bytes.max_bytes, rc);
  rc = cras_alsa_config_get_tlv_bytes_data(card8_bytes.control_name, buf,
                                           MAX_CONFIG_BYTE_LEN);
  EXPECT_EQ((int)card8_bytes.config_bytes, rc);
  for (i = 0; i < card8_bytes.config_bytes; i++) {
    EXPECT_EQ(buf[i], card8_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 3);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 0);
}

TEST_F(AlsaConfigTestSuite, SetBytes) {
  const size_t buf_size = 4;
  uint8_t buf[buf_size] = {0x55, 0xaa, 0x55, 0xaa};
  unsigned int i;
  int rc;

  // Set control "BYTES1.4".
  stub_control_ptr = &card1_bytes;
  rc = cras_alsa_config_set_tlv_bytes(card1_bytes.control_name, buf, buf_size);
  EXPECT_EQ(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_EQ(buf[i], card1_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  // One read call for the preliminary read.
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 1);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 1);

  buf[0] = 0x66;

  // Set control "BYTES1.4" again.
  rc = cras_alsa_config_set_tlv_bytes(card1_bytes.control_name, buf, buf_size);
  EXPECT_EQ(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_EQ(buf[i], card1_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  // The preliminary read is only needed for the first time.
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 1);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 2);

  // Set control "BYTES8.6".
  stub_control_ptr = &card8_bytes;
  rc = cras_alsa_config_set_tlv_bytes(card8_bytes.control_name, buf, buf_size);
  EXPECT_EQ(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_EQ(buf[i], card8_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  // The preliminary read is needed by controls individually.
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 2);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 3);

  buf[0] = 0x77;

  // Set control "BYTES1.4" the third time.
  stub_control_ptr = &card1_bytes;
  rc = cras_alsa_config_set_tlv_bytes(card1_bytes.control_name, buf, buf_size);
  EXPECT_EQ(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_EQ(buf[i], card1_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  // The preliminary read is only needed for the first time.
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 2);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 4);

  cras_alsa_config_release_controls_on_card(1);
  buf[0] = 0x88;

  // Set control "BYTES1.4" the fourth time.
  rc = cras_alsa_config_set_tlv_bytes(card1_bytes.control_name, buf, buf_size);
  EXPECT_EQ(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_EQ(buf[i], card1_bytes.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] mismatched";
  }
  // The preliminary read is needed once its control got released.
  EXPECT_EQ(snd_ctl_elem_tlv_read_called, 3);
  EXPECT_EQ(snd_ctl_elem_tlv_write_called, 5);

  // Set read-only control "BYTES1.5" and expect on error while the control
  // config is not tainted.
  stub_control_ptr = &card1_bytes_ro;
  rc = cras_alsa_config_set_tlv_bytes(card1_bytes_ro.control_name, buf,
                                      buf_size);
  EXPECT_LT(rc, 0);
  for (i = 0; i < buf_size; i++) {
    EXPECT_NE(buf[i], card1_bytes_ro.config_data[SOF_ABI_HEADER_SIZE + i])
        << "byte[" << i << "] tainted";
  }
}

TEST_F(AlsaConfigTestSuite, InvalidArguments) {
  bool state;
  // Allocate the placeholding buffer in full size and initialize it to avoid
  // sanitizer errors even if we knew that will not be reached during the test.
  uint8_t buf[MAX_CONFIG_BYTE_LEN] = {0};
  int rc;
  int buf_size;

  // Health check for non-existent control name.
  // -ENOENT should be received as the implication of "control not found".
  stub_control_ptr = &card0_bytes;
  rc = cras_alsa_config_get_tlv_bytes_data("BYTES99.99", buf,
                                           MAX_CONFIG_BYTE_LEN);
  EXPECT_EQ(rc, -ENOENT);
  rc = cras_alsa_config_set_tlv_bytes("BYTES99.99", buf, MAX_CONFIG_BYTE_LEN);
  EXPECT_EQ(rc, -ENOENT);
  rc = cras_alsa_config_get_switch("SWITCH99.99", &state);
  EXPECT_EQ(rc, -ENOENT);

  // Health check for wrong type control.
  state = 1;
  stub_control_ptr = &card1_bytes;
  rc = cras_alsa_config_set_switch(card1_bytes.control_name, state);
  EXPECT_LT(rc, 0);

  stub_control_ptr = &card0_switch;
  rc = cras_alsa_config_get_tlv_bytes_data(card0_switch.control_name, buf,
                                           MAX_CONFIG_BYTE_LEN);
  EXPECT_LT(rc, 0);

  // Health check for un-allocated buffer or insufficient size for config read.
  stub_control_ptr = &card0_bytes;
  rc = cras_alsa_config_get_tlv_bytes_maxcount(card0_bytes.control_name);
  EXPECT_EQ(rc, (int)card0_bytes.max_bytes);
  buf_size = rc;
  rc = cras_alsa_config_get_tlv_bytes_data(card0_bytes.control_name,
                                           (uint8_t*)NULL, buf_size);
  EXPECT_LT(rc, 0);
  buf_size = card0_bytes.config_bytes - 1;
  rc = cras_alsa_config_get_tlv_bytes_data(card0_bytes.control_name, buf,
                                           buf_size);
  EXPECT_LT(rc, 0);
}

}  // namespace

extern "C" {

int snd_card_next(int* rcard) {
  // Valid card indices: 0, 1, 8
  switch (*rcard) {
    case -1:
      *rcard = 0;
      return 0;
    case 0:
      *rcard = 1;
      return 0;
    case 1:
      *rcard = 8;
      return 0;
    case 8:
      *rcard = -1;
      return 0;
    default:
      return -EINVAL;
  }
}

int snd_ctl_open(snd_ctl_t** ctl, const char* name, int mode) {
  *ctl = snd_ctl_ptr_val;
  strncpy(snd_ctl_opened_card_name, name, MAX_CARD_NAME_LEN);
  return 0;
}

int snd_ctl_close(snd_ctl_t* ctl) {
  return 0;
}

int snd_ctl_elem_info_malloc(snd_ctl_elem_info_t** ptr) {
  *ptr = snd_ctl_elem_info_ptr_val;
  return 0;
}

void snd_ctl_elem_info_free(snd_ctl_elem_info_t* obj) {}

int snd_ctl_elem_id_malloc(snd_ctl_elem_id_t** ptr) {
  *ptr = snd_ctl_elem_id_ptr_val;
  return 0;
}

void snd_ctl_elem_id_free(snd_ctl_elem_id_t* obj) {}

int snd_ctl_elem_value_malloc(snd_ctl_elem_value_t** ptr) {
  *ptr = snd_ctl_elem_value_ptr_val;
  return 0;
}

void snd_ctl_elem_value_free(snd_ctl_elem_value_t* obj) {}

// Stub this function to check the existence of the element name "str".
int snd_ctl_ascii_elem_id_parse(snd_ctl_elem_id_t* dst, const char* str) {
  char elem_name[MAX_CTL_NAME_LEN];
  if (!stub_control_ptr) {
    return -EINVAL;
  }

  if (strcmp(stub_control_ptr->card_name, snd_ctl_opened_card_name)) {
    return -ENOENT;
  }

  snprintf(elem_name, MAX_CTL_NAME_LEN, "name='%s'",
           stub_control_ptr->control_name);
  if (strcmp(elem_name, str)) {
    return -ENOENT;
  }
  return 0;
}

void snd_ctl_elem_info_set_id(snd_ctl_elem_info_t* info,
                              const snd_ctl_elem_id_t* ptr) {}

int snd_ctl_elem_info(snd_ctl_t* ctl, snd_ctl_elem_info_t* info) {
  return 0;
}

void snd_ctl_elem_value_set_id(snd_ctl_elem_value_t* obj,
                               const snd_ctl_elem_id_t* ptr) {}

int snd_ctl_elem_read(snd_ctl_t* ctl, snd_ctl_elem_value_t* obj) {
  return 0;
}

// Stub this function to get state from the corresponding MockConfigControl
// instance.
int snd_ctl_elem_value_get_boolean(const snd_ctl_elem_value_t* obj,
                                   unsigned int idx) {
  if (!stub_control_ptr) {
    return 0;
  }
  return (int)stub_control_ptr->state;
}

int snd_ctl_elem_write(snd_ctl_t* ctl, snd_ctl_elem_value_t* obj) {
  // Return 0 on success; >0 on success when value was changed; <0 on error
  return 1;
}

// Stub this function to set state to the corresponding MockConfigControl
// instance.
void snd_ctl_elem_value_set_boolean(snd_ctl_elem_value_t* obj,
                                    unsigned int idx,
                                    long val) {
  if (!stub_control_ptr) {
    return;
  }
  stub_control_ptr->state = !!val;
}

snd_ctl_elem_type_t snd_ctl_elem_info_get_type(
    const snd_ctl_elem_info_t* info) {
  if (!stub_control_ptr) {
    return SND_CTL_ELEM_TYPE_NONE;
  }
  return stub_control_ptr->type;
}

unsigned int snd_ctl_elem_info_get_count(const snd_ctl_elem_info_t* info) {
  if (!stub_control_ptr) {
    return 0;
  }
  return stub_control_ptr->max_bytes;
}

int snd_ctl_elem_info_is_tlv_readable(const snd_ctl_elem_info_t* info) {
  if (!stub_control_ptr) {
    return 0;
  }
  return (int)stub_control_ptr->is_readable;
}

int snd_ctl_elem_info_is_tlv_writable(const snd_ctl_elem_info_t* info) {
  if (!stub_control_ptr) {
    return 0;
  }
  return (int)stub_control_ptr->is_writable;
}

// Stub this function to read bytes from the corresponding MockConfigControl
// instance.
int snd_ctl_elem_tlv_read(snd_ctl_t* ctl,
                          const snd_ctl_elem_id_t* id,
                          unsigned int* buf,
                          unsigned int size) {
  if (!stub_control_ptr) {
    return -EINVAL;
  }
  if (size < stub_control_ptr->max_bytes + 2 * sizeof(uint32_t)) {
    return -EINVAL;
  }
  buf[0] = 1;  // TLV TAG which is irrelevant for tests
  buf[1] = stub_control_ptr->config_bytes + SOF_ABI_HEADER_SIZE;  // TLV SIZE
  memcpy(&buf[2], stub_control_ptr->config_data, buf[1]);         // TLV DATA

  snd_ctl_elem_tlv_read_called++;
  return 0;
}

// Stub this function to write bytes to the corresponding MockConfigControl
// instance.
int snd_ctl_elem_tlv_write(snd_ctl_t* ctl,
                           const snd_ctl_elem_id_t* id,
                           const unsigned int* buf) {
  unsigned int size = buf[1];  // TLV SIZE
  if (!stub_control_ptr) {
    return -EINVAL;
  }

  if (size > stub_control_ptr->max_bytes) {
    return -EINVAL;
  }

  memcpy(stub_control_ptr->config_data, &buf[2], size);  // TLV DATA

  snd_ctl_elem_tlv_write_called++;
  // Return 0 on success; >0 on success when value was changed; <0 on error
  return size;
}
}
