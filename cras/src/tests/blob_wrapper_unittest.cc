// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

#include "cras/src/common/blob_wrapper.h"

static const size_t sof_blob_data_length = 3;
static const size_t sof_blob_header_length = 10;
static const size_t sof_blob_sample_length =
    sof_blob_data_length + sof_blob_header_length;
static const uint32_t sof_blob_sample[sof_blob_sample_length] = {
    /* HEADER BYTES */
    3,  // TAG: SOF_CTRL_CMD_BINARY
    (sof_blob_sample_length - 2) * sizeof(uint32_t),  // SIZE
    0xfeedbacc,                                       // ABI_HEADER->magic
    0,                                                // ABI_HEADER->type
    sof_blob_data_length * sizeof(uint32_t),          // ABI_HEADER->size
    0x00001234,                                       // ABI_HEADER->abi
    0,
    0,
    0,
    0,  // ABI_HEADER->reserved[4]

    /* CONFIG DATA BYTES */
    0x04030201,
    0x08070605,
    0x0c0b0a09,
};

namespace {

TEST(BlobWrapper, BaseInvalidArguments) {
  struct blob_wrapper* bw = NULL;
  const size_t buf_size = 4;
  uint8_t buf[buf_size] = {0};

  // Health check by invalid blob_wrapper input.
  EXPECT_EQ(-EINVAL, blob_wrapper_get_wrapped_size(bw, buf, buf_size));
  EXPECT_EQ(-EINVAL, blob_wrapper_get_unwrapped_size(bw, buf, buf_size));
  EXPECT_EQ(-EINVAL, blob_wrapper_wrap(bw, buf, buf_size, buf, buf_size));
  EXPECT_EQ(-EINVAL, blob_wrapper_unwrap(bw, buf, buf_size, buf, buf_size));

  // Health check by un-allocated dst buffer.
  bw = sof_blob_wrapper_create();
  ASSERT_NE(bw, (struct blob_wrapper*)NULL);
  EXPECT_EQ(-EINVAL, blob_wrapper_wrap(bw, (uint8_t*)NULL, 0, buf, buf_size));
  EXPECT_EQ(-EINVAL, blob_wrapper_unwrap(bw, (uint8_t*)NULL, 0, buf, buf_size));

  free(bw);
}

TEST(BlobWrapper, TlvBlobWrapUnwrap) {
  struct blob_wrapper* bw;
  const size_t length = 8;
  size_t header_size = 2 * sizeof(uint32_t);
  uint8_t value_bytes[length] = {0x01, 0x02, 0x04, 0x08,
                                 0x10, 0x20, 0x40, 0x80};
  uint8_t* wbuf;
  size_t wbuf_size;
  uint8_t* uwbuf;
  size_t uwbuf_size;
  size_t i;
  int rc;

  // Instantiate tlv_blob_wrapper.
  bw = tlv_blob_wrapper_create();
  ASSERT_NE(bw, (struct blob_wrapper*)NULL);

  // Test blob wrapping.
  rc = blob_wrapper_get_wrapped_size(bw, value_bytes, length);
  EXPECT_EQ(rc, (int)(length + header_size));

  wbuf_size = rc;
  wbuf = (uint8_t*)calloc(1, wbuf_size);
  rc = blob_wrapper_wrap(bw, wbuf, wbuf_size, value_bytes, length);
  EXPECT_EQ(rc, (int)wbuf_size);

  uint32_t* wbuf_u32 = (uint32_t*)wbuf;
  EXPECT_EQ(length, wbuf_u32[1]);
  for (i = 0; i < length; i++) {
    EXPECT_EQ(value_bytes[i], wbuf[header_size + i])
        << "value_byte[" << i << "] mismatched.";
  }

  // Test blob unwrapping.
  rc = blob_wrapper_get_unwrapped_size(bw, wbuf, wbuf_size);
  EXPECT_EQ(rc, (int)length);

  uwbuf_size = rc;
  uwbuf = (uint8_t*)calloc(1, uwbuf_size);
  rc = blob_wrapper_unwrap(bw, uwbuf, uwbuf_size, wbuf, wbuf_size);
  EXPECT_EQ(rc, (int)length);

  for (i = 0; i < length; i++) {
    EXPECT_EQ(value_bytes[i], uwbuf[i])
        << "value_byte[" << i << "] mismatched.";
  }

  free(bw);
  free(wbuf);
  free(uwbuf);
}

TEST(BlobWrapper, SofBlobStandardFlow) {
  struct blob_wrapper* bw;
  const uint8_t* read_blob = (const uint8_t*)sof_blob_sample;
  size_t read_blob_size = sof_blob_sample_length * sizeof(uint32_t);
  uint8_t* uwbuf;
  size_t uwbuf_size;
  uint8_t* wbuf;
  size_t wbuf_size;
  uint32_t* buf_u32;
  size_t i;
  int rc;

  // Instantiate sof_blob_wrapper.
  bw = sof_blob_wrapper_create();
  ASSERT_NE(bw, (struct blob_wrapper*)NULL);

  // Perform blob unwrapping due to preliminary configuration read.
  // ABI header information will be stored in wrapper for future usage.
  rc = blob_wrapper_get_unwrapped_size(bw, read_blob, read_blob_size);
  EXPECT_EQ(rc, (int)(sof_blob_data_length * sizeof(uint32_t)));

  uwbuf_size = rc;
  uwbuf = (uint8_t*)calloc(1, uwbuf_size);
  rc = blob_wrapper_unwrap(bw, uwbuf, uwbuf_size, read_blob, read_blob_size);
  EXPECT_EQ(rc, (int)(sof_blob_data_length * sizeof(uint32_t)));

  buf_u32 = (uint32_t*)uwbuf;
  for (i = 0; i < sof_blob_data_length; i++) {
    EXPECT_EQ(sof_blob_sample[sof_blob_header_length + i], buf_u32[i])
        << "word[" << i << "] mismatched.";
  }

  // Test blob wrapping.
  rc = blob_wrapper_get_wrapped_size(bw, uwbuf, uwbuf_size);
  EXPECT_EQ(rc, (int)read_blob_size);

  // Allocate larger buffer on purpose, which should be fine while wrap() will
  // return the correct blob size.
  wbuf_size = rc + 8;
  wbuf = (uint8_t*)calloc(1, wbuf_size);
  rc = blob_wrapper_wrap(bw, wbuf, wbuf_size, uwbuf, uwbuf_size);
  EXPECT_EQ(rc, (int)read_blob_size);

  buf_u32 = (uint32_t*)wbuf;
  for (i = 0; i < sof_blob_sample_length; i++) {
    EXPECT_EQ(sof_blob_sample[i], buf_u32[i])
        << "word[" << i << "] mismatched.";
  }

  free(bw);
  free(uwbuf);
  free(wbuf);
}

TEST(BlobWrapper, SofBlobCheckBufferSize) {
  struct blob_wrapper* bw;
  // Allocate the placeholding buffer in full size and initialize it to avoid
  // sanitizer errors even if we knew that will not be reached during the test.
  const size_t full_size = sof_blob_sample_length * sizeof(uint32_t);
  uint8_t buf[full_size] = {0};
  size_t test_size;
  int rc;

  bw = sof_blob_wrapper_create();
  ASSERT_NE(bw, (struct blob_wrapper*)NULL);

  // Health check for insufficient dst buffer size of wrap().
  test_size = 8;
  rc = blob_wrapper_get_wrapped_size(bw, buf, test_size);
  EXPECT_EQ(rc, (int)(test_size + sof_blob_header_length * sizeof(uint32_t)));
  rc = blob_wrapper_wrap(bw, buf, rc - 1, buf, test_size);
  EXPECT_EQ(-E2BIG, rc);

  // Health check for invalid src size of get_unwrapped_size().
  test_size = sof_blob_header_length * sizeof(uint32_t) - 1;
  rc = blob_wrapper_get_unwrapped_size(bw, buf, test_size);
  EXPECT_EQ(-EINVAL, rc);

  // Health check for insufficient dst buffer size of unwrap().
  test_size = sof_blob_data_length * sizeof(uint32_t) - 1;
  rc = blob_wrapper_unwrap(bw, buf, test_size, buf,
                           (size_t)(sof_blob_sample_length * sizeof(uint32_t)));
  EXPECT_EQ(-EINVAL, rc);

  free(bw);
}

}  //  namespace
