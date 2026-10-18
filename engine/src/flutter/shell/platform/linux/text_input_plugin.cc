// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/text_input_plugin.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "flutter/fml/logging.h"
#include "flutter/fml/string_conversion.h"
#include "flutter/shell/platform/common/json_method_codec.h"
#include "flutter/shell/platform/common/text_editing_delta.h"
#include "flutter/shell/platform/linux/protocols/text-input-unstable-v3-client-protocol.h"

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
constexpr char kSetEditableSizeAndTransformMethod[] =
    "TextInput.setEditableSizeAndTransform";
constexpr char kSetMarkedTextRectMethod[] = "TextInput.setMarkedTextRect";

// Methods that need no action. The input method is used whenever a text
// field has focus, and the marked text rectangle is used as the cursor
// position, as the framework sets it to the caret when nothing is composed.
const std::set<std::string> kIgnoredMethods = {
    "TextInput.show",
    "TextInput.hide",
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
constexpr char kObscureTextKey[] = "obscureText";
constexpr char kAutocorrectKey[] = "autocorrect";
constexpr char kEnableSuggestionsKey[] = "enableSuggestions";
constexpr char kTextCapitalizationKey[] = "textCapitalization";
constexpr char kDecimalKey[] = "decimal";
constexpr char kSignedKey[] = "signed";
constexpr char kTransformKey[] = "transform";
constexpr char kXKey[] = "x";
constexpr char kYKey[] = "y";
constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";
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
constexpr char kNumberInputType[] = "TextInputType.number";
constexpr char kPhoneInputType[] = "TextInputType.phone";
constexpr char kDatetimeInputType[] = "TextInputType.datetime";
constexpr char kEmailInputType[] = "TextInputType.emailAddress";
constexpr char kUrlInputType[] = "TextInputType.url";
constexpr char kVisiblePasswordInputType[] = "TextInputType.visiblePassword";
constexpr char kNameInputType[] = "TextInputType.name";
constexpr char kTerminalInputType[] = "TextInputType.terminal";
constexpr char kCapitalizeCharacters[] = "TextCapitalization.characters";
constexpr char kCapitalizeWords[] = "TextCapitalization.words";
constexpr char kCapitalizeSentences[] = "TextCapitalization.sentences";

// The most surrounding text the input method may be sent, in bytes.
constexpr size_t kMaxSurroundingTextLength = 4000;

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

// Returns the boolean member |name| of |object|, or |fallback| if it is
// missing.
bool GetBool(const rapidjson::Value& object, const char* name, bool fallback) {
  auto it = object.FindMember(name);
  if (it == object.MemberEnd() || !it->value.IsBool()) {
    return fallback;
  }
  return it->value.GetBool();
}

// Returns the number member |name| of |object|, or zero if it is missing.
double GetDouble(const rapidjson::Value& object, const char* name) {
  auto it = object.FindMember(name);
  if (it == object.MemberEnd() || !it->value.IsNumber()) {
    return 0;
  }
  return it->value.GetDouble();
}

// Returns the input method content type for a text field's configuration.
InputMethod::ContentType GetContentType(const rapidjson::Value& config,
                                        const std::string& input_type) {
  InputMethod::ContentType content_type;
  content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
  if (input_type == kNumberInputType) {
    auto it = config.FindMember(kInputTypeKey);
    bool is_decimal = it != config.MemberEnd() && it->value.IsObject() &&
                      GetBool(it->value, kDecimalKey, false);
    bool is_signed = it != config.MemberEnd() && it->value.IsObject() &&
                     GetBool(it->value, kSignedKey, false);
    content_type.purpose = is_decimal || is_signed
                               ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER
                               : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS;
  } else if (input_type == kPhoneInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE;
  } else if (input_type == kDatetimeInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME;
  } else if (input_type == kEmailInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
  } else if (input_type == kUrlInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL;
  } else if (input_type == kVisiblePasswordInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
  } else if (input_type == kNameInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NAME;
  } else if (input_type == kTerminalInputType) {
    content_type.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL;
  }

  if (input_type == kMultilineInputType) {
    content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;
  }
  if (GetBool(config, kObscureTextKey, false)) {
    content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |
                         ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
    content_type.purpose =
        content_type.purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS
            ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN
            : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
  } else {
    if (GetBool(config, kAutocorrectKey, true)) {
      content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK;
    }
    if (GetBool(config, kEnableSuggestionsKey, true)) {
      content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION;
    }
  }

  std::string capitalization = GetString(config, kTextCapitalizationKey);
  if (capitalization == kCapitalizeCharacters) {
    content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_UPPERCASE;
  } else if (capitalization == kCapitalizeWords) {
    content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_TITLECASE;
  } else if (capitalization == kCapitalizeSentences) {
    content_type.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
  }

  return content_type;
}

// Returns the length in UTF-16 code units of the first |length| bytes of
// the UTF-8 string |text|. A partial character at the end is excluded.
size_t Utf8ToUtf16Length(const std::string& text, size_t length) {
  length = std::min(length, text.size());
  while (length > 0 && length < text.size() &&
         (static_cast<uint8_t>(text[length]) & 0xc0) == 0x80) {
    length--;
  }
  return fml::Utf8ToUtf16(text.substr(0, length)).size();
}

// Returns the length in UTF-8 bytes of the first |length| code units of the
// UTF-16 string |text|.
size_t Utf16ToUtf8Length(const std::u16string& text, size_t length) {
  return fml::Utf16ToUtf8(text.substr(0, length)).size();
}

// Returns the smallest change that turns |old_text| into |new_text|.
TextEditingDelta DiffText(const std::u16string& old_text,
                          const std::u16string& new_text) {
  if (old_text == new_text) {
    return TextEditingDelta(old_text);
  }
  size_t max_common = std::min(old_text.size(), new_text.size());
  size_t prefix = 0;
  while (prefix < max_common && old_text[prefix] == new_text[prefix]) {
    prefix++;
  }
  size_t suffix = 0;
  while (suffix < max_common - prefix &&
         old_text[old_text.size() - 1 - suffix] ==
             new_text[new_text.size() - 1 - suffix]) {
    suffix++;
  }
  return TextEditingDelta(
      old_text, TextRange(prefix, old_text.size() - suffix),
      new_text.substr(prefix, new_text.size() - suffix - prefix));
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
    content_type_ = GetContentType(config, input_type_);
    active_model_ = std::make_unique<TextInputModel>();
    if (input_method_ != nullptr) {
      EnableInputMethod();
    }
  } else if (method == kClearClientMethod) {
    active_model_.reset();
    if (input_method_enabled_) {
      input_method_->Disable();
      input_method_->Commit();
      input_method_enabled_ = false;
    }
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
      if (input_method_enabled_) {
        SendInputMethodState(false);
      }
    }
  } else if (method == kSetEditableSizeAndTransformMethod) {
    if (args == nullptr || !args->IsObject()) {
      return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                        "Expected size and transform");
    }
    auto transform = args->FindMember(kTransformKey);
    if (transform == args->MemberEnd() || !transform->value.IsArray() ||
        transform->value.Size() != editable_transform_.size()) {
      return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                        "Expected 4x4 transform");
    }
    for (size_t i = 0; i < editable_transform_.size(); i++) {
      const rapidjson::Value& value = transform->value[i];
      editable_transform_[i] = value.IsNumber() ? value.GetDouble() : 0;
    }
    UpdateInputMethodCursor();
  } else if (method == kSetMarkedTextRectMethod) {
    if (args == nullptr || !args->IsObject()) {
      return *codec.EncodeErrorEnvelope(kBadArgumentsError,
                                        "Expected rectangle");
    }
    marked_text_rect_.x = GetDouble(*args, kXKey);
    marked_text_rect_.y = GetDouble(*args, kYKey);
    marked_text_rect_.width = GetDouble(*args, kWidthKey);
    marked_text_rect_.height = GetDouble(*args, kHeightKey);
    UpdateInputMethodCursor();
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

void TextInputPlugin::OnInputMethodEnter(InputMethod* input_method) {
  input_method_ = input_method;
  input_method_enabled_ = false;
  if (active_model_ != nullptr) {
    EnableInputMethod();
  }
}

void TextInputPlugin::OnInputMethodLeave() {
  input_method_ = nullptr;
  input_method_enabled_ = false;

  // The input method has forgotten the composing text, so remove it.
  if (active_model_ != nullptr && active_model_->composing()) {
    std::u16string old_text = fml::Utf8ToUtf16(active_model_->GetText());
    active_model_->UpdateComposingText(std::u16string());
    active_model_->EndComposing();
    SendStateUpdate(
        DiffText(old_text, fml::Utf8ToUtf16(active_model_->GetText())));
  }
}

void TextInputPlugin::OnInputMethodChange(const InputMethodChange& change) {
  if (active_model_ == nullptr || !input_method_enabled_) {
    return;
  }

  std::u16string old_text = fml::Utf8ToUtf16(active_model_->GetText());
  std::u16string text = old_text;
  size_t base = active_model_->selection().base();
  size_t extent = active_model_->selection().extent();

  // The changes are applied in the order the text-input-v3 protocol
  // requires. First the composing text is removed.
  if (active_model_->composing()) {
    TextRange composing = active_model_->composing_range();
    text.erase(composing.start(), composing.length());
    base = extent = composing.start();
  }

  // Then the surrounding text is deleted.
  size_t start = std::min(base, extent);
  size_t end = std::max(base, extent);
  if (change.delete_after > 0) {
    std::string after = fml::Utf16ToUtf8(text.substr(end));
    text.erase(end, Utf8ToUtf16Length(after, change.delete_after));
  }
  if (change.delete_before > 0) {
    std::string before = fml::Utf16ToUtf8(text.substr(0, start));
    size_t keep = Utf8ToUtf16Length(
        before,
        before.size() - std::min<size_t>(change.delete_before, before.size()));
    size_t count = start - keep;
    text.erase(keep, count);
    start -= count;
    end -= count;
    base -= count;
    extent -= count;
  }

  // Then the committed text replaces the selection.
  if (!change.commit.empty()) {
    std::u16string commit = fml::Utf8ToUtf16(change.commit);
    text.replace(start, end - start, commit);
    start = end = base = extent = start + commit.size();
  }

  // Then the new composing text replaces the selection, with the cursor
  // inside it.
  TextRange composing(0);
  if (!change.preedit.empty()) {
    std::u16string preedit = fml::Utf8ToUtf16(change.preedit);
    text.replace(start, end - start, preedit);
    composing = TextRange(start, start + preedit.size());
    if (change.preedit_cursor_begin < 0 || change.preedit_cursor_end < 0) {
      base = extent = composing.end();
    } else {
      base = start +
             Utf8ToUtf16Length(change.preedit, change.preedit_cursor_begin);
      extent =
          start + Utf8ToUtf16Length(change.preedit, change.preedit_cursor_end);
    }
  }

  TextRange selection(base, extent);
  bool changed = text != old_text || selection != active_model_->selection() ||
                 composing != (active_model_->composing()
                                   ? active_model_->composing_range()
                                   : TextRange(0));
  if (changed) {
    active_model_->SetText(fml::Utf16ToUtf8(text), selection, composing);
    SendStateUpdate(DiffText(old_text, text));
  }

  // The input method's state is only updated once it has seen the previous
  // state.
  if (!change.current) {
    input_method_state_dirty_ |= changed;
  } else if (changed || input_method_state_dirty_) {
    SendInputMethodState(true);
  }
}

void TextInputPlugin::EnableInputMethod() {
  input_method_->Enable();
  input_method_->SetContentType(content_type_);
  input_method_enabled_ = true;
  input_method_cursor_.reset();
  SendInputMethodState(false);
}

void TextInputPlugin::SendInputMethodState(bool from_input_method) {
  // The surrounding text excludes the composing text.
  std::u16string text = fml::Utf8ToUtf16(active_model_->GetText());
  size_t base = active_model_->selection().base();
  size_t extent = active_model_->selection().extent();
  if (active_model_->composing()) {
    TextRange composing = active_model_->composing_range();
    text.erase(composing.start(), composing.length());
    auto adjust = [&composing](size_t offset) {
      if (offset <= composing.start()) {
        return offset;
      }
      return offset >= composing.end() ? offset - composing.length()
                                       : composing.start();
    };
    base = adjust(base);
    extent = adjust(extent);
  }

  std::string surrounding = fml::Utf16ToUtf8(text);
  size_t anchor = Utf16ToUtf8Length(text, base);
  size_t cursor = Utf16ToUtf8Length(text, extent);

  // Long text is reduced to the part around the selection.
  if (surrounding.size() > kMaxSurroundingTextLength) {
    size_t low = std::min(anchor, cursor);
    size_t high = std::max(anchor, cursor);
    size_t margin = high - low < kMaxSurroundingTextLength
                        ? (kMaxSurroundingTextLength - (high - low)) / 2
                        : 0;
    size_t offset = low > margin ? low - margin : 0;
    offset = std::min(offset, surrounding.size() - kMaxSurroundingTextLength);
    while (offset < surrounding.size() &&
           (static_cast<uint8_t>(surrounding[offset]) & 0xc0) == 0x80) {
      offset++;
    }
    size_t length =
        std::min(kMaxSurroundingTextLength, surrounding.size() - offset);
    while (length > 0 && offset + length < surrounding.size() &&
           (static_cast<uint8_t>(surrounding[offset + length]) & 0xc0) ==
               0x80) {
      length--;
    }
    surrounding = surrounding.substr(offset, length);
    anchor = std::clamp(anchor, offset, offset + length) - offset;
    cursor = std::clamp(cursor, offset, offset + length) - offset;
  }

  input_method_->SetSurroundingText(surrounding, cursor, anchor,
                                    from_input_method);
  IntRect rect = GetCursorRectangle();
  input_method_->SetCursorRectangle(rect.x, rect.y, rect.width, rect.height);
  input_method_cursor_ = rect;
  input_method_->Commit();
  input_method_state_dirty_ = false;
}

void TextInputPlugin::UpdateInputMethodCursor() {
  if (!input_method_enabled_) {
    return;
  }
  IntRect rect = GetCursorRectangle();
  if (input_method_cursor_ == rect) {
    return;
  }
  input_method_->SetCursorRectangle(rect.x, rect.y, rect.width, rect.height);
  input_method_cursor_ = rect;
  input_method_->Commit();
}

TextInputPlugin::IntRect TextInputPlugin::GetCursorRectangle() const {
  const std::array<double, 16>& m = editable_transform_;
  double min_x = INFINITY;
  double min_y = INFINITY;
  double max_x = -INFINITY;
  double max_y = -INFINITY;
  const Rect& r = marked_text_rect_;
  for (auto [x, y] : {std::pair(r.x, r.y), std::pair(r.x + r.width, r.y),
                      std::pair(r.x, r.y + r.height),
                      std::pair(r.x + r.width, r.y + r.height)}) {
    double w = m[3] * x + m[7] * y + m[15];
    if (w == 0) {
      w = 1;
    }
    double tx = (m[0] * x + m[4] * y + m[12]) / w;
    double ty = (m[1] * x + m[5] * y + m[13]) / w;
    min_x = std::min(min_x, tx);
    min_y = std::min(min_y, ty);
    max_x = std::max(max_x, tx);
    max_y = std::max(max_y, ty);
  }
  IntRect rect;
  rect.x = static_cast<int32_t>(std::floor(min_x));
  rect.y = static_cast<int32_t>(std::floor(min_y));
  rect.width = static_cast<int32_t>(std::ceil(max_x)) - rect.x;
  rect.height = static_cast<int32_t>(std::ceil(max_y)) - rect.y;
  return rect;
}

void TextInputPlugin::InvokeMethod(const std::string& method,
                                   std::unique_ptr<rapidjson::Document> args) {
  MethodCall<rapidjson::Document> call(method, std::move(args));
  std::unique_ptr<std::vector<uint8_t>> message =
      JsonMethodCodec::GetInstance().EncodeMethodCall(call);
  messenger_->SendMessage(kChannelName, *message, nullptr);
}

}  // namespace flutter
