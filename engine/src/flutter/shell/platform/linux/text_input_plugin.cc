// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/text_input_plugin.h"

#include <set>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/json_method_codec.h"
#include "flutter/shell/platform/common/text_editing_delta.h"

namespace flutter {

namespace {

constexpr char kChannelName[] = "flutter/textinput";

constexpr char kSetClientMethod[] = "TextInput.setClient";
constexpr char kClearClientMethod[] = "TextInput.clearClient";
constexpr char kSetEditingStateMethod[] = "TextInput.setEditingState";
constexpr char kUpdateEditingStateMethod[] =
    "TextInputClient.updateEditingState";
constexpr char kUpdateEditingStateWithDeltasMethod[] =
    "TextInputClient.updateEditingStateWithDeltas";
constexpr char kPerformActionMethod[] = "TextInputClient.performAction";

// Methods that need no action, as there is no on-screen keyboard or input
// method.
const std::set<std::string> kIgnoredMethods = {
    "TextInput.show",
    "TextInput.hide",
    "TextInput.setEditableSizeAndTransform",
    "TextInput.setMarkedTextRect",
    "TextInput.setCaretRect",
    "TextInput.setSelectionRects",
    "TextInput.setStyle",
    "TextInput.requestAutofill",
    "TextInput.finishAutofillContext",
};

constexpr char kInputActionKey[] = "inputAction";
constexpr char kInputTypeKey[] = "inputType";
constexpr char kInputTypeNameKey[] = "name";
constexpr char kEnableDeltaModelKey[] = "enableDeltaModel";
constexpr char kTextKey[] = "text";
constexpr char kSelectionBaseKey[] = "selectionBase";
constexpr char kSelectionExtentKey[] = "selectionExtent";
constexpr char kSelectionAffinityKey[] = "selectionAffinity";
constexpr char kSelectionIsDirectionalKey[] = "selectionIsDirectional";
constexpr char kComposingBaseKey[] = "composingBase";
constexpr char kComposingExtentKey[] = "composingExtent";
constexpr char kDeltasKey[] = "deltas";
constexpr char kDeltaOldTextKey[] = "oldText";
constexpr char kDeltaTextKey[] = "deltaText";
constexpr char kDeltaStartKey[] = "deltaStart";
constexpr char kDeltaEndKey[] = "deltaEnd";

constexpr char kAffinityDownstream[] = "TextAffinity.downstream";
constexpr char kMultilineInputType[] = "TextInputType.multiline";
constexpr char kInputActionNewline[] = "TextInputAction.newline";

constexpr char kBadArgumentsError[] = "Bad Arguments";

// The keysyms for Enter.
constexpr uint32_t kReturnKeysym = 0xff0d;
constexpr uint32_t kKeypadEnterKeysym = 0xff8d;

// Modifiers that stop keys from typing, so they can be used in shortcuts.
constexpr uint32_t kControlMask = 1 << 2;
constexpr uint32_t kAltMask = 1 << 3;

// Returns the integer member |name| of |object|, or |fallback| if it is
// missing.
int GetInt(const rapidjson::Value& object, const char* name, int fallback) {
  auto it = object.FindMember(name);
  if (it == object.MemberEnd() || !it->value.IsInt()) {
    return fallback;
  }
  return it->value.GetInt();
}

// Returns the string member |name| of |object|, or an empty string if it is
// missing.
std::string GetString(const rapidjson::Value& object, const char* name) {
  auto it = object.FindMember(name);
  if (it == object.MemberEnd() || !it->value.IsString()) {
    return "";
  }
  return it->value.GetString();
}

}  // namespace

TextInputPlugin::TextInputPlugin(PlatformMessenger* messenger)
    : messenger_(messenger) {
  messenger_->SetMessageHandler(
      kChannelName, [this](const uint8_t* data, size_t size,
                           const PlatformMessenger::ResponseCallback& respond) {
        respond(HandleMethodCall(data, size));
      });
}

TextInputPlugin::~TextInputPlugin() {
  messenger_->SetMessageHandler(kChannelName, nullptr);
}

bool TextInputPlugin::HandleKey(const KeyboardKey& key,
                                const KeyModifiers& modifiers) {
  if (active_model_ == nullptr) {
    return false;
  }

  if (key.keysym == kReturnKeysym || key.keysym == kKeypadEnterKeysym) {
    EnterPressed();
    return true;
  }

  if ((modifiers.mask & (kControlMask | kAltMask)) != 0 ||
      !IsPrintable(key.text)) {
    return false;
  }

  AddText(CharacterToUtf8(key.text));
  return true;
}

std::vector<uint8_t> TextInputPlugin::HandleMethodCall(const uint8_t* data,
                                                       size_t size) {
  const JsonMethodCodec& codec = JsonMethodCodec::GetInstance();
  std::unique_ptr<MethodCall<rapidjson::Document>> call =
      codec.DecodeMethodCall(data, size);
  if (call == nullptr) {
    FML_LOG(ERROR) << "Failed to decode text input method call";
    return {};
  }

  const std::string& method = call->method_name();
  const rapidjson::Document* args = call->arguments();

  if (method == kSetClientMethod) {
    if (args == nullptr || !args->IsArray() || args->Size() < 2 ||
        !(*args)[0].IsInt() || !(*args)[1].IsObject()) {
      return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                        "Expected client ID and config");
    }
    const rapidjson::Value& config = (*args)[1];
    client_id_ = (*args)[0].GetInt();
    input_action_ = GetString(config, kInputActionKey);
    input_type_.clear();
    auto input_type = config.FindMember(kInputTypeKey);
    if (input_type != config.MemberEnd() && input_type->value.IsObject()) {
      input_type_ = GetString(input_type->value, kInputTypeNameKey);
    }
    auto enable_delta_model = config.FindMember(kEnableDeltaModelKey);
    enable_delta_model_ = enable_delta_model != config.MemberEnd() &&
                          enable_delta_model->value.IsBool() &&
                          enable_delta_model->value.GetBool();
    active_model_ = std::make_unique<TextInputModel>();
  } else if (method == kClearClientMethod) {
    active_model_.reset();
  } else if (method == kSetEditingStateMethod) {
    if (args == nullptr || !args->IsObject()) {
      return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                        "Expected editing state");
    }
    if (active_model_ != nullptr) {
      int selection_base = GetInt(*args, kSelectionBaseKey, -1);
      int selection_extent = GetInt(*args, kSelectionExtentKey, -1);
      if (selection_base == -1 && selection_extent == -1) {
        selection_base = selection_extent = 0;
      }
      int composing_base = GetInt(*args, kComposingBaseKey, -1);
      int composing_extent = GetInt(*args, kComposingExtentKey, -1);
      if (composing_base == -1 && composing_extent == -1) {
        composing_base = composing_extent = 0;
      }
      active_model_->SetText(GetString(*args, kTextKey),
                             TextRange(selection_base, selection_extent),
                             TextRange(composing_base, composing_extent));
    }
  } else if (kIgnoredMethods.count(method) == 0) {
    return {};
  }

  return *codec.EncodeSuccessEnvelope();
}

void TextInputPlugin::AddText(const std::string& text) {
  std::string text_before_change = active_model_->GetText();
  TextRange selection_before_change = active_model_->selection();
  active_model_->AddText(text);
  SendStateUpdate(
      TextEditingDelta(text_before_change, selection_before_change, text));
}

void TextInputPlugin::EnterPressed() {
  if (input_type_ == kMultilineInputType &&
      input_action_ == kInputActionNewline) {
    AddText("\n");
  }

  auto args = std::make_unique<rapidjson::Document>(rapidjson::kArrayType);
  auto& allocator = args->GetAllocator();
  args->PushBack(client_id_, allocator);
  args->PushBack(rapidjson::Value(input_action_, allocator).Move(), allocator);
  InvokeMethod(kPerformActionMethod, std::move(args));
}

void TextInputPlugin::SendStateUpdate(const TextEditingDelta& delta) {
  auto args = std::make_unique<rapidjson::Document>(rapidjson::kArrayType);
  auto& allocator = args->GetAllocator();
  args->PushBack(client_id_, allocator);

  rapidjson::Value state(rapidjson::kObjectType);
  if (enable_delta_model_) {
    state.AddMember(kDeltaOldTextKey,
                    rapidjson::Value(delta.old_text(), allocator).Move(),
                    allocator);
    state.AddMember(kDeltaTextKey,
                    rapidjson::Value(delta.delta_text(), allocator).Move(),
                    allocator);
    state.AddMember(kDeltaStartKey, delta.delta_start(), allocator);
    state.AddMember(kDeltaEndKey, delta.delta_end(), allocator);
  }

  TextRange selection = active_model_->selection();
  state.AddMember(kSelectionAffinityKey, kAffinityDownstream, allocator);
  state.AddMember(kSelectionBaseKey, static_cast<int>(selection.base()),
                  allocator);
  state.AddMember(kSelectionExtentKey, static_cast<int>(selection.extent()),
                  allocator);
  state.AddMember(kSelectionIsDirectionalKey, false, allocator);
  bool composing = active_model_->composing();
  TextRange composing_range = active_model_->composing_range();
  state.AddMember(kComposingBaseKey,
                  composing ? static_cast<int>(composing_range.base()) : -1,
                  allocator);
  state.AddMember(kComposingExtentKey,
                  composing ? static_cast<int>(composing_range.extent()) : -1,
                  allocator);

  if (enable_delta_model_) {
    rapidjson::Value deltas(rapidjson::kArrayType);
    deltas.PushBack(state, allocator);
    rapidjson::Value object(rapidjson::kObjectType);
    object.AddMember(kDeltasKey, deltas, allocator);
    args->PushBack(object, allocator);
    InvokeMethod(kUpdateEditingStateWithDeltasMethod, std::move(args));
  } else {
    state.AddMember(
        kTextKey, rapidjson::Value(active_model_->GetText(), allocator).Move(),
        allocator);
    args->PushBack(state, allocator);
    InvokeMethod(kUpdateEditingStateMethod, std::move(args));
  }
}

void TextInputPlugin::InvokeMethod(const std::string& method,
                                   std::unique_ptr<rapidjson::Document> args) {
  MethodCall<rapidjson::Document> call(method, std::move(args));
  std::unique_ptr<std::vector<uint8_t>> message =
      JsonMethodCodec::GetInstance().EncodeMethodCall(call);
  messenger_->SendMessage(kChannelName, *message, nullptr);
}

}  // namespace flutter
