// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_handler.h"

#include <string>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/json_message_codec.h"
#include "flutter/shell/platform/common/json_method_codec.h"

namespace flutter {

namespace {

constexpr char kChannelName[] = "flutter/platform";

constexpr char kGetClipboardDataMethod[] = "Clipboard.getData";
constexpr char kSetClipboardDataMethod[] = "Clipboard.setData";
constexpr char kClipboardHasStringsMethod[] = "Clipboard.hasStrings";
constexpr char kExitApplicationMethod[] = "System.exitApplication";
constexpr char kRequestAppExitMethod[] = "System.requestAppExit";
constexpr char kInitializationCompleteMethod[] =
    "System.initializationComplete";
constexpr char kPlaySoundMethod[] = "SystemSound.play";
constexpr char kSystemNavigatorPopMethod[] = "SystemNavigator.pop";
constexpr char kHapticFeedbackMethod[] = "HapticFeedback.vibrate";
// SystemChrome methods control mobile system UI, so have no effect.
constexpr char kSystemChromePrefix[] = "SystemChrome.";

constexpr char kTextPlainFormat[] = "text/plain";
constexpr char kTextKey[] = "text";
constexpr char kValueKey[] = "value";
constexpr char kExitTypeKey[] = "type";
constexpr char kExitTypeCancelable[] = "cancelable";
constexpr char kExitResponseKey[] = "response";
constexpr char kExitResponseCancel[] = "cancel";
constexpr char kExitResponseExit[] = "exit";
constexpr char kSoundTypeAlert[] = "SystemSoundType.alert";

constexpr char kBadArgumentsError[] = "Bad Arguments";
constexpr char kUnknownClipboardFormatError[] = "Unknown Clipboard Format";

std::vector<uint8_t> EncodeExitResponse(bool exit) {
  rapidjson::Document result(rapidjson::kObjectType);
  result.AddMember(
      kExitResponseKey,
      rapidjson::StringRef(exit ? kExitResponseExit : kExitResponseCancel),
      result.GetAllocator());
  return *JsonMethodCodec::GetInstance().EncodeSuccessEnvelope(&result);
}

}  // namespace

PlatformHandler::PlatformHandler(PlatformMessenger* messenger,
                                 Clipboard* clipboard,
                                 std::function<void()> on_exit,
                                 std::function<void()> on_alert)
    : messenger_(messenger),
      clipboard_(clipboard),
      on_exit_(std::move(on_exit)),
      on_alert_(std::move(on_alert)) {
  messenger_->SetMessageHandler(
      kChannelName, [this](const uint8_t* data, size_t size,
                           const PlatformMessenger::ResponseCallback& respond) {
        HandleMethodCall(data, size, respond);
      });
}

PlatformHandler::~PlatformHandler() {
  messenger_->SetMessageHandler(kChannelName, nullptr);
}

void PlatformHandler::RequestAppExit() {
  if (!initialization_complete_) {
    on_exit_();
    return;
  }
  SendRequestAppExit(kExitTypeCancelable, [this](bool exit) {
    if (exit) {
      on_exit_();
    }
  });
}

void PlatformHandler::HandleMethodCall(
    const uint8_t* data,
    size_t size,
    const PlatformMessenger::ResponseCallback& respond) {
  const JsonMethodCodec& codec = JsonMethodCodec::GetInstance();
  std::unique_ptr<MethodCall<rapidjson::Document>> call =
      codec.DecodeMethodCall(data, size);
  if (call == nullptr) {
    FML_LOG(ERROR) << "Failed to decode platform method call";
    respond({});
    return;
  }

  const std::string& method = call->method_name();
  const rapidjson::Document* args = call->arguments();

  if (method == kSetClipboardDataMethod) {
    if (args == nullptr || !args->IsObject() || !args->HasMember(kTextKey) ||
        !(*args)[kTextKey].IsString()) {
      respond(*codec.EncodeErrorEnvelope(kBadArgumentsError,
                                         "Expected text to set"));
      return;
    }
    if (clipboard_ == nullptr ||
        !clipboard_->SetText((*args)[kTextKey].GetString())) {
      FML_LOG(WARNING) << "Unable to set the clipboard";
    }
    respond(*codec.EncodeSuccessEnvelope());
  } else if (method == kGetClipboardDataMethod) {
    if (args == nullptr || !args->IsString()) {
      respond(*codec.EncodeErrorEnvelope(kBadArgumentsError,
                                         "Expected clipboard format"));
      return;
    }
    if (std::string(args->GetString()) != kTextPlainFormat) {
      respond(*codec.EncodeErrorEnvelope(kUnknownClipboardFormatError,
                                         "Only text is supported"));
      return;
    }
    if (clipboard_ == nullptr) {
      respond(*codec.EncodeSuccessEnvelope());
      return;
    }
    clipboard_->GetText([respond](std::optional<std::string> text) {
      const JsonMethodCodec& codec = JsonMethodCodec::GetInstance();
      if (!text.has_value()) {
        respond(*codec.EncodeSuccessEnvelope());
        return;
      }
      rapidjson::Document result(rapidjson::kObjectType);
      result.AddMember(
          kTextKey,
          rapidjson::Value(text->c_str(), text->size(), result.GetAllocator()),
          result.GetAllocator());
      respond(*codec.EncodeSuccessEnvelope(&result));
    });
  } else if (method == kClipboardHasStringsMethod) {
    rapidjson::Document result(rapidjson::kObjectType);
    result.AddMember(kValueKey, clipboard_ != nullptr && clipboard_->HasText(),
                     result.GetAllocator());
    respond(*codec.EncodeSuccessEnvelope(&result));
  } else if (method == kExitApplicationMethod) {
    std::string type;
    if (args != nullptr && args->IsObject() && args->HasMember(kExitTypeKey) &&
        (*args)[kExitTypeKey].IsString()) {
      type = (*args)[kExitTypeKey].GetString();
    }
    // Exits immediately if required, or if the framework can't be asked.
    if (type != kExitTypeCancelable || !initialization_complete_) {
      respond(EncodeExitResponse(true));
      on_exit_();
      return;
    }
    SendRequestAppExit(kExitTypeCancelable, [this, respond](bool exit) {
      respond(EncodeExitResponse(exit));
      if (exit) {
        on_exit_();
      }
    });
  } else if (method == kInitializationCompleteMethod) {
    initialization_complete_ = true;
    respond(*codec.EncodeSuccessEnvelope());
  } else if (method == kSystemNavigatorPopMethod) {
    respond(*codec.EncodeSuccessEnvelope());
    on_exit_();
  } else if (method == kPlaySoundMethod) {
    if (args != nullptr && args->IsString() &&
        std::string(args->GetString()) == kSoundTypeAlert) {
      on_alert_();
    }
    respond(*codec.EncodeSuccessEnvelope());
  } else if (method == kHapticFeedbackMethod ||
             method.starts_with(kSystemChromePrefix)) {
    respond(*codec.EncodeSuccessEnvelope());
  } else {
    respond({});
  }
}

void PlatformHandler::SendRequestAppExit(
    const char* type,
    std::function<void(bool exit)> callback) {
  auto args = std::make_unique<rapidjson::Document>(rapidjson::kObjectType);
  args->AddMember(kExitTypeKey, rapidjson::StringRef(type),
                  args->GetAllocator());
  MethodCall<rapidjson::Document> call(kRequestAppExitMethod, std::move(args));
  messenger_->SendMessage(
      kChannelName, *JsonMethodCodec::GetInstance().EncodeMethodCall(call),
      [alive = std::weak_ptr<bool>(alive_), callback = std::move(callback)](
          const uint8_t* data, size_t size) {
        if (alive.expired()) {
          return;
        }
        // The application exits unless the framework cancels it, so it can
        // always be closed.
        bool exit = true;
        if (data != nullptr && size > 0) {
          // A successful response is a single element array with the result.
          std::unique_ptr<rapidjson::Document> envelope =
              JsonMessageCodec::GetInstance().DecodeMessage(data, size);
          if (envelope != nullptr && envelope->IsArray() &&
              envelope->Size() == 1) {
            const rapidjson::Value& value = (*envelope)[0];
            if (value.IsObject() && value.HasMember(kExitResponseKey) &&
                value[kExitResponseKey].IsString()) {
              exit = std::string(value[kExitResponseKey].GetString()) !=
                     kExitResponseCancel;
            }
          }
        }
        callback(exit);
      });
}

}  // namespace flutter
