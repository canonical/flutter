// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KEY_MAPPING_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KEY_MAPPING_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace flutter {

// Maps XKB keycodes to Flutter physical keys.
extern const std::unordered_map<uint32_t, uint64_t> kXkbKeycodeToPhysicalKey;

// Maps XKB keysyms to Flutter logical keys, for keys that don't produce a
// printable character.
extern const std::unordered_map<uint32_t, uint64_t> kKeysymToLogicalKey;

// A logical key that should be produced by a key on any keyboard layout, so
// shortcuts such as Ctrl+C work on layouts that don't have that character.
struct LayoutGoal {
  // The XKB keycode of the key that produces |logical_key| on the US layout.
  uint32_t keycode;

  // The logical key, which is the Unicode character it produces for printable
  // keys.
  uint64_t logical_key;

  // True if a key must be found for this goal, falling back to the US layout
  // if no key on the current layout produces it.
  bool mandatory;
};

extern const std::vector<LayoutGoal> kLayoutGoals;

// The bits of a logical key that hold its value within its plane.
extern const uint64_t kValueMask;

// The plane of logical keys that are Unicode characters.
extern const uint64_t kUnicodePlane;

// The plane of logical keys that are keysyms Flutter doesn't recognize.
extern const uint64_t kLinuxPlane;

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KEY_MAPPING_H_
