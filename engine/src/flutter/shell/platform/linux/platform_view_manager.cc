// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_view_manager.h"

#include <optional>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"

namespace flutter {

namespace {

constexpr char kChannelName[] = "flutter/platform_views";

constexpr char kCreateMethod[] = "create";
constexpr char kDisposeMethod[] = "dispose";
constexpr char kPointerMethod[] = "pointer";
constexpr char kClearFocusMethod[] = "clearFocus";

constexpr char kBadArgumentsError[] = "Bad Arguments";
constexpr char kCreateError[] = "error";

bool IsInt(const EncodableValue& value) {
  return std::holds_alternative<int32_t>(value) ||
         std::holds_alternative<int64_t>(value);
}

const EncodableValue* Lookup(const EncodableMap& map, const char* key) {
  auto it = map.find(EncodableValue(key));
  return it == map.end() ? nullptr : &it->second;
}

std::optional<int64_t> GetInt(const EncodableMap& map, const char* key) {
  const EncodableValue* value = Lookup(map, key);
  if (value == nullptr || !IsInt(*value)) {
    return std::nullopt;
  }
  return value->LongValue();
}

double GetDouble(const EncodableMap& map, const char* key) {
  const EncodableValue* value = Lookup(map, key);
  if (value == nullptr) {
    return 0;
  }
  if (const auto* d = std::get_if<double>(value)) {
    return *d;
  }
  if (IsInt(*value)) {
    return static_cast<double>(value->LongValue());
  }
  return 0;
}

std::string GetString(const EncodableMap& map, const char* key) {
  const EncodableValue* value = Lookup(map, key);
  if (value == nullptr) {
    return "";
  }
  const auto* s = std::get_if<std::string>(value);
  return s != nullptr ? *s : "";
}

std::optional<PlatformViewPointerEvent::Kind> ParseKind(
    const std::string& kind) {
  using Kind = PlatformViewPointerEvent::Kind;
  static const std::map<std::string, Kind> kinds = {
      {"down", Kind::kDown},     {"move", Kind::kMove},   {"up", Kind::kUp},
      {"cancel", Kind::kCancel}, {"hover", Kind::kHover}, {"exit", Kind::kExit},
      {"scroll", Kind::kScroll},
  };
  auto it = kinds.find(kind);
  if (it == kinds.end()) {
    return std::nullopt;
  }
  return it->second;
}

PlatformViewPointerEvent::DeviceKind ParseDeviceKind(const std::string& kind) {
  using DeviceKind = PlatformViewPointerEvent::DeviceKind;
  if (kind == "touch") {
    return DeviceKind::kTouch;
  }
  if (kind == "stylus") {
    return DeviceKind::kStylus;
  }
  if (kind == "trackpad") {
    return DeviceKind::kTrackpad;
  }
  return DeviceKind::kMouse;
}

}  // namespace

PlatformViewManager::PlatformViewManager(PlatformMessenger* messenger,
                                         ViewCreator create_view)
    : messenger_(messenger), create_view_(std::move(create_view)) {
  messenger_->SetMessageHandler(
      kChannelName, [this](const uint8_t* data, size_t size,
                           PlatformMessenger::ResponseCallback respond) {
        respond(HandleMethodCall(data, size));
      });
}

PlatformViewManager::~PlatformViewManager() {
  messenger_->SetMessageHandler(kChannelName, nullptr);
  DisposeAll();
  // Factories may hold plugin data, which is freed with them.
  factories_.clear();
}

void PlatformViewManager::RegisterViewType(const std::string& view_type,
                                           Factory factory) {
  factories_[view_type] = std::move(factory);
}

std::shared_ptr<LinuxPlatformView> PlatformViewManager::GetView(int64_t id) {
  std::scoped_lock lock(views_mutex_);
  auto it = views_.find(id);
  return it == views_.end() ? nullptr : it->second;
}

void PlatformViewManager::DisposeAll() {
  std::map<int64_t, std::shared_ptr<LinuxPlatformView>> views;
  {
    std::scoped_lock lock(views_mutex_);
    views.swap(views_);
  }
  for (auto& [id, view] : views) {
    view->Dispose();
  }
}

std::vector<uint8_t> PlatformViewManager::HandleMethodCall(const uint8_t* data,
                                                           size_t size) {
  const StandardMethodCodec& codec = StandardMethodCodec::GetInstance();
  std::unique_ptr<MethodCall<EncodableValue>> call =
      codec.DecodeMethodCall(data, size);
  if (call == nullptr) {
    FML_LOG(ERROR) << "Failed to decode platform view method call";
    return {};
  }

  const std::string& method = call->method_name();
  if (method == kCreateMethod) {
    return Create(call->arguments());
  }
  if (method == kDisposeMethod) {
    return Dispose(call->arguments());
  }
  if (method == kPointerMethod) {
    return DispatchPointerEvent(call->arguments());
  }
  if (method == kClearFocusMethod) {
    // Platform views don't take keyboard focus yet.
    return *codec.EncodeSuccessEnvelope();
  }
  return {};
}

std::vector<uint8_t> PlatformViewManager::Create(
    const EncodableValue* arguments) {
  const StandardMethodCodec& codec = StandardMethodCodec::GetInstance();
  const auto* args =
      arguments != nullptr ? std::get_if<EncodableMap>(arguments) : nullptr;
  if (args == nullptr) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected arguments map");
  }
  std::optional<int64_t> id = GetInt(*args, "id");
  std::string view_type = GetString(*args, "viewType");
  if (!id.has_value() || view_type.empty()) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected id and viewType");
  }
  auto factory = factories_.find(view_type);
  if (factory == factories_.end()) {
    return *codec.EncodeErrorEnvelope(
        kCreateError, "Unknown platform view type " + view_type);
  }
  {
    std::scoped_lock lock(views_mutex_);
    if (views_.count(*id) != 0) {
      return *codec.EncodeErrorEnvelope(
          kCreateError, "Platform view " + std::to_string(*id) + " exists");
    }
  }

  std::vector<uint8_t> params;
  if (const EncodableValue* value = Lookup(*args, "params")) {
    if (const auto* bytes = std::get_if<std::vector<uint8_t>>(value)) {
      params = *bytes;
    }
  }

  std::shared_ptr<LinuxPlatformView> view = create_view_(*id);
  if (view == nullptr) {
    return *codec.EncodeErrorEnvelope(
        kCreateError, "Platform views aren't supported on this display");
  }
  if (!factory->second(view.get(), params)) {
    view->Dispose();
    return *codec.EncodeErrorEnvelope(
        kCreateError, "Failed to create platform view of type " + view_type);
  }
  {
    std::scoped_lock lock(views_mutex_);
    views_[*id] = std::move(view);
  }
  return *codec.EncodeSuccessEnvelope();
}

std::vector<uint8_t> PlatformViewManager::Dispose(
    const EncodableValue* arguments) {
  const StandardMethodCodec& codec = StandardMethodCodec::GetInstance();
  if (arguments == nullptr || !IsInt(*arguments)) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError, "Expected view ID");
  }
  std::shared_ptr<LinuxPlatformView> view;
  {
    std::scoped_lock lock(views_mutex_);
    auto it = views_.find(arguments->LongValue());
    if (it != views_.end()) {
      view = std::move(it->second);
      views_.erase(it);
    }
  }
  if (view != nullptr) {
    view->Dispose();
  }
  return *codec.EncodeSuccessEnvelope();
}

std::vector<uint8_t> PlatformViewManager::DispatchPointerEvent(
    const EncodableValue* arguments) {
  const StandardMethodCodec& codec = StandardMethodCodec::GetInstance();
  const auto* args =
      arguments != nullptr ? std::get_if<EncodableMap>(arguments) : nullptr;
  if (args == nullptr) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected arguments map");
  }
  std::optional<int64_t> id = GetInt(*args, "id");
  std::optional<PlatformViewPointerEvent::Kind> kind =
      ParseKind(GetString(*args, "kind"));
  if (!id.has_value() || !kind.has_value()) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected id and kind");
  }
  std::shared_ptr<LinuxPlatformView> view = GetView(*id);
  if (view == nullptr) {
    return *codec.EncodeSuccessEnvelope();
  }

  PlatformViewPointerEvent event;
  event.kind = *kind;
  event.device_kind = ParseDeviceKind(GetString(*args, "deviceKind"));
  event.device = static_cast<int32_t>(GetInt(*args, "device").value_or(0));
  event.buttons = GetInt(*args, "buttons").value_or(0);
  event.x = GetDouble(*args, "x");
  event.y = GetDouble(*args, "y");
  event.scroll_delta_x = GetDouble(*args, "scrollDeltaX");
  event.scroll_delta_y = GetDouble(*args, "scrollDeltaY");
  event.timestamp_us = GetInt(*args, "timestamp").value_or(0);
  view->DispatchPointerEvent(event);
  return *codec.EncodeSuccessEnvelope();
}

}  // namespace flutter
