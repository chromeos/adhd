// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus/dbus.h>

#include "absl/types/span.h"
#include "cras/testing/dbus/message_builder.hh"
#include "gtest/gtest.h"

using google::protobuf::FieldDescriptor;

TEST(MessageBuilder, BuildAndPrintAll) {
  DBusMessage* message =
      dbus_message_new_method_call(NULL, "/", NULL, "method");
  assert(message != nullptr);
  {
    DBusMessageBuilder builder(*message);
    builder.EmitInt32(INT32_MIN);
    builder.EmitInt64(INT64_MIN);
    builder.EmitUint32(UINT32_MAX);
    builder.EmitUint64(UINT64_MAX);
    builder.EmitBool(true);
    builder.EmitDouble(1e300);
    builder.EmitString("this is a string");

    builder.BeginArray(FieldDescriptor::TYPE_INT32);
    builder.EmitInt32(INT32_MIN);
    builder.EmitInt32(-1);
    builder.EmitInt32(0);
    builder.EmitInt32(1);
    builder.EmitInt32(INT32_MAX);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_INT64);
    builder.EmitInt64(INT64_MIN);
    builder.EmitInt64(-1);
    builder.EmitInt64(0);
    builder.EmitInt64(1);
    builder.EmitInt64(INT64_MAX);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_UINT32);
    builder.EmitUint32(0);
    builder.EmitUint32(1);
    builder.EmitUint32(UINT32_MAX);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_UINT64);
    builder.EmitUint64(0);
    builder.EmitUint64(1);
    builder.EmitUint64(UINT64_MAX);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_BOOL);
    builder.EmitBool(true);
    builder.EmitBool(false);
    builder.EmitBool(true);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_DOUBLE);
    builder.EmitDouble(1.5);
    builder.EmitDouble(2.5);
    builder.EmitDouble(3.5);
    builder.EndArray();

    builder.BeginArray(FieldDescriptor::TYPE_STRING);
    builder.EmitString("foo");
    builder.EmitString("bar");
    builder.EmitString("baz");
    builder.EndArray();
  }

  int32_t i32;
  int64_t i64;
  uint32_t u32;
  uint64_t u64;
  dbus_bool_t b;
  double f64;
  char* str;
  int32_t* i32a;
  int i32as;
  int64_t* i64a;
  int i64as;
  uint32_t* u32a;
  int u32as;
  uint64_t* u64a;
  int u64as;
  dbus_bool_t* ba;
  int bas;
  double* f64a;
  int f64as;
  char** stra;
  int stras;

  DBusError err = DBUS_ERROR_INIT;
  ASSERT_TRUE(dbus_message_get_args(
      message, &err,                                     // comments
      DBUS_TYPE_INT32, &i32,                             // to
      DBUS_TYPE_INT64, &i64,                             // keep
      DBUS_TYPE_UINT32, &u32,                            // line
      DBUS_TYPE_UINT64, &u64,                            // breaks
      DBUS_TYPE_BOOLEAN, &b,                             // in
      DBUS_TYPE_DOUBLE, &f64,                            // clang-format
      DBUS_TYPE_STRING, &str,                            //
      DBUS_TYPE_ARRAY, DBUS_TYPE_INT32, &i32a, &i32as,   //
      DBUS_TYPE_ARRAY, DBUS_TYPE_INT64, &i64a, &i64as,   //
      DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &u32a, &u32as,  //
      DBUS_TYPE_ARRAY, DBUS_TYPE_UINT64, &u64a, &u64as,  //
      DBUS_TYPE_ARRAY, DBUS_TYPE_BOOLEAN, &ba, &bas,     //
      DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE, &f64a, &f64as,  //
      DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &stra, &stras,  //
      DBUS_TYPE_INVALID                                  //
      ))
      << err.message;

  EXPECT_EQ(i32, INT32_MIN);
  EXPECT_EQ(i64, INT64_MIN);
  EXPECT_EQ(u32, UINT32_MAX);
  EXPECT_EQ(u64, UINT64_MAX);
  EXPECT_EQ(b, true);
  EXPECT_EQ(f64, 1e300);
  EXPECT_STREQ(str, "this is a string");
  EXPECT_EQ(absl::Span(i32a, i32as),
            (std::vector<int32_t>{INT32_MIN, -1, 0, 1, INT32_MAX}));
  EXPECT_EQ(absl::Span(i64a, i64as),
            (std::vector<int64_t>{INT64_MIN, -1, 0, 1, INT64_MAX}));
  EXPECT_EQ(absl::Span(u32a, u32as), (std::vector<uint32_t>{0, 1, UINT32_MAX}));
  EXPECT_EQ(absl::Span(u64a, u64as), (std::vector<uint64_t>{0, 1, UINT64_MAX}));
  EXPECT_EQ(absl::Span(ba, bas), (std::vector<dbus_bool_t>{true, false, true}));
  EXPECT_EQ(absl::Span(f64a, f64as), (std::vector<double>{1.5, 2.5, 3.5}));

  ASSERT_EQ(stras, 3);
  EXPECT_STREQ(stra[0], "foo");
  EXPECT_STREQ(stra[1], "bar");
  EXPECT_STREQ(stra[2], "baz");

  free(stra[0]);
  free(stra[1]);
  free(stra[2]);
  free(stra);

  dbus_message_unref(message);
}
