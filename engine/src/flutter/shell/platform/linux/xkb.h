// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_

#include <cstdint>
#include <memory>
#include <string_view>

#include "flutter/fml/macros.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace flutter {

// Functions from libxkbcommon, which is loaded at runtime so the embedder
// has no build or link dependency on it.
//
// libxkbcommon is used to interpret the XKB keymaps Wayland compositors
// send. It is installed on every system that runs a Wayland desktop, since
// compositors and toolkits use it to the same end.
struct XkbFunctions {
  xkb_context* (*context_new)(int flags);
  void (*context_unref)(xkb_context* context);
  xkb_keymap* (*keymap_new_from_string)(xkb_context* context,
                                        const char* string,
                                        int format,
                                        int flags);
  void (*keymap_unref)(xkb_keymap* keymap);
  int (*keymap_key_get_syms_by_level)(xkb_keymap* keymap,
                                      uint32_t key,
                                      uint32_t layout,
                                      uint32_t level,
                                      const uint32_t** syms_out);
  int (*keymap_key_repeats)(xkb_keymap* keymap, uint32_t key);
  uint32_t (*keymap_num_layouts)(xkb_keymap* keymap);
  xkb_state* (*state_new)(xkb_keymap* keymap);
  void (*state_unref)(xkb_state* state);
  int (*state_update_mask)(xkb_state* state,
                           uint32_t depressed_mods,
                           uint32_t latched_mods,
                           uint32_t locked_mods,
                           uint32_t depressed_layout,
                           uint32_t latched_layout,
                           uint32_t locked_layout);
  uint32_t (*state_key_get_one_sym)(xkb_state* state, uint32_t key);
  uint32_t (*state_key_get_utf32)(xkb_state* state, uint32_t key);
  uint32_t (*state_key_get_layout)(xkb_state* state, uint32_t key);
  uint32_t (*state_serialize_mods)(xkb_state* state, int components);
  int (*state_led_name_is_active)(xkb_state* state, const char* name);
  uint32_t (*keysym_to_utf32)(uint32_t keysym);
  uint32_t (*keysym_to_lower)(uint32_t keysym);

  // Returns the functions, or nullptr if libxkbcommon is not installed. A
  // warning is printed the first time this fails.
  static const XkbFunctions* Get();
};

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
  static std::unique_ptr<XkbKeyboard> Create(const XkbFunctions* xkb,
                                             std::string_view keymap);

  ~XkbKeyboard();

  // Sets the state of the keyboard, as sent in wl_keyboard.modifiers.
  void UpdateState(uint32_t depressed_mods,
                   uint32_t latched_mods,
                   uint32_t locked_mods,
                   uint32_t group);

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

  const XkbFunctions* functions() const { return xkb_; }

 private:
  explicit XkbKeyboard(const XkbFunctions* xkb);

  const XkbFunctions* xkb_;
  xkb_context* context_ = nullptr;
  xkb_keymap* keymap_ = nullptr;
  xkb_state* state_ = nullptr;

  FML_DISALLOW_COPY_AND_ASSIGN(XkbKeyboard);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_XKB_H_
