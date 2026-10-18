// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/xkb.h"

#include <dlfcn.h>

#include <cstdio>
#include <string>
#include <type_traits>

namespace flutter {

namespace {

// Values from xkbcommon.h.
constexpr int kContextNoDefaultIncludes = 1 << 0;
constexpr int kContextNoEnvironmentNames = 1 << 1;
constexpr int kKeymapFormatTextV1 = 1;
constexpr int kStateModsEffective = 1 << 3;

// Bits in KeyModifiers::mask, which are the X11 modifier order XKB uses for
// its first eight modifiers.
constexpr uint32_t kRealModsMask = 0xff;

// Loads the functions from libxkbcommon. Returns false if any are missing.
bool Load(XkbFunctions* functions) {
  void* library = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    return false;
  }

  bool ok = true;
  auto load = [&](auto& function, const char* name) {
    function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
        dlsym(library, name));
    ok = ok && function != nullptr;
  };
  load(functions->context_new, "xkb_context_new");
  load(functions->context_unref, "xkb_context_unref");
  load(functions->keymap_new_from_string, "xkb_keymap_new_from_string");
  load(functions->keymap_unref, "xkb_keymap_unref");
  load(functions->keymap_key_get_syms_by_level,
       "xkb_keymap_key_get_syms_by_level");
  load(functions->keymap_key_repeats, "xkb_keymap_key_repeats");
  load(functions->keymap_num_layouts, "xkb_keymap_num_layouts");
  load(functions->state_new, "xkb_state_new");
  load(functions->state_unref, "xkb_state_unref");
  load(functions->state_update_mask, "xkb_state_update_mask");
  load(functions->state_key_get_one_sym, "xkb_state_key_get_one_sym");
  load(functions->state_key_get_utf32, "xkb_state_key_get_utf32");
  load(functions->state_key_get_layout, "xkb_state_key_get_layout");
  load(functions->state_serialize_mods, "xkb_state_serialize_mods");
  load(functions->state_led_name_is_active, "xkb_state_led_name_is_active");
  load(functions->keysym_to_utf32, "xkb_keysym_to_utf32");
  load(functions->keysym_to_lower, "xkb_keysym_to_lower");

  // The library is kept loaded for the lifetime of the process.
  if (!ok) {
    dlclose(library);
  }
  return ok;
}

}  // namespace

const XkbFunctions* XkbFunctions::Get() {
  static const XkbFunctions* functions = []() -> const XkbFunctions* {
    static XkbFunctions loaded = {};
    if (!Load(&loaded)) {
      fprintf(stderr,
              "Warning: libxkbcommon is not available, keyboard input is "
              "disabled\n");
      return nullptr;
    }
    return &loaded;
  }();
  return functions;
}

std::unique_ptr<XkbKeyboard> XkbKeyboard::Create(const XkbFunctions* xkb,
                                                 std::string_view keymap) {
  std::unique_ptr<XkbKeyboard> keyboard(new XkbKeyboard(xkb));
  keyboard->context_ =
      xkb->context_new(kContextNoDefaultIncludes | kContextNoEnvironmentNames);
  if (keyboard->context_ == nullptr) {
    return nullptr;
  }

  // The keymap may not be terminated, and may include a terminator in its
  // size.
  std::string text(keymap.substr(0, keymap.find('\0')));
  keyboard->keymap_ = xkb->keymap_new_from_string(
      keyboard->context_, text.c_str(), kKeymapFormatTextV1, 0);
  if (keyboard->keymap_ == nullptr) {
    return nullptr;
  }

  keyboard->state_ = xkb->state_new(keyboard->keymap_);
  if (keyboard->state_ == nullptr) {
    return nullptr;
  }

  return keyboard;
}

XkbKeyboard::XkbKeyboard(const XkbFunctions* xkb) : xkb_(xkb) {}

XkbKeyboard::~XkbKeyboard() {
  if (state_ != nullptr) {
    xkb_->state_unref(state_);
  }
  if (keymap_ != nullptr) {
    xkb_->keymap_unref(keymap_);
  }
  if (context_ != nullptr) {
    xkb_->context_unref(context_);
  }
}

void XkbKeyboard::UpdateState(uint32_t depressed_mods,
                              uint32_t latched_mods,
                              uint32_t locked_mods,
                              uint32_t group) {
  xkb_->state_update_mask(state_, depressed_mods, latched_mods, locked_mods, 0,
                          0, group);
}

uint32_t XkbKeyboard::GetKeysym(uint32_t keycode) const {
  return xkb_->state_key_get_one_sym(state_, keycode);
}

uint32_t XkbKeyboard::GetText(uint32_t keycode) const {
  return xkb_->state_key_get_utf32(state_, keycode);
}

uint32_t XkbKeyboard::GetLayout(uint32_t keycode) const {
  return xkb_->state_key_get_layout(state_, keycode);
}

uint32_t XkbKeyboard::GetLevelKeysym(uint32_t keycode,
                                     uint32_t layout,
                                     uint32_t level) const {
  const uint32_t* syms = nullptr;
  int count = xkb_->keymap_key_get_syms_by_level(keymap_, keycode, layout,
                                                 level, &syms);
  return count > 0 ? syms[0] : 0;
}

bool XkbKeyboard::KeyRepeats(uint32_t keycode) const {
  return xkb_->keymap_key_repeats(keymap_, keycode) != 0;
}

uint32_t XkbKeyboard::NumLayouts() const {
  return xkb_->keymap_num_layouts(keymap_);
}

KeyModifiers XkbKeyboard::GetModifiers() const {
  KeyModifiers modifiers;
  modifiers.mask =
      xkb_->state_serialize_mods(state_, kStateModsEffective) & kRealModsMask;
  modifiers.caps_lock = xkb_->state_led_name_is_active(state_, "Caps Lock") > 0;
  modifiers.num_lock = xkb_->state_led_name_is_active(state_, "Num Lock") > 0;
  modifiers.scroll_lock =
      xkb_->state_led_name_is_active(state_, "Scroll Lock") > 0;
  return modifiers;
}

}  // namespace flutter
