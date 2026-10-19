// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DBUS_MESSAGE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DBUS_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flutter {

// Writes values in the D-Bus wire format, in the host byte order.
//
// The signature of the values written outside of arrays and variants is
// recorded, so a message body's signature doesn't need to be written
// separately.
class DBusWriter {
 public:
  DBusWriter() = default;

  // Creates a writer containing values already marshalled with |signature|.
  DBusWriter(std::vector<uint8_t> data, std::string signature);

  const std::vector<uint8_t>& data() const { return data_; }
  const std::string& signature() const { return signature_; }

  void WriteByte(uint8_t value);
  void WriteBoolean(bool value);
  void WriteInt16(int16_t value);
  void WriteUInt16(uint16_t value);
  void WriteInt32(int32_t value);
  void WriteUInt32(uint32_t value);
  void WriteInt64(int64_t value);
  void WriteUInt64(uint64_t value);
  void WriteDouble(double value);
  void WriteString(std::string_view value);
  void WriteObjectPath(std::string_view value);
  void WriteSignature(std::string_view value);

  // Writes an array of values of type |element_signature|, which are written
  // between these calls.
  void OpenArray(std::string_view element_signature);
  void CloseArray();

  void OpenStruct();
  void CloseStruct();

  // Dictionary entries may only be written in arrays.
  void OpenDictEntry();
  void CloseDictEntry();

  // Writes a variant containing a single value of type |signature|.
  void OpenVariant(std::string_view signature);
  void CloseVariant();

  // Pads the data to a multiple of |alignment| bytes.
  void Align(size_t alignment);

 private:
  // Records |type| in the signature unless inside an array or variant.
  void AddType(std::string_view type);

  template <typename T>
  void WriteFixed(T value, size_t alignment);

  std::vector<uint8_t> data_;
  std::string signature_;

  // The number of arrays and variants being written.
  int nesting_ = 0;

  // For each array being written, the offset of its length and the offset
  // its elements start at.
  std::vector<std::pair<size_t, size_t>> arrays_;
};

// Reads values in the D-Bus wire format.
//
// Read methods return false if the next value isn't of the requested type
// or the data is invalid, after which all reads fail.
class DBusReader {
 public:
  // Reads values of |signature| from |data| starting at |offset|. Alignment
  // is relative to the start of |data|. |data| and |signature| must outlive
  // the reader.
  DBusReader(const uint8_t* data,
             size_t size,
             std::string_view signature,
             bool big_endian,
             size_t offset = 0);

  // False if a read has failed.
  bool ok() const { return ok_; }

  // The type code of the next value, or '\0' if there are no more values in
  // the current container.
  char PeekType() const;

  // True if there are more values in the current container.
  bool HasMore() const { return PeekType() != '\0'; }

  // The current read offset into the data.
  size_t offset() const { return offset_; }

  bool ReadByte(uint8_t* value);
  bool ReadBoolean(bool* value);
  bool ReadInt16(int16_t* value);
  bool ReadUInt16(uint16_t* value);
  bool ReadInt32(int32_t* value);
  bool ReadUInt32(uint32_t* value);
  bool ReadInt64(int64_t* value);
  bool ReadUInt64(uint64_t* value);
  bool ReadDouble(double* value);
  bool ReadString(std::string* value);
  bool ReadObjectPath(std::string* value);
  bool ReadSignature(std::string* value);

  // Enters an array. Its elements are read until HasMore returns false, then
  // CloseArray skips any that remain.
  bool OpenArray();
  bool CloseArray();

  // Enters a struct or dictionary entry. Close skips any unread members.
  bool OpenStruct();
  bool CloseStruct();
  bool OpenDictEntry();
  bool CloseDictEntry();

  // Enters a variant, setting |signature| to the type of its value if not
  // nullptr. CloseVariant skips the value if it wasn't read.
  bool OpenVariant(std::string* signature = nullptr);
  bool CloseVariant();

  // Skips the next value.
  bool Skip();

 private:
  struct Container {
    char type;
    // The signature of the values in the container. For arrays, the element
    // signature, which repeats until |end|.
    std::string_view signature;
    size_t signature_offset;
    size_t end;
  };

  bool Fail();
  bool Align(size_t alignment);
  template <typename T>
  bool ReadFixed(char type, T* value);
  bool ReadStringLike(char type, std::string* value);
  bool OpenAggregate(char open);
  bool CloseContainer(char type);

  // Advances past the next complete type in the current signature.
  void ConsumeType(size_t length);

  const uint8_t* data_;
  bool big_endian_;
  size_t offset_;
  bool ok_ = true;
  std::vector<Container> containers_;
};

// Returns the length of the first complete type in |signature|, or zero if
// it is invalid.
size_t DBusCompleteTypeLength(std::string_view signature);

// A D-Bus message.
class DBusMessage {
 public:
  enum class Type : uint8_t {
    kInvalid = 0,
    kMethodCall = 1,
    kMethodReturn = 2,
    kError = 3,
    kSignal = 4,
  };

  static constexpr uint8_t kNoReplyExpected = 0x1;
  static constexpr uint8_t kNoAutoStart = 0x2;

  static DBusMessage MethodCall(std::string destination,
                                std::string path,
                                std::string interface,
                                std::string member);
  static DBusMessage Signal(std::string path,
                            std::string interface,
                            std::string member);
  static DBusMessage MethodReturn(const DBusMessage& call);
  static DBusMessage Error(const DBusMessage& call,
                           std::string name,
                           std::string_view text);

  // Returns the total length of the message starting at |data| from its
  // fixed header, which is the first 16 bytes. Returns zero if the header is
  // invalid.
  static size_t GetLength(const uint8_t* data, size_t size);

  // Parses a complete message. Returns nullopt if it is invalid.
  static std::optional<DBusMessage> Parse(const uint8_t* data, size_t size);

  // Returns the message in the wire format.
  std::vector<uint8_t> Serialize(uint32_t serial) const;

  Type type() const { return type_; }
  uint8_t flags() const { return flags_; }
  void set_flags(uint8_t flags) { flags_ = flags; }
  uint32_t serial() const { return serial_; }
  uint32_t reply_serial() const { return reply_serial_; }
  const std::string& path() const { return path_; }
  const std::string& interface() const { return interface_; }
  const std::string& member() const { return member_; }
  const std::string& error_name() const { return error_name_; }
  const std::string& destination() const { return destination_; }
  const std::string& sender() const { return sender_; }
  const std::string& signature() const { return body_.signature(); }

  // True if this is a method call the sender expects a reply to.
  bool expects_reply() const {
    return type_ == Type::kMethodCall && (flags_ & kNoReplyExpected) == 0;
  }

  // Writes the message body.
  DBusWriter& writer() { return body_; }

  // Reads the message body. The message must outlive the reader.
  DBusReader reader() const;

 private:
  DBusMessage() = default;

  Type type_ = Type::kInvalid;
  uint8_t flags_ = 0;
  bool big_endian_ = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;
  uint32_t serial_ = 0;
  uint32_t reply_serial_ = 0;
  std::string path_;
  std::string interface_;
  std::string member_;
  std::string error_name_;
  std::string destination_;
  std::string sender_;
  DBusWriter body_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DBUS_MESSAGE_H_
