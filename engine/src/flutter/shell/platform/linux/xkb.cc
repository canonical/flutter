// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/xkb.h"

#include <xkbcommon/xkbcommon.h>

#include <string>

namespace flutter {

namespace {

// Bits in KeyModifiers::mask, which are the X11 modifier order XKB uses for
// its first eight modifiers.
constexpr uint32_t kRealModsMask = 0xff;

}  // namespace

uint32_t KeysymToUtf32(uint32_t keysym) {
  return xkb_keysym_to_utf32(keysym);
}

uint32_t KeysymToLower(uint32_t keysym) {
  return xkb_keysym_to_lower(keysym);
}

std::unique_ptr<XkbKeyboard> XkbKeyboard::Create(std::string_view keymap) {
  std::unique_ptr<XkbKeyboard> keyboard(new XkbKeyboard());

  // The keymap is complete, so no files or environment variables are needed.
  keyboard->context_ = xkb_context_new(static_cast<xkb_context_flags>(
      XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES));
  if (keyboard->context_ == nullptr) {
    return nullptr;
  }

  // The keymap may not be terminated, and may include a terminator in its
  // size.
  std::string text(keymap.substr(0, keymap.find('\0')));
  keyboard->keymap_ = xkb_keymap_new_from_string(
      keyboard->context_, text.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keyboard->keymap_ == nullptr) {
    return nullptr;
  }

  keyboard->state_ = xkb_state_new(keyboard->keymap_);
  if (keyboard->state_ == nullptr) {
    return nullptr;
  }

  return keyboard;
}

XkbKeyboard::~XkbKeyboard() {
  xkb_state_unref(state_);
  xkb_keymap_unref(keymap_);
  xkb_context_unref(context_);
}

void XkbKeyboard::UpdateState(uint32_t depressed_mods,
                              uint32_t latched_mods,
                              uint32_t locked_mods,
                              uint32_t group) {
  xkb_state_update_mask(state_, depressed_mods, latched_mods, locked_mods, 0, 0,
                        group);
}

uint32_t XkbKeyboard::GetKeysym(uint32_t keycode) const {
  return xkb_state_key_get_one_sym(state_, keycode);
}

uint32_t XkbKeyboard::GetText(uint32_t keycode) const {
  return xkb_state_key_get_utf32(state_, keycode);
}

uint32_t XkbKeyboard::GetLayout(uint32_t keycode) const {
  return xkb_state_key_get_layout(state_, keycode);
}

uint32_t XkbKeyboard::GetLevelKeysym(uint32_t keycode,
                                     uint32_t layout,
                                     uint32_t level) const {
  const xkb_keysym_t* syms = nullptr;
  int count =
      xkb_keymap_key_get_syms_by_level(keymap_, keycode, layout, level, &syms);
  return count > 0 ? syms[0] : 0;
}

bool XkbKeyboard::KeyRepeats(uint32_t keycode) const {
  return xkb_keymap_key_repeats(keymap_, keycode) != 0;
}

uint32_t XkbKeyboard::NumLayouts() const {
  return xkb_keymap_num_layouts(keymap_);
}

KeyModifiers XkbKeyboard::GetModifiers() const {
  KeyModifiers modifiers;
  modifiers.mask = xkb_state_serialize_mods(state_, XKB_STATE_MODS_EFFECTIVE) &
                   kRealModsMask;
  modifiers.caps_lock =
      xkb_state_led_name_is_active(state_, XKB_LED_NAME_CAPS) > 0;
  modifiers.num_lock =
      xkb_state_led_name_is_active(state_, XKB_LED_NAME_NUM) > 0;
  modifiers.scroll_lock =
      xkb_state_led_name_is_active(state_, XKB_LED_NAME_SCROLL) > 0;
  return modifiers;
}

}  // namespace flutter
