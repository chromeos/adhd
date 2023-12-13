// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_TESTING_DBUS_MESSAGE_BUILDER_HH_
#define CRAS_TESTING_DBUS_MESSAGE_BUILDER_HH_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cras/common/check.h"
#include "dbus/dbus.h"
#include "google/protobuf/descriptor.h"

// A type-safe D-Bus message builder.
class DBusMessageBuilder {
 public:
  DBusMessageBuilder(DBusMessage&& message) = delete;
  DBusMessageBuilder(DBusMessage& message) : message_(message) {
    DBusMessageIter iter;
    dbus_message_iter_init_append(&message_, &iter);
    stack_.push_back(iter);
  }

  void EmitInt32(int32_t value) {
    CRAS_CHECK(
        dbus_message_iter_append_basic(&iter(), DBUS_TYPE_INT32, &value));
  }

  void EmitInt64(int64_t value) {
    CRAS_CHECK(
        dbus_message_iter_append_basic(&iter(), DBUS_TYPE_INT64, &value));
  }

  void EmitUint32(uint32_t value) {
    CRAS_CHECK(
        dbus_message_iter_append_basic(&iter(), DBUS_TYPE_UINT32, &value));
  }

  void EmitUint64(uint64_t value) {
    CRAS_CHECK(
        dbus_message_iter_append_basic(&iter(), DBUS_TYPE_UINT64, &value));
  }

  void EmitDouble(double value) {
    CRAS_CHECK(
        dbus_message_iter_append_basic(&iter(), DBUS_TYPE_DOUBLE, &value));
  }

  void EmitBool(bool value) {
    dbus_bool_t b = !!value;
    CRAS_CHECK(dbus_message_iter_append_basic(&iter(), DBUS_TYPE_BOOLEAN, &b));
  }

  void EmitString(std::string value) {
    const char* str = value.c_str();
    CRAS_CHECK(dbus_message_iter_append_basic(&iter(), DBUS_TYPE_STRING, &str));
  }

  void BeginArray(google::protobuf::FieldDescriptor::Type type) {
    DBusMessageIter array_iter;
    CRAS_CHECK(dbus_message_iter_open_container(
        &iter(), DBUS_TYPE_ARRAY, DBusTypeAsString(type), &array_iter));
    stack_.push_back(array_iter);
  }

  void EndArray() {
    DBusMessageIter array_iter = iter();
    CRAS_CHECK(!stack_.empty());
    stack_.pop_back();
    CRAS_CHECK(dbus_message_iter_close_container(&iter(), &array_iter));
  }

 private:
  const char* DBusTypeAsString(google::protobuf::FieldDescriptor::Type type) {
    using google::protobuf::FieldDescriptor;
    switch (type) {
      case FieldDescriptor::TYPE_INT32:
        return DBUS_TYPE_INT32_AS_STRING;
      case FieldDescriptor::TYPE_INT64:
        return DBUS_TYPE_INT64_AS_STRING;
      case FieldDescriptor::TYPE_UINT32:
        return DBUS_TYPE_UINT32_AS_STRING;
      case FieldDescriptor::TYPE_UINT64:
        return DBUS_TYPE_UINT64_AS_STRING;
      case FieldDescriptor::TYPE_DOUBLE:
        return DBUS_TYPE_DOUBLE_AS_STRING;
      case FieldDescriptor::TYPE_BOOL:
        return DBUS_TYPE_BOOLEAN_AS_STRING;
      case FieldDescriptor::TYPE_STRING:
        return DBUS_TYPE_STRING_AS_STRING;
      default:
        CRAS_CHECK(false && "unsupported type!");
    }
  }

  DBusMessageIter& iter() { return stack_.back(); }

 private:
  DBusMessage& message_;
  std::vector<DBusMessageIter> stack_;
};

#endif  // CRAS_TESTING_DBUS_MESSAGE_BUILDER_HH_
