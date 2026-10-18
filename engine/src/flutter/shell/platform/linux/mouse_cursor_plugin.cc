// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/mouse_cursor_plugin.h"

#include <map>
#include <string>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

namespace {

constexpr char kChannelName[] = "flutter/mousecursor";

constexpr char kActivateSystemCursorMethod[] = "activateSystemCursor";
constexpr char kKindKey[] = "kind";

constexpr char kBadArgumentsError[] = "Bad Arguments";

// Returns the cursor shape for a framework cursor kind, the names of
// SystemMouseCursors.
uint32_t ShapeForKind(const std::string& kind) {
  static const std::map<std::string, uint32_t> kShapes = {
      {"alias", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS},
      {"allScroll", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL},
      {"basic", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT},
      {"cell", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL},
      {"click", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER},
      {"contextMenu", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU},
      {"copy", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY},
      {"forbidden", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED},
      {"grab", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB},
      {"grabbing", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING},
      {"help", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP},
      {"move", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE},
      {"none", kHiddenCursorShape},
      {"noDrop", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP},
      {"precise", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR},
      {"progress", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS},
      {"resizeColumn", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE},
      {"resizeDown", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE},
      {"resizeDownLeft", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE},
      {"resizeDownRight", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE},
      {"resizeLeft", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE},
      {"resizeLeftRight", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE},
      {"resizeRight", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE},
      {"resizeRow", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE},
      {"resizeUp", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE},
      {"resizeUpDown", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE},
      {"resizeUpLeft", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE},
      {"resizeUpLeftDownRight", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE},
      {"resizeUpRight", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE},
      {"resizeUpRightDownLeft", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE},
      {"text", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT},
      {"verticalText", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT},
      {"wait", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT},
      {"zoomIn", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN},
      {"zoomOut", WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT},
  };
  auto it = kShapes.find(kind);
  return it != kShapes.end() ? it->second
                             : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
}

}  // namespace

MouseCursorPlugin::MouseCursorPlugin(PlatformMessenger* messenger,
                                     CursorCallback on_cursor)
    : messenger_(messenger),
      on_cursor_(std::move(on_cursor)),
      cursor_(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT) {
  messenger_->SetMessageHandler(
      kChannelName, [this](const uint8_t* data, size_t size,
                           const PlatformMessenger::ResponseCallback& respond) {
        respond(HandleMethodCall(data, size));
      });
}

MouseCursorPlugin::~MouseCursorPlugin() {
  messenger_->SetMessageHandler(kChannelName, nullptr);
}

std::vector<uint8_t> MouseCursorPlugin::HandleMethodCall(const uint8_t* data,
                                                         size_t size) {
  const StandardMethodCodec& codec = StandardMethodCodec::GetInstance();
  std::unique_ptr<MethodCall<EncodableValue>> call =
      codec.DecodeMethodCall(data, size);
  if (call == nullptr) {
    FML_LOG(ERROR) << "Failed to decode mouse cursor method call";
    return {};
  }

  if (call->method_name() != kActivateSystemCursorMethod) {
    return {};
  }

  const auto* args = std::get_if<EncodableMap>(call->arguments());
  if (args == nullptr) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected arguments map");
  }
  auto kind = args->find(EncodableValue(kKindKey));
  if (kind == args->end() ||
      !std::holds_alternative<std::string>(kind->second)) {
    return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                      "Expected cursor kind");
  }

  uint32_t shape = ShapeForKind(std::get<std::string>(kind->second));
  if (shape != cursor_) {
    cursor_ = shape;
    on_cursor_(cursor_);
  }
  return *codec.EncodeSuccessEnvelope();
}

}  // namespace flutter
