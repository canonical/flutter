// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_seat.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// A keymap that doesn't need the system's XKB data.
constexpr char kKeymap[] = R"(xkb_keymap {
  xkb_keycodes "test" {
    minimum = 8;
    maximum = 255;
    <AC01> = 38;
    <LCTL> = 37;
    <CAPS> = 66;
    <FK01> = 67;
    indicator 1 = "Caps Lock";
  };
  xkb_types "test" {
    type "ONE_LEVEL" {
      modifiers = none;
      level_name[Level1] = "Any";
    };
    type "ALPHABETIC" {
      modifiers = Shift + Lock;
      map[Shift] = Level2;
      map[Lock] = Level2;
      level_name[Level1] = "Base";
      level_name[Level2] = "Caps";
    };
  };
  xkb_compatibility "test" {
    interpret Control_L { action = SetMods(modifiers = Control); };
    interpret Caps_Lock { action = LockMods(modifiers = Lock); };
    indicator "Caps Lock" { whichModState = Locked; modifiers = Lock; };
  };
  xkb_symbols "test" {
    key <AC01> { type = "ALPHABETIC", [ a, A ] };
    key <LCTL> { [ Control_L ] };
    key <CAPS> { [ Caps_Lock ] };
    key <FK01> { [ XF86Switch_VT_1 ] };
    modifier_map Control { <LCTL> };
    modifier_map Lock { <CAPS> };
  };
};)";

constexpr uint32_t kControlMask = 1 << 2;

class FakeHandler : public InputHandler, public KeyboardHandler {
 public:
  std::vector<std::string> events;
  std::vector<KeyboardKey> keys;
  std::vector<KeyModifiers> modifiers;

  void OnPointerEnter(uint64_t time, double x, double y) override {
    events.push_back("enter " + Point(x, y));
  }
  void OnPointerLeave(uint64_t time) override { events.push_back("leave"); }
  void OnPointerMotion(uint64_t time, double x, double y) override {
    events.push_back("motion " + Point(x, y));
  }
  void OnPointerButton(uint32_t serial,
                       uint64_t time,
                       uint32_t button,
                       bool pressed) override {
    events.push_back("button " + std::to_string(button) +
                     (pressed ? " down" : " up"));
  }
  void OnPointerScroll(uint64_t time, const PointerScroll& scroll) override {
    events.push_back("scroll " + Point(scroll.dx, scroll.dy));
  }
  void OnTouchDown(uint32_t serial,
                   uint64_t time,
                   int32_t id,
                   double x,
                   double y) override {
    events.push_back("touch down " + std::to_string(id) + " " + Point(x, y));
  }
  void OnTouchUp(uint64_t time, int32_t id) override {
    events.push_back("touch up " + std::to_string(id));
  }
  void OnTouchMotion(uint64_t time, int32_t id, double x, double y) override {
    events.push_back("touch motion " + std::to_string(id) + " " + Point(x, y));
  }
  void OnTouchCancel() override { events.push_back("touch cancel"); }

  void OnKeyboardEnter(uint64_t time,
                       const std::vector<KeyboardKey>& pressed,
                       const KeyModifiers& mods) override {
    events.push_back("keyboard enter " + std::to_string(pressed.size()));
    keys = pressed;
  }
  void OnKeyboardLeave(uint64_t time) override {
    events.push_back("keyboard leave");
  }
  void OnKey(uint64_t time,
             const KeyboardKey& key,
             KeyAction action,
             const KeyModifiers& mods) override {
    const char* names[] = {"down", "up", "repeat"};
    events.push_back("key " + std::to_string(key.keysym) + " " +
                     names[static_cast<int>(action)]);
    modifiers.push_back(mods);
  }
  void OnKeyModifiers(uint64_t time, const KeyModifiers& mods) override {
    events.push_back("modifiers " + std::to_string(mods.mask));
    modifiers.push_back(mods);
  }

 private:
  static std::string Point(double x, double y) {
    return std::to_string(static_cast<int>(x)) + "," +
           std::to_string(static_cast<int>(y));
  }
};

class KmsSeatTest : public ::testing::Test {
 protected:
  KmsSeatTest() : seat_(&loop_, XkbKeyboard::Create(kKeymap)) {
    seat_.SetScreen(1000, 800, 2.0);
  }

  void Focus() { seat_.SetHandlers(&handler_, &handler_); }

  EventLoop loop_;
  KmsSeat seat_;
  FakeHandler handler_;
};

}  // namespace

TEST_F(KmsSeatTest, SendsKeys) {
  Focus();
  seat_.OnKmsKey(1, KEY_A, true);
  seat_.OnKmsKey(2, KEY_A, false);
  EXPECT_EQ(handler_.events, std::vector<std::string>(
                                 {"keyboard enter 0",
                                  "key " + std::to_string(XKB_KEY_a) + " down",
                                  "key " + std::to_string(XKB_KEY_a) + " up"}));
}

TEST_F(KmsSeatTest, ReportsModifiersAfterKey) {
  Focus();
  seat_.OnKmsKey(1, KEY_LEFTCTRL, true);
  ASSERT_EQ(handler_.events.size(), 3u);
  EXPECT_EQ(handler_.events[1],
            "key " + std::to_string(XKB_KEY_Control_L) + " down");
  EXPECT_EQ(handler_.events[2], "modifiers " + std::to_string(kControlMask));
  // The key is sent with the modifiers before it was pressed.
  EXPECT_EQ(handler_.modifiers[0].mask, 0u);
}

TEST_F(KmsSeatTest, UpdatesLeds) {
  std::vector<bool> caps_lock;
  seat_.set_leds_callback(
      [&](const KeyModifiers& mods) { caps_lock.push_back(mods.caps_lock); });
  seat_.OnKmsKey(1, KEY_CAPSLOCK, true);
  seat_.OnKmsKey(2, KEY_CAPSLOCK, false);
  seat_.OnKmsKey(3, KEY_CAPSLOCK, true);
  seat_.OnKmsKey(4, KEY_CAPSLOCK, false);
  EXPECT_EQ(caps_lock, std::vector<bool>({true, false}));
}

TEST_F(KmsSeatTest, IgnoresKeysWithoutHandlers) {
  seat_.OnKmsKey(1, KEY_A, true);
  EXPECT_TRUE(handler_.events.empty());
  // Keys already pressed are reported on enter.
  Focus();
  ASSERT_EQ(handler_.keys.size(), 1u);
  EXPECT_EQ(handler_.keys[0].keysym, static_cast<uint32_t>(XKB_KEY_a));
}

TEST_F(KmsSeatTest, SwitchesVirtualTerminal) {
  Focus();
  std::vector<uint32_t> vts;
  seat_.set_switch_vt_callback([&](uint32_t vt) { vts.push_back(vt); });
  seat_.OnKmsKey(1, KEY_F1, true);
  seat_.OnKmsKey(2, KEY_F1, false);
  EXPECT_EQ(vts, std::vector<uint32_t>({1}));
  // The key isn't sent to the window.
  EXPECT_EQ(handler_.events, std::vector<std::string>({"keyboard enter 0"}));
}

TEST_F(KmsSeatTest, RepeatsKeys) {
  Focus();
  seat_.OnKmsKey(1, KEY_A, true);
  loop_.PostTask([this]() { loop_.Quit(); },
                 EventLoop::Clock::now() +
                     std::chrono::milliseconds(KmsSeat::kRepeatDelayMs + 100));
  loop_.Run();
  seat_.OnKmsKey(2, KEY_A, false);
  std::string repeat = "key " + std::to_string(XKB_KEY_a) + " repeat";
  size_t repeats =
      std::count(handler_.events.begin(), handler_.events.end(), repeat);
  EXPECT_GE(repeats, 1u);
  EXPECT_EQ(handler_.events.back(), "key " + std::to_string(XKB_KEY_a) + " up");
}

TEST_F(KmsSeatTest, LeavesWhenInactive) {
  Focus();
  seat_.OnKmsPointerAvailable(true);
  seat_.OnKmsPointerMotion(1, 10, 0);
  seat_.OnKmsKey(2, KEY_A, true);
  handler_.events.clear();

  seat_.SetActive(false);
  EXPECT_EQ(handler_.events,
            std::vector<std::string>({"keyboard leave", "leave"}));
  // Input is ignored while inactive.
  seat_.OnKmsPointerMotion(3, 10, 0);
  EXPECT_EQ(handler_.events.size(), 2u);

  // Keys pressed before becoming inactive are considered released.
  seat_.SetActive(true);
  EXPECT_EQ(handler_.events.back(), "keyboard enter 0");
}

TEST_F(KmsSeatTest, MovesPointer) {
  Focus();
  seat_.OnKmsPointerAvailable(true);
  // The pointer starts in the center, and positions are in logical pixels.
  seat_.OnKmsPointerMotion(1, 10, 20);
  seat_.OnKmsPointerMotion(2, -10000, 10000);
  seat_.OnKmsPointerMotionAbsolute(3, 100, 200);
  seat_.OnKmsPointerButton(4, BTN_LEFT, true);
  EXPECT_EQ(
      handler_.events,
      std::vector<std::string>(
          {"keyboard enter 0", "enter 255,210", "motion 0,400", "motion 50,100",
           "button " + std::to_string(BTN_LEFT) + " down"}));
  EXPECT_DOUBLE_EQ(seat_.pointer_x(), 100);
  EXPECT_DOUBLE_EQ(seat_.pointer_y(), 200);
}

TEST_F(KmsSeatTest, LeavesWhenPointerRemoved) {
  Focus();
  seat_.OnKmsPointerAvailable(true);
  seat_.OnKmsPointerButton(1, BTN_LEFT, true);
  seat_.OnKmsPointerAvailable(false);
  EXPECT_EQ(handler_.events.back(), "leave");
  EXPECT_FALSE(seat_.has_pointer());
}

TEST_F(KmsSeatTest, SendsTouches) {
  Focus();
  seat_.OnKmsTouchDown(1, 0, 100, 200);
  seat_.OnKmsTouchMotion(2, 0, 120, 220);
  seat_.OnKmsTouchUp(3, 0);
  // Touch points that weren't started are ignored.
  seat_.OnKmsTouchMotion(4, 1, 0, 0);
  seat_.OnKmsTouchDown(5, 1, 0, 0);
  seat_.SetHandlers(nullptr, nullptr);
  EXPECT_EQ(handler_.events,
            std::vector<std::string>({"keyboard enter 0", "touch down 0 50,100",
                                      "touch motion 0 60,110", "touch up 0",
                                      "touch down 1 0,0", "keyboard leave",
                                      "touch cancel"}));
}

}  // namespace testing
}  // namespace flutter
