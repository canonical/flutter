// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/key_event_converter.h"

#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr uint32_t kKeycodeA = 38;
constexpr uint32_t kKeycodeQ = 24;
constexpr uint32_t kKeycodeW = 25;
constexpr uint32_t kKeycodeShiftLeft = 50;
constexpr uint32_t kKeycodeCapsLock = 66;
constexpr uint32_t kKeycodeDigit1 = 10;

constexpr uint32_t kKeysymA = 0x61;
constexpr uint32_t kKeysymShiftA = 0x41;
constexpr uint32_t kKeysymShiftLeft = 0xffe1;
constexpr uint32_t kKeysymCapsLock = 0xffe5;

constexpr uint64_t kPhysicalKeyA = 0x00070004;
constexpr uint64_t kPhysicalShiftLeft = 0x000700e1;
constexpr uint64_t kPhysicalCapsLock = 0x00070039;
constexpr uint64_t kPhysicalNumLock = 0x00070053;

constexpr uint64_t kLogicalKeyA = 0x00000000061;
constexpr uint64_t kLogicalShiftLeft = 0x00200000102;
constexpr uint64_t kLogicalCapsLock = 0x00100000104;
constexpr uint64_t kLogicalNumLock = 0x0010000010a;

struct RecordedEvent {
  FlutterKeyEventType type;
  uint64_t physical;
  uint64_t logical;
  std::optional<std::string> character;
  bool synthesized;
};

class KeyEventConverterTest : public ::testing::Test {
 protected:
  KeyEventConverterTest()
      : converter_([this](const FlutterKeyEvent& event,
                          std::function<void(bool)> callback) {
          events_.push_back({event.type, event.physical, event.logical,
                             event.character != nullptr
                                 ? std::optional<std::string>(event.character)
                                 : std::nullopt,
                             event.synthesized});
          if (callback) {
            callback(false);
          }
        }) {}

  KeyboardKey KeyA() {
    return {.keycode = kKeycodeA,
            .keysym = kKeysymA,
            .logical_key = kLogicalKeyA,
            .character = 'a',
            .text = 'a'};
  }

  KeyboardKey ShiftLeft() {
    return {.keycode = kKeycodeShiftLeft,
            .keysym = kKeysymShiftLeft,
            .logical_key = kLogicalShiftLeft};
  }

  KeyboardKey CapsLock() {
    return {.keycode = kKeycodeCapsLock,
            .keysym = kKeysymCapsLock,
            .logical_key = kLogicalCapsLock};
  }

  std::vector<RecordedEvent> events_;
  KeyEventConverter converter_;
};

}  // namespace

TEST(KeyEventConverterFunctionsTest, PhysicalKey) {
  EXPECT_EQ(GetPhysicalKey(kKeycodeA), kPhysicalKeyA);
  EXPECT_EQ(GetPhysicalKey(kKeycodeShiftLeft), kPhysicalShiftLeft);

  // Unknown keycodes are in the Linux plane.
  EXPECT_EQ(GetPhysicalKey(0xfff), 0x01500000fffu);
}

TEST(KeyEventConverterFunctionsTest, LogicalKey) {
  // Keysyms in the mapping table.
  EXPECT_EQ(GetLogicalKey(kKeysymShiftLeft, 0), kLogicalShiftLeft);

  // Characters use their lowercase form in the Unicode plane.
  EXPECT_EQ(GetLogicalKey(kKeysymShiftA, 'a'), kLogicalKeyA);
  EXPECT_EQ(GetLogicalKey(0x6e4, 0x444), 0x444u);

  // Other keysyms are in the Linux plane.
  EXPECT_EQ(GetLogicalKey(0x1008ff99, 0), 0x0151008ff99u);
}

TEST(KeyEventConverterFunctionsTest, CharacterToUtf8) {
  EXPECT_EQ(CharacterToUtf8('a'), "a");
  EXPECT_EQ(CharacterToUtf8(0xe9), "\xc3\xa9");
  EXPECT_EQ(CharacterToUtf8(0x20ac), "\xe2\x82\xac");
  EXPECT_EQ(CharacterToUtf8(0x1f600), "\xf0\x9f\x98\x80");
}

TEST(KeyEventConverterFunctionsTest, IsPrintable) {
  EXPECT_TRUE(IsPrintable('a'));
  EXPECT_TRUE(IsPrintable(' '));
  EXPECT_TRUE(IsPrintable(0xe9));
  EXPECT_FALSE(IsPrintable(0));
  EXPECT_FALSE(IsPrintable('\r'));
  EXPECT_FALSE(IsPrintable(0x7f));
  EXPECT_FALSE(IsPrintable(0x9f));
}

TEST(KeyEventConverterFunctionsTest, LayoutUs) {
  // The US layout needs no derived keys other than the letters and digits
  // it already produces.
  auto keys =
      DeriveLayoutLogicalKeys([](uint32_t keycode, uint32_t level) -> uint32_t {
        switch (keycode) {
          case kKeycodeA:
            return level == 0 ? 'a' : 'A';
          case kKeycodeQ:
            return level == 0 ? 'q' : 'Q';
          case kKeycodeDigit1:
            return level == 0 ? '1' : '!';
          default:
            return 0;
        }
      });
  EXPECT_EQ(keys[kKeycodeA], kLogicalKeyA);
  EXPECT_EQ(keys[kKeycodeQ], static_cast<uint64_t>('q'));
  EXPECT_EQ(keys[kKeycodeDigit1], static_cast<uint64_t>('1'));
}

TEST(KeyEventConverterFunctionsTest, LayoutFrench) {
  // AZERTY swaps A and Q, and types digits with Shift.
  auto keys =
      DeriveLayoutLogicalKeys([](uint32_t keycode, uint32_t level) -> uint32_t {
        switch (keycode) {
          case kKeycodeA:
            return level == 0 ? 'q' : 'Q';
          case kKeycodeQ:
            return level == 0 ? 'a' : 'A';
          case kKeycodeDigit1:
            return level == 0 ? '&' : '1';
          default:
            return 0;
        }
      });
  EXPECT_EQ(keys[kKeycodeA], static_cast<uint64_t>('q'));
  EXPECT_EQ(keys[kKeycodeQ], static_cast<uint64_t>('a'));
  EXPECT_EQ(keys[kKeycodeDigit1], static_cast<uint64_t>('1'));
}

TEST(KeyEventConverterFunctionsTest, LayoutRussian) {
  // Keys that produce no Latin characters use the US layout.
  auto keys =
      DeriveLayoutLogicalKeys([](uint32_t keycode, uint32_t level) -> uint32_t {
        switch (keycode) {
          case kKeycodeA:
            return level == 0 ? 0x444 : 0x424;
          case kKeycodeW:
            return kLayoutNotCharacter;
          default:
            return 0;
        }
      });
  EXPECT_EQ(keys[kKeycodeA], kLogicalKeyA);
  EXPECT_EQ(keys[kKeycodeW], static_cast<uint64_t>('w'));
}

TEST_F(KeyEventConverterTest, PressAndRelease) {
  bool down_called = false;
  converter_.HandleKey(1000, KeyA(), KeyAction::kDown,
                       [&down_called](bool handled) { down_called = true; });
  EXPECT_TRUE(down_called);
  EXPECT_EQ(converter_.pressed_keys().at(kPhysicalKeyA), kLogicalKeyA);

  converter_.HandleKey(2000, KeyA(), KeyAction::kUp, [](bool handled) {});
  EXPECT_TRUE(converter_.pressed_keys().empty());

  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[0].type, kFlutterKeyEventTypeDown);
  EXPECT_EQ(events_[0].physical, kPhysicalKeyA);
  EXPECT_EQ(events_[0].logical, kLogicalKeyA);
  EXPECT_EQ(events_[0].character, "a");
  EXPECT_FALSE(events_[0].synthesized);
  EXPECT_EQ(events_[1].type, kFlutterKeyEventTypeUp);
  EXPECT_EQ(events_[1].physical, kPhysicalKeyA);
  EXPECT_EQ(events_[1].logical, kLogicalKeyA);
  EXPECT_EQ(events_[1].character, std::nullopt);
}

TEST_F(KeyEventConverterTest, RepeatKeepsLogicalKey) {
  converter_.HandleKey(1000, KeyA(), KeyAction::kDown, [](bool handled) {});

  // Shift was pressed while A was held.
  KeyboardKey shifted = KeyA();
  shifted.keysym = kKeysymShiftA;
  shifted.character = 'A';
  converter_.HandleKey(2000, shifted, KeyAction::kRepeat, [](bool handled) {});

  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[1].type, kFlutterKeyEventTypeRepeat);
  EXPECT_EQ(events_[1].logical, kLogicalKeyA);
  EXPECT_EQ(events_[1].character, "A");
}

TEST_F(KeyEventConverterTest, ReleaseUnpressedKey) {
  bool handled_result = false;
  converter_.HandleKey(
      1000, KeyA(), KeyAction::kUp,
      [&handled_result](bool handled) { handled_result = handled; });

  // An empty event is sent, as each key event must send one, and the key is
  // considered handled.
  EXPECT_TRUE(handled_result);
  ASSERT_EQ(events_.size(), 1u);
  EXPECT_EQ(events_[0].physical, 0u);
  EXPECT_EQ(events_[0].logical, 0u);
}

TEST_F(KeyEventConverterTest, SyncPressedKeys) {
  converter_.HandleKey(1000, KeyA(), KeyAction::kDown, [](bool handled) {});
  events_.clear();

  // Focus returns with Shift held and A released.
  converter_.SyncPressedKeys(2000, {ShiftLeft()});
  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[0].type, kFlutterKeyEventTypeUp);
  EXPECT_EQ(events_[0].physical, kPhysicalKeyA);
  EXPECT_TRUE(events_[0].synthesized);
  EXPECT_EQ(events_[1].type, kFlutterKeyEventTypeDown);
  EXPECT_EQ(events_[1].physical, kPhysicalShiftLeft);
  EXPECT_EQ(events_[1].logical, kLogicalShiftLeft);
  EXPECT_TRUE(events_[1].synthesized);

  // Focus is lost.
  events_.clear();
  converter_.SyncPressedKeys(3000, {});
  ASSERT_EQ(events_.size(), 1u);
  EXPECT_EQ(events_[0].type, kFlutterKeyEventTypeUp);
  EXPECT_EQ(events_[0].physical, kPhysicalShiftLeft);
  EXPECT_TRUE(converter_.pressed_keys().empty());
}

TEST_F(KeyEventConverterTest, SyncLockStates) {
  KeyModifiers modifiers;
  modifiers.num_lock = true;
  converter_.SyncLockStates(1000, modifiers);
  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[0].type, kFlutterKeyEventTypeDown);
  EXPECT_EQ(events_[0].physical, kPhysicalNumLock);
  EXPECT_EQ(events_[0].logical, kLogicalNumLock);
  EXPECT_TRUE(events_[0].synthesized);
  EXPECT_EQ(events_[1].type, kFlutterKeyEventTypeUp);
  EXPECT_EQ(events_[1].logical, kLogicalNumLock);

  // No change.
  events_.clear();
  converter_.SyncLockStates(2000, modifiers);
  EXPECT_TRUE(events_.empty());

  modifiers.num_lock = false;
  converter_.SyncLockStates(3000, modifiers);
  EXPECT_EQ(events_.size(), 2u);
}

TEST_F(KeyEventConverterTest, CapsLockPress) {
  KeyModifiers modifiers;

  // Pressing Caps Lock toggles the state Flutter knows about.
  converter_.HandleKey(1000, CapsLock(), KeyAction::kDown, [](bool handled) {});
  modifiers.caps_lock = true;
  converter_.SyncLockStates(1000, modifiers);
  converter_.HandleKey(2000, CapsLock(), KeyAction::kUp, [](bool handled) {});
  converter_.SyncLockStates(2000, modifiers);
  ASSERT_EQ(events_.size(), 2u);
  EXPECT_FALSE(events_[0].synthesized);
  EXPECT_FALSE(events_[1].synthesized);

  // XKB keeps the lock on until the second press is released.
  events_.clear();
  converter_.HandleKey(3000, CapsLock(), KeyAction::kDown, [](bool handled) {});
  converter_.SyncLockStates(3000, modifiers);
  converter_.HandleKey(4000, CapsLock(), KeyAction::kUp, [](bool handled) {});
  modifiers.caps_lock = false;
  converter_.SyncLockStates(4000, modifiers);
  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[0].physical, kPhysicalCapsLock);
  EXPECT_FALSE(events_[0].synthesized);
  EXPECT_FALSE(events_[1].synthesized);
}

TEST_F(KeyEventConverterTest, LockKeyPressedOnEnter) {
  // Caps Lock was pressed before focus was gained, which turned it on.
  converter_.SyncPressedKeys(1000, {CapsLock()});
  KeyModifiers modifiers;
  modifiers.caps_lock = true;
  converter_.SyncLockStates(1000, modifiers);

  ASSERT_EQ(events_.size(), 1u);
  EXPECT_EQ(events_[0].type, kFlutterKeyEventTypeDown);
  EXPECT_EQ(events_[0].logical, kLogicalCapsLock);
}

}  // namespace testing
}  // namespace flutter
