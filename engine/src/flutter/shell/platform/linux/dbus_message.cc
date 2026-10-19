// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dbus_message.h"

#include <cstring>
#include <utility>

namespace flutter {

namespace {

constexpr bool kHostBigEndian = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;

// Limits from the D-Bus specification.
constexpr size_t kMaxArrayLength = 64 * 1024 * 1024;
constexpr size_t kMaxMessageLength = 128 * 1024 * 1024;
constexpr size_t kMaxSignatureDepth = 64;

constexpr uint8_t kFieldPath = 1;
constexpr uint8_t kFieldInterface = 2;
constexpr uint8_t kFieldMember = 3;
constexpr uint8_t kFieldErrorName = 4;
constexpr uint8_t kFieldReplySerial = 5;
constexpr uint8_t kFieldDestination = 6;
constexpr uint8_t kFieldSender = 7;
constexpr uint8_t kFieldSignature = 8;
constexpr uint8_t kFieldUnixFds = 9;

size_t AlignmentOf(char type) {
  switch (type) {
    case 'n':
    case 'q':
      return 2;
    case 'b':
    case 'i':
    case 'u':
    case 's':
    case 'o':
    case 'a':
    case 'h':
      return 4;
    case 'x':
    case 't':
    case 'd':
    case '(':
    case '{':
      return 8;
    default:
      return 1;
  }
}

bool IsBasicType(char type) {
  return std::strchr("ybnqiuxtdsogh", type) != nullptr && type != '\0';
}

size_t CompleteTypeLength(std::string_view signature, size_t depth) {
  if (signature.empty() || depth > kMaxSignatureDepth) {
    return 0;
  }
  char type = signature[0];
  if (IsBasicType(type) || type == 'v') {
    return 1;
  }
  if (type == 'a') {
    size_t length = CompleteTypeLength(signature.substr(1), depth + 1);
    return length == 0 ? 0 : length + 1;
  }
  if (type == '(') {
    size_t offset = 1;
    while (offset < signature.size() && signature[offset] != ')') {
      size_t length = CompleteTypeLength(signature.substr(offset), depth + 1);
      if (length == 0) {
        return 0;
      }
      offset += length;
    }
    // Structs must have at least one member.
    if (offset >= signature.size() || offset == 1) {
      return 0;
    }
    return offset + 1;
  }
  if (type == '{') {
    // A basic key and a single value.
    if (signature.size() < 4 || !IsBasicType(signature[1])) {
      return 0;
    }
    size_t length = CompleteTypeLength(signature.substr(2), depth + 1);
    if (length == 0 || 2 + length >= signature.size() ||
        signature[2 + length] != '}') {
      return 0;
    }
    return length + 3;
  }
  return 0;
}

// True if |signature| is a sequence of complete types.
bool IsValidSignature(std::string_view signature) {
  if (signature.size() > 255) {
    return false;
  }
  while (!signature.empty()) {
    size_t length = DBusCompleteTypeLength(signature);
    // Dictionary entries may only appear in arrays.
    if (length == 0 || signature[0] == '{') {
      return false;
    }
    signature.remove_prefix(length);
  }
  return true;
}

template <typename T>
T ByteSwap(T value) {
  uint8_t bytes[sizeof(T)];
  std::memcpy(bytes, &value, sizeof(T));
  for (size_t i = 0; i < sizeof(T) / 2; i++) {
    std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
  }
  std::memcpy(&value, bytes, sizeof(T));
  return value;
}

uint32_t ReadUInt32At(const uint8_t* data, bool big_endian) {
  uint32_t value;
  std::memcpy(&value, data, sizeof(value));
  return big_endian != kHostBigEndian ? ByteSwap(value) : value;
}

size_t AlignUp(size_t value, size_t alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

size_t DBusCompleteTypeLength(std::string_view signature) {
  return CompleteTypeLength(signature, 0);
}

DBusWriter::DBusWriter(std::vector<uint8_t> data, std::string signature)
    : data_(std::move(data)), signature_(std::move(signature)) {}

void DBusWriter::AddType(std::string_view type) {
  if (nesting_ == 0) {
    signature_ += type;
  }
}

void DBusWriter::Align(size_t alignment) {
  while (data_.size() % alignment != 0) {
    data_.push_back(0);
  }
}

template <typename T>
void DBusWriter::WriteFixed(T value, size_t alignment) {
  Align(alignment);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
  data_.insert(data_.end(), bytes, bytes + sizeof(T));
}

void DBusWriter::WriteByte(uint8_t value) {
  AddType("y");
  data_.push_back(value);
}

void DBusWriter::WriteBoolean(bool value) {
  AddType("b");
  WriteFixed<uint32_t>(value ? 1 : 0, 4);
}

void DBusWriter::WriteInt16(int16_t value) {
  AddType("n");
  WriteFixed(value, 2);
}

void DBusWriter::WriteUInt16(uint16_t value) {
  AddType("q");
  WriteFixed(value, 2);
}

void DBusWriter::WriteInt32(int32_t value) {
  AddType("i");
  WriteFixed(value, 4);
}

void DBusWriter::WriteUInt32(uint32_t value) {
  AddType("u");
  WriteFixed(value, 4);
}

void DBusWriter::WriteInt64(int64_t value) {
  AddType("x");
  WriteFixed(value, 8);
}

void DBusWriter::WriteUInt64(uint64_t value) {
  AddType("t");
  WriteFixed(value, 8);
}

void DBusWriter::WriteDouble(double value) {
  AddType("d");
  WriteFixed(value, 8);
}

void DBusWriter::WriteString(std::string_view value) {
  AddType("s");
  WriteFixed(static_cast<uint32_t>(value.size()), 4);
  data_.insert(data_.end(), value.begin(), value.end());
  data_.push_back(0);
}

void DBusWriter::WriteObjectPath(std::string_view value) {
  AddType("o");
  WriteFixed(static_cast<uint32_t>(value.size()), 4);
  data_.insert(data_.end(), value.begin(), value.end());
  data_.push_back(0);
}

void DBusWriter::WriteSignature(std::string_view value) {
  AddType("g");
  data_.push_back(static_cast<uint8_t>(value.size()));
  data_.insert(data_.end(), value.begin(), value.end());
  data_.push_back(0);
}

void DBusWriter::OpenArray(std::string_view element_signature) {
  AddType("a");
  AddType(element_signature);
  WriteFixed<uint32_t>(0, 4);
  size_t length_offset = data_.size() - sizeof(uint32_t);
  Align(AlignmentOf(element_signature.empty() ? 'y' : element_signature[0]));
  arrays_.emplace_back(length_offset, data_.size());
  nesting_++;
}

void DBusWriter::CloseArray() {
  auto [length_offset, start] = arrays_.back();
  arrays_.pop_back();
  nesting_--;
  uint32_t length = static_cast<uint32_t>(data_.size() - start);
  std::memcpy(data_.data() + length_offset, &length, sizeof(length));
}

void DBusWriter::OpenStruct() {
  AddType("(");
  Align(8);
}

void DBusWriter::CloseStruct() {
  AddType(")");
}

void DBusWriter::OpenDictEntry() {
  Align(8);
}

void DBusWriter::CloseDictEntry() {}

void DBusWriter::OpenVariant(std::string_view signature) {
  AddType("v");
  nesting_++;
  data_.push_back(static_cast<uint8_t>(signature.size()));
  data_.insert(data_.end(), signature.begin(), signature.end());
  data_.push_back(0);
}

void DBusWriter::CloseVariant() {
  nesting_--;
}

DBusReader::DBusReader(const uint8_t* data,
                       size_t size,
                       std::string_view signature,
                       bool big_endian,
                       size_t offset)
    : data_(data), big_endian_(big_endian), offset_(offset) {
  containers_.push_back({'\0', signature, 0, size});
}

bool DBusReader::Fail() {
  ok_ = false;
  return false;
}

char DBusReader::PeekType() const {
  if (!ok_) {
    return '\0';
  }
  const Container& container = containers_.back();
  if (container.type == 'a' && offset_ >= container.end) {
    return '\0';
  }
  if (container.signature_offset >= container.signature.size()) {
    return '\0';
  }
  return container.signature[container.signature_offset];
}

void DBusReader::ConsumeType(size_t length) {
  Container& container = containers_.back();
  container.signature_offset += length;
  // Array elements repeat the element signature.
  if (container.type == 'a' &&
      container.signature_offset >= container.signature.size()) {
    container.signature_offset = 0;
  }
}

bool DBusReader::Align(size_t alignment) {
  size_t aligned = AlignUp(offset_, alignment);
  if (aligned > containers_.back().end) {
    return Fail();
  }
  offset_ = aligned;
  return true;
}

template <typename T>
bool DBusReader::ReadFixed(char type, T* value) {
  if (PeekType() != type || !Align(AlignmentOf(type)) ||
      offset_ + sizeof(T) > containers_.back().end) {
    return Fail();
  }
  std::memcpy(value, data_ + offset_, sizeof(T));
  if (big_endian_ != kHostBigEndian) {
    *value = ByteSwap(*value);
  }
  offset_ += sizeof(T);
  ConsumeType(1);
  return true;
}

bool DBusReader::ReadByte(uint8_t* value) {
  return ReadFixed('y', value);
}

bool DBusReader::ReadBoolean(bool* value) {
  uint32_t v;
  if (!ReadFixed('b', &v) || v > 1) {
    return Fail();
  }
  *value = v == 1;
  return true;
}

bool DBusReader::ReadInt16(int16_t* value) {
  return ReadFixed('n', value);
}

bool DBusReader::ReadUInt16(uint16_t* value) {
  return ReadFixed('q', value);
}

bool DBusReader::ReadInt32(int32_t* value) {
  return ReadFixed('i', value);
}

bool DBusReader::ReadUInt32(uint32_t* value) {
  return ReadFixed('u', value);
}

bool DBusReader::ReadInt64(int64_t* value) {
  return ReadFixed('x', value);
}

bool DBusReader::ReadUInt64(uint64_t* value) {
  return ReadFixed('t', value);
}

bool DBusReader::ReadDouble(double* value) {
  return ReadFixed('d', value);
}

bool DBusReader::ReadStringLike(char type, std::string* value) {
  if (PeekType() != type) {
    return Fail();
  }
  size_t end = containers_.back().end;
  size_t length;
  if (type == 'g') {
    if (offset_ + 1 > end) {
      return Fail();
    }
    length = data_[offset_];
    offset_ += 1;
  } else {
    if (!Align(4) || offset_ + 4 > end) {
      return Fail();
    }
    length = ReadUInt32At(data_ + offset_, big_endian_);
    offset_ += 4;
  }
  if (length >= end - offset_ || data_[offset_ + length] != 0) {
    return Fail();
  }
  value->assign(reinterpret_cast<const char*>(data_ + offset_), length);
  offset_ += length + 1;
  ConsumeType(1);
  return true;
}

bool DBusReader::ReadString(std::string* value) {
  return ReadStringLike('s', value);
}

bool DBusReader::ReadObjectPath(std::string* value) {
  return ReadStringLike('o', value);
}

bool DBusReader::ReadSignature(std::string* value) {
  return ReadStringLike('g', value);
}

bool DBusReader::OpenArray() {
  if (PeekType() != 'a' || containers_.size() > kMaxSignatureDepth) {
    return Fail();
  }
  const Container& parent = containers_.back();
  std::string_view element =
      parent.signature.substr(parent.signature_offset + 1);
  size_t element_length = DBusCompleteTypeLength(element);
  if (element_length == 0) {
    return Fail();
  }
  element = element.substr(0, element_length);
  if (!Align(4) || offset_ + 4 > parent.end) {
    return Fail();
  }
  size_t length = ReadUInt32At(data_ + offset_, big_endian_);
  offset_ += 4;
  if (length > kMaxArrayLength || !Align(AlignmentOf(element[0])) ||
      length > parent.end - offset_) {
    return Fail();
  }
  size_t end = offset_ + length;
  ConsumeType(1 + element_length);
  containers_.push_back({'a', element, 0, end});
  return true;
}

bool DBusReader::CloseArray() {
  if (!ok_ || containers_.back().type != 'a') {
    return Fail();
  }
  offset_ = containers_.back().end;
  containers_.pop_back();
  return true;
}

bool DBusReader::OpenAggregate(char open) {
  if (PeekType() != open || containers_.size() > kMaxSignatureDepth) {
    return Fail();
  }
  const Container& parent = containers_.back();
  std::string_view signature = parent.signature.substr(parent.signature_offset);
  size_t length = DBusCompleteTypeLength(signature);
  if (length < 2 || !Align(8)) {
    return Fail();
  }
  size_t end = parent.end;
  ConsumeType(length);
  containers_.push_back({open, signature.substr(1, length - 2), 0, end});
  return true;
}

bool DBusReader::CloseContainer(char type) {
  if (!ok_ || containers_.size() < 2 || containers_.back().type != type) {
    return Fail();
  }
  while (HasMore()) {
    if (!Skip()) {
      return false;
    }
  }
  containers_.pop_back();
  return ok_;
}

bool DBusReader::OpenStruct() {
  return OpenAggregate('(');
}

bool DBusReader::CloseStruct() {
  return CloseContainer('(');
}

bool DBusReader::OpenDictEntry() {
  return OpenAggregate('{');
}

bool DBusReader::CloseDictEntry() {
  return CloseContainer('{');
}

bool DBusReader::OpenVariant(std::string* signature) {
  if (PeekType() != 'v' || containers_.size() > kMaxSignatureDepth) {
    return Fail();
  }
  size_t end = containers_.back().end;
  if (offset_ + 1 > end) {
    return Fail();
  }
  size_t length = data_[offset_];
  if (length + 2 > end - offset_ || data_[offset_ + 1 + length] != 0) {
    return Fail();
  }
  std::string_view value_signature(
      reinterpret_cast<const char*>(data_ + offset_ + 1), length);
  if (DBusCompleteTypeLength(value_signature) != length) {
    return Fail();
  }
  offset_ += length + 2;
  ConsumeType(1);
  if (signature != nullptr) {
    *signature = std::string(value_signature);
  }
  containers_.push_back({'v', value_signature, 0, end});
  return true;
}

bool DBusReader::CloseVariant() {
  return CloseContainer('v');
}

bool DBusReader::Skip() {
  switch (PeekType()) {
    case 'y': {
      uint8_t v;
      return ReadByte(&v);
    }
    case 'b': {
      bool v;
      return ReadBoolean(&v);
    }
    case 'n': {
      int16_t v;
      return ReadInt16(&v);
    }
    case 'q': {
      uint16_t v;
      return ReadUInt16(&v);
    }
    case 'i': {
      int32_t v;
      return ReadInt32(&v);
    }
    case 'u': {
      uint32_t v;
      return ReadUInt32(&v);
    }
    case 'h': {
      uint32_t v;
      return ReadFixed('h', &v);
    }
    case 'x': {
      int64_t v;
      return ReadInt64(&v);
    }
    case 't': {
      uint64_t v;
      return ReadUInt64(&v);
    }
    case 'd': {
      double v;
      return ReadDouble(&v);
    }
    case 's':
    case 'o':
    case 'g': {
      std::string v;
      return ReadStringLike(PeekType(), &v);
    }
    case 'a':
      return OpenArray() && CloseArray();
    case '(':
      return OpenStruct() && CloseStruct();
    case '{':
      return OpenDictEntry() && CloseDictEntry();
    case 'v':
      return OpenVariant() && CloseVariant();
    default:
      return Fail();
  }
}

DBusMessage DBusMessage::MethodCall(std::string destination,
                                    std::string path,
                                    std::string interface,
                                    std::string member) {
  DBusMessage message;
  message.type_ = Type::kMethodCall;
  message.destination_ = std::move(destination);
  message.path_ = std::move(path);
  message.interface_ = std::move(interface);
  message.member_ = std::move(member);
  return message;
}

DBusMessage DBusMessage::Signal(std::string path,
                                std::string interface,
                                std::string member) {
  DBusMessage message;
  message.type_ = Type::kSignal;
  message.path_ = std::move(path);
  message.interface_ = std::move(interface);
  message.member_ = std::move(member);
  return message;
}

DBusMessage DBusMessage::MethodReturn(const DBusMessage& call) {
  DBusMessage message;
  message.type_ = Type::kMethodReturn;
  message.flags_ = kNoReplyExpected;
  message.reply_serial_ = call.serial_;
  message.destination_ = call.sender_;
  return message;
}

DBusMessage DBusMessage::Error(const DBusMessage& call,
                               std::string name,
                               std::string_view text) {
  DBusMessage message;
  message.type_ = Type::kError;
  message.flags_ = kNoReplyExpected;
  message.reply_serial_ = call.serial_;
  message.destination_ = call.sender_;
  message.error_name_ = std::move(name);
  if (!text.empty()) {
    message.body_.WriteString(text);
  }
  return message;
}

size_t DBusMessage::GetLength(const uint8_t* data, size_t size) {
  if (size < 16 || (data[0] != 'l' && data[0] != 'B') || data[3] != 1) {
    return 0;
  }
  bool big_endian = data[0] == 'B';
  size_t body_length = ReadUInt32At(data + 4, big_endian);
  size_t fields_length = ReadUInt32At(data + 12, big_endian);
  if (body_length > kMaxMessageLength || fields_length > kMaxArrayLength) {
    return 0;
  }
  size_t length = AlignUp(16 + fields_length, 8) + body_length;
  return length > kMaxMessageLength ? 0 : length;
}

std::optional<DBusMessage> DBusMessage::Parse(const uint8_t* data,
                                              size_t size) {
  size_t length = GetLength(data, size);
  if (length == 0 || length != size) {
    return std::nullopt;
  }

  DBusMessage message;
  message.big_endian_ = data[0] == 'B';
  message.type_ = static_cast<Type>(data[1]);
  message.flags_ = data[2];
  message.serial_ = ReadUInt32At(data + 8, message.big_endian_);
  if (message.type_ < Type::kMethodCall || message.type_ > Type::kSignal ||
      message.serial_ == 0) {
    return std::nullopt;
  }

  size_t fields_end = 16 + ReadUInt32At(data + 12, message.big_endian_);
  DBusReader reader(data, fields_end, "a(yv)", message.big_endian_, 12);
  std::string signature;
  uint32_t unix_fds = 0;
  reader.OpenArray();
  while (reader.HasMore()) {
    uint8_t code;
    std::string field_signature;
    if (!reader.OpenStruct() || !reader.ReadByte(&code) ||
        !reader.OpenVariant(&field_signature)) {
      return std::nullopt;
    }
    auto read_string = [&](const char* type, std::string* value) {
      return field_signature == type &&
             (type[0] == 'o'   ? reader.ReadObjectPath(value)
              : type[0] == 'g' ? reader.ReadSignature(value)
                               : reader.ReadString(value));
    };
    bool valid = true;
    switch (code) {
      case kFieldPath:
        valid = read_string("o", &message.path_);
        break;
      case kFieldInterface:
        valid = read_string("s", &message.interface_);
        break;
      case kFieldMember:
        valid = read_string("s", &message.member_);
        break;
      case kFieldErrorName:
        valid = read_string("s", &message.error_name_);
        break;
      case kFieldReplySerial:
        valid =
            field_signature == "u" && reader.ReadUInt32(&message.reply_serial_);
        break;
      case kFieldDestination:
        valid = read_string("s", &message.destination_);
        break;
      case kFieldSender:
        valid = read_string("s", &message.sender_);
        break;
      case kFieldSignature:
        valid = read_string("g", &signature);
        break;
      case kFieldUnixFds:
        valid = field_signature == "u" && reader.ReadUInt32(&unix_fds);
        break;
      default:
        break;
    }
    if (!valid || !reader.CloseVariant() || !reader.CloseStruct()) {
      return std::nullopt;
    }
  }
  // File descriptors are not negotiated, so can't be received.
  if (!reader.CloseArray() || unix_fds != 0 || !IsValidSignature(signature)) {
    return std::nullopt;
  }

  switch (message.type_) {
    case Type::kMethodCall:
      if (message.path_.empty() || message.member_.empty()) {
        return std::nullopt;
      }
      break;
    case Type::kSignal:
      if (message.path_.empty() || message.interface_.empty() ||
          message.member_.empty()) {
        return std::nullopt;
      }
      break;
    case Type::kError:
      if (message.error_name_.empty() || message.reply_serial_ == 0) {
        return std::nullopt;
      }
      break;
    case Type::kMethodReturn:
      if (message.reply_serial_ == 0) {
        return std::nullopt;
      }
      break;
    default:
      return std::nullopt;
  }

  size_t body_start = AlignUp(fields_end, 8);
  message.body_ = DBusWriter(
      std::vector<uint8_t>(data + body_start, data + size), signature);
  return message;
}

std::vector<uint8_t> DBusMessage::Serialize(uint32_t serial) const {
  DBusWriter header;
  header.WriteByte(kHostBigEndian ? 'B' : 'l');
  header.WriteByte(static_cast<uint8_t>(type_));
  header.WriteByte(flags_);
  header.WriteByte(1);
  header.WriteUInt32(static_cast<uint32_t>(body_.data().size()));
  header.WriteUInt32(serial);

  header.OpenArray("(yv)");
  auto write_field = [&header](uint8_t code, const char* type,
                               const std::string& value) {
    if (value.empty()) {
      return;
    }
    header.OpenStruct();
    header.WriteByte(code);
    header.OpenVariant(type);
    if (type[0] == 'o') {
      header.WriteObjectPath(value);
    } else if (type[0] == 'g') {
      header.WriteSignature(value);
    } else {
      header.WriteString(value);
    }
    header.CloseVariant();
    header.CloseStruct();
  };
  write_field(kFieldPath, "o", path_);
  write_field(kFieldInterface, "s", interface_);
  write_field(kFieldMember, "s", member_);
  write_field(kFieldErrorName, "s", error_name_);
  if (reply_serial_ != 0) {
    header.OpenStruct();
    header.WriteByte(kFieldReplySerial);
    header.OpenVariant("u");
    header.WriteUInt32(reply_serial_);
    header.CloseVariant();
    header.CloseStruct();
  }
  write_field(kFieldDestination, "s", destination_);
  write_field(kFieldSender, "s", sender_);
  write_field(kFieldSignature, "g", body_.signature());
  header.CloseArray();
  header.Align(8);

  std::vector<uint8_t> data = header.data();
  data.insert(data.end(), body_.data().begin(), body_.data().end());
  return data;
}

DBusReader DBusMessage::reader() const {
  return DBusReader(body_.data().data(), body_.data().size(), body_.signature(),
                    big_endian_);
}

}  // namespace flutter
