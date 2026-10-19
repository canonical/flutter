// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dbus_message.h"

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

TEST(DBusMessageTest, CompleteTypeLength) {
  EXPECT_EQ(DBusCompleteTypeLength("i"), 1u);
  EXPECT_EQ(DBusCompleteTypeLength("ii"), 1u);
  EXPECT_EQ(DBusCompleteTypeLength("as"), 2u);
  EXPECT_EQ(DBusCompleteTypeLength("a{sv}i"), 5u);
  EXPECT_EQ(DBusCompleteTypeLength("(so)"), 4u);
  EXPECT_EQ(DBusCompleteTypeLength("a((so)(so)(so)iiassusau)"), 24u);
  EXPECT_EQ(DBusCompleteTypeLength(""), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("a"), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("()"), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("(s"), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("a{vs}"), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("a{sss}"), 0u);
  EXPECT_EQ(DBusCompleteTypeLength("z"), 0u);
}

TEST(DBusMessageTest, WriterSignature) {
  DBusWriter writer;
  writer.WriteByte(1);
  writer.OpenStruct();
  writer.WriteString("a");
  writer.WriteObjectPath("/");
  writer.CloseStruct();
  writer.OpenArray("{sv}");
  writer.OpenDictEntry();
  writer.WriteString("key");
  writer.OpenVariant("i");
  writer.WriteInt32(1);
  writer.CloseVariant();
  writer.CloseDictEntry();
  writer.CloseArray();
  writer.OpenVariant("as");
  writer.OpenArray("s");
  writer.CloseArray();
  writer.CloseVariant();
  EXPECT_EQ(writer.signature(), "y(so)a{sv}v");
}

TEST(DBusMessageTest, EmptyArrayPadding) {
  DBusWriter writer;
  writer.OpenArray("t");
  writer.CloseArray();
  // Length then padding to 8 even though the array is empty.
  EXPECT_EQ(writer.data().size(), 8u);
  writer.WriteByte(1);
  DBusReader reader(writer.data().data(), writer.data().size(),
                    writer.signature(), false);
  EXPECT_TRUE(reader.OpenArray());
  EXPECT_FALSE(reader.HasMore());
  EXPECT_TRUE(reader.CloseArray());
  uint8_t byte;
  EXPECT_TRUE(reader.ReadByte(&byte));
  EXPECT_EQ(byte, 1);
}

TEST(DBusMessageTest, RoundTrip) {
  DBusMessage call = DBusMessage::MethodCall("org.example", "/org/example",
                                             "org.example.Iface", "Method");
  DBusWriter& writer = call.writer();
  writer.WriteBoolean(true);
  writer.WriteInt16(-2);
  writer.WriteUInt16(3);
  writer.WriteInt32(-4);
  writer.WriteUInt32(5);
  writer.WriteInt64(-6);
  writer.WriteUInt64(7);
  writer.WriteDouble(8.5);
  writer.WriteSignature("a{sv}");
  writer.OpenArray("{sv}");
  for (int i = 0; i < 3; i++) {
    writer.OpenDictEntry();
    writer.WriteString("key" + std::to_string(i));
    writer.OpenVariant("(si)");
    writer.OpenStruct();
    writer.WriteString("value");
    writer.WriteInt32(i);
    writer.CloseStruct();
    writer.CloseVariant();
    writer.CloseDictEntry();
  }
  writer.CloseArray();
  writer.WriteString("end");

  std::vector<uint8_t> data = call.Serialize(42);
  EXPECT_EQ(DBusMessage::GetLength(data.data(), data.size()), data.size());
  std::optional<DBusMessage> parsed =
      DBusMessage::Parse(data.data(), data.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type(), DBusMessage::Type::kMethodCall);
  EXPECT_EQ(parsed->serial(), 42u);
  EXPECT_EQ(parsed->destination(), "org.example");
  EXPECT_EQ(parsed->path(), "/org/example");
  EXPECT_EQ(parsed->interface(), "org.example.Iface");
  EXPECT_EQ(parsed->member(), "Method");
  EXPECT_EQ(parsed->signature(), "bnqiuxtdga{sv}s");
  EXPECT_TRUE(parsed->expects_reply());

  DBusReader reader = parsed->reader();
  bool b;
  int16_t n;
  uint16_t q;
  int32_t i;
  uint32_t u;
  int64_t x;
  uint64_t t;
  double d;
  std::string s;
  EXPECT_TRUE(reader.ReadBoolean(&b));
  EXPECT_TRUE(b);
  EXPECT_TRUE(reader.ReadInt16(&n));
  EXPECT_EQ(n, -2);
  EXPECT_TRUE(reader.ReadUInt16(&q));
  EXPECT_EQ(q, 3);
  EXPECT_TRUE(reader.ReadInt32(&i));
  EXPECT_EQ(i, -4);
  EXPECT_TRUE(reader.ReadUInt32(&u));
  EXPECT_EQ(u, 5u);
  EXPECT_TRUE(reader.ReadInt64(&x));
  EXPECT_EQ(x, -6);
  EXPECT_TRUE(reader.ReadUInt64(&t));
  EXPECT_EQ(t, 7u);
  EXPECT_TRUE(reader.ReadDouble(&d));
  EXPECT_EQ(d, 8.5);
  EXPECT_TRUE(reader.ReadSignature(&s));
  EXPECT_EQ(s, "a{sv}");
  EXPECT_TRUE(reader.OpenArray());
  int count = 0;
  while (reader.HasMore()) {
    EXPECT_TRUE(reader.OpenDictEntry());
    EXPECT_TRUE(reader.ReadString(&s));
    EXPECT_EQ(s, "key" + std::to_string(count));
    std::string signature;
    EXPECT_TRUE(reader.OpenVariant(&signature));
    EXPECT_EQ(signature, "(si)");
    if (count == 1) {
      // Leave the value unread to check it's skipped.
    } else {
      EXPECT_TRUE(reader.OpenStruct());
      EXPECT_TRUE(reader.ReadString(&s));
      EXPECT_EQ(s, "value");
      EXPECT_TRUE(reader.ReadInt32(&i));
      EXPECT_EQ(i, count);
      EXPECT_TRUE(reader.CloseStruct());
    }
    EXPECT_TRUE(reader.CloseVariant());
    EXPECT_TRUE(reader.CloseDictEntry());
    count++;
  }
  EXPECT_EQ(count, 3);
  EXPECT_TRUE(reader.CloseArray());
  EXPECT_TRUE(reader.ReadString(&s));
  EXPECT_EQ(s, "end");
  EXPECT_FALSE(reader.HasMore());
  EXPECT_TRUE(reader.ok());
}

TEST(DBusMessageTest, WrongTypeFails) {
  DBusWriter writer;
  writer.WriteString("x");
  writer.WriteInt32(1);
  DBusReader reader(writer.data().data(), writer.data().size(),
                    writer.signature(), false);
  int32_t i;
  EXPECT_FALSE(reader.ReadInt32(&i));
  EXPECT_FALSE(reader.ok());
  std::string s;
  EXPECT_FALSE(reader.ReadString(&s));
}

TEST(DBusMessageTest, Reply) {
  DBusMessage call = DBusMessage::MethodCall("", "/", "", "Ping");
  std::vector<uint8_t> data = call.Serialize(7);
  std::optional<DBusMessage> parsed =
      DBusMessage::Parse(data.data(), data.size());
  ASSERT_TRUE(parsed.has_value());

  DBusMessage reply = DBusMessage::MethodReturn(*parsed);
  data = reply.Serialize(8);
  parsed = DBusMessage::Parse(data.data(), data.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type(), DBusMessage::Type::kMethodReturn);
  EXPECT_EQ(parsed->reply_serial(), 7u);

  DBusMessage error =
      DBusMessage::Error(*parsed, "org.example.Error", "Failed");
  data = error.Serialize(9);
  parsed = DBusMessage::Parse(data.data(), data.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type(), DBusMessage::Type::kError);
  EXPECT_EQ(parsed->error_name(), "org.example.Error");
  EXPECT_EQ(parsed->reply_serial(), 8u);
  DBusReader reader = parsed->reader();
  std::string s;
  EXPECT_TRUE(reader.ReadString(&s));
  EXPECT_EQ(s, "Failed");
}

TEST(DBusMessageTest, ParseBigEndian) {
  // Signal /a org.b.C D with body (u 0x01020304, s "hi").
  std::vector<uint8_t> data = {
      'B', 4, 0, 1, 0, 0, 0, 11, 0, 0, 0, 1, 0, 0, 0, 0,
  };
  auto field = [&](uint8_t code, char type, std::string value) {
    while (data.size() % 8 != 0) {
      data.push_back(0);
    }
    data.push_back(code);
    data.push_back(1);
    data.push_back(type);
    data.push_back(0);
    if (type == 'g') {
      data.push_back(value.size());
    } else {
      uint32_t length = value.size();
      data.push_back(length >> 24);
      data.push_back(length >> 16);
      data.push_back(length >> 8);
      data.push_back(length);
    }
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
  };
  field(1, 'o', "/a");
  field(2, 's', "org.b.C");
  field(3, 's', "D");
  field(8, 'g', "us");
  uint32_t fields_length = data.size() - 16;
  data[12] = fields_length >> 24;
  data[13] = fields_length >> 16;
  data[14] = fields_length >> 8;
  data[15] = fields_length;
  while (data.size() % 8 != 0) {
    data.push_back(0);
  }
  std::vector<uint8_t> body = {1, 2, 3, 4, 0, 0, 0, 2, 'h', 'i', 0};
  data.insert(data.end(), body.begin(), body.end());

  std::optional<DBusMessage> parsed =
      DBusMessage::Parse(data.data(), data.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type(), DBusMessage::Type::kSignal);
  EXPECT_EQ(parsed->path(), "/a");
  EXPECT_EQ(parsed->interface(), "org.b.C");
  EXPECT_EQ(parsed->member(), "D");
  DBusReader reader = parsed->reader();
  uint32_t u;
  std::string s;
  EXPECT_TRUE(reader.ReadUInt32(&u));
  EXPECT_EQ(u, 0x01020304u);
  EXPECT_TRUE(reader.ReadString(&s));
  EXPECT_EQ(s, "hi");
}

TEST(DBusMessageTest, ParseInvalid) {
  DBusMessage signal = DBusMessage::Signal("/a", "org.b.C", "D");
  signal.writer().WriteString("hello");
  std::vector<uint8_t> data = signal.Serialize(1);

  // Truncated.
  EXPECT_FALSE(DBusMessage::Parse(data.data(), data.size() - 1).has_value());

  // Every corruption of a single byte must be handled safely.
  for (size_t i = 0; i < data.size(); i++) {
    for (uint8_t value : {0x00, 0x01, 0x7f, 0xff}) {
      std::vector<uint8_t> corrupt = data;
      corrupt[i] = value;
      std::optional<DBusMessage> parsed =
          DBusMessage::Parse(corrupt.data(), corrupt.size());
      if (parsed.has_value()) {
        DBusReader reader = parsed->reader();
        while (reader.HasMore() && reader.Skip()) {
        }
      }
    }
  }

  // Missing the member.
  DBusMessage call = DBusMessage::MethodCall("", "/", "", "");
  data = call.Serialize(1);
  EXPECT_FALSE(DBusMessage::Parse(data.data(), data.size()).has_value());

  // Serial zero.
  data = signal.Serialize(0);
  EXPECT_FALSE(DBusMessage::Parse(data.data(), data.size()).has_value());
}

}  // namespace testing
}  // namespace flutter
