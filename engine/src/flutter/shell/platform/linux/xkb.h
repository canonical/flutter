// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "flutter/fml/macros.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace flutter {

// Returns the Unicode character |keysym| produces, or 0 if none.
uint32_t KeysymToUtf32(uint32_t keysym);

// Returns the lowercase form of |keysym|.
uint32_t KeysymToLower(uint32_t keysym);

// The state of the modifiers that affect a key event.
struct KeyModifiers {
  // A mask of the X11 modifiers that are active, with bits in the order
  // Shift, Lock, Control, Mod1 to Mod5, as GDK reports them.
  uint32_t mask = 0;

  bool caps_lock = false;
  bool num_lock = false;
  bool scroll_lock = false;

  bool operator==(const KeyModifiers& other) const = default;
};

// An XKB keymap and the state of the keyboard using it.
class XkbKeyboard {
 public:
  // Compiles |keymap|, a keymap in the XKB text format. Returns nullptr on
  // failure.
  static std::unique_ptr<XkbKeyboard> Create(std::string_view keymap);

  // Compiles a keymap from RMLVO names, as used by X11 servers. Empty names
  // use the system defaults. Returns nullptr on failure.
  static std::unique_ptr<XkbKeyboard> CreateFromNames(
      const std::string& rules,
      const std::string& model,
      const std::string& layout,
      const std::string& variant,
      const std::string& options);

  ~XkbKeyboard();

  // Sets the state of the keyboard, as sent in wl_keyboard.modifiers.
  void UpdateState(uint32_t depressed_mods,
                   uint32_t latched_mods,
                   uint32_t locked_mods,
                   uint32_t group);

  // Updates the state for |keycode| being pressed or released.
  void UpdateKey(uint32_t keycode, bool pressed);

  // Returns the keysym |keycode| produces in the current state, or 0 if it
  // produces none or more than one.
  uint32_t GetKeysym(uint32_t keycode) const;

  // Returns the character |keycode| types in the current state, after
  // applying transformations such as Control, or 0 if it types none.
  uint32_t GetText(uint32_t keycode) const;

  // Returns the layout used for |keycode| in the current state.
  uint32_t GetLayout(uint32_t keycode) const;

  // Returns the first keysym |keycode| produces at shift |level| of
  // |layout|, or 0 if none.
  uint32_t GetLevelKeysym(uint32_t keycode,
                          uint32_t layout,
                          uint32_t level) const;

  // Returns true if |keycode| repeats when held.
  bool KeyRepeats(uint32_t keycode) const;

  // The number of layouts in the keymap.
  uint32_t NumLayouts() const;

  // Returns the current modifier state.
  KeyModifiers GetModifiers() const;

 private:
  XkbKeyboard() = default;

  xkb_context* context_ = nullptr;
  xkb_keymap* keymap_ = nullptr;
  xkb_state* state_ = nullptr;

  FML_DISALLOW_COPY_AND_ASSIGN(XkbKeyboard);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_
