// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_input.h"

#include <dlfcn.h>
#include <libinput.h>
#include <libudev.h>
#include <sys/epoll.h>

#include <algorithm>
#include <cerrno>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/kms/kms_session.h"

namespace flutter {

namespace {

// libinput and libudev are loaded at runtime so the embedder doesn't depend
// on them unless the KMS backend is used.
#define KMS_INPUT_FUNCTIONS(F)                                \
  F(udev, udev_new)                                           \
  F(udev, udev_unref)                                         \
  F(input, libinput_udev_create_context)                      \
  F(input, libinput_udev_assign_seat)                         \
  F(input, libinput_unref)                                    \
  F(input, libinput_get_fd)                                   \
  F(input, libinput_dispatch)                                 \
  F(input, libinput_get_event)                                \
  F(input, libinput_suspend)                                  \
  F(input, libinput_resume)                                   \
  F(input, libinput_event_destroy)                            \
  F(input, libinput_event_get_type)                           \
  F(input, libinput_event_get_device)                         \
  F(input, libinput_device_ref)                               \
  F(input, libinput_device_unref)                             \
  F(input, libinput_device_has_capability)                    \
  F(input, libinput_device_led_update)                        \
  F(input, libinput_event_get_keyboard_event)                 \
  F(input, libinput_event_keyboard_get_time_usec)             \
  F(input, libinput_event_keyboard_get_key)                   \
  F(input, libinput_event_keyboard_get_key_state)             \
  F(input, libinput_event_keyboard_get_seat_key_count)        \
  F(input, libinput_event_get_pointer_event)                  \
  F(input, libinput_event_pointer_get_time_usec)              \
  F(input, libinput_event_pointer_get_dx)                     \
  F(input, libinput_event_pointer_get_dy)                     \
  F(input, libinput_event_pointer_get_absolute_x_transformed) \
  F(input, libinput_event_pointer_get_absolute_y_transformed) \
  F(input, libinput_event_pointer_get_button)                 \
  F(input, libinput_event_pointer_get_button_state)           \
  F(input, libinput_event_pointer_get_seat_button_count)      \
  F(input, libinput_event_pointer_has_axis)                   \
  F(input, libinput_event_pointer_get_axis_value)             \
  F(input, libinput_event_pointer_get_axis_source)            \
  F(input, libinput_event_get_touch_event)                    \
  F(input, libinput_event_touch_get_time_usec)                \
  F(input, libinput_event_touch_get_seat_slot)                \
  F(input, libinput_event_touch_get_x_transformed)            \
  F(input, libinput_event_touch_get_y_transformed)            \
  F(input, libinput_event_get_gesture_event)                  \
  F(input, libinput_event_gesture_get_time_usec)              \
  F(input, libinput_event_gesture_get_cancelled)              \
  F(input, libinput_event_gesture_get_dx)                     \
  F(input, libinput_event_gesture_get_dy)                     \
  F(input, libinput_event_gesture_get_scale)                  \
  F(input, libinput_event_gesture_get_angle_delta)

struct LibInput {
  bool loaded = false;
#define KMS_INPUT_DECLARE(library, name) decltype(&::name) name = nullptr;
  KMS_INPUT_FUNCTIONS(KMS_INPUT_DECLARE)
#undef KMS_INPUT_DECLARE
};

const LibInput& LoadLibInput() {
  static LibInput library = []() {
    LibInput result;
    void* udev = dlopen("libudev.so.1", RTLD_NOW | RTLD_LOCAL);
    void* input = dlopen("libinput.so.10", RTLD_NOW | RTLD_LOCAL);
    if (udev == nullptr || input == nullptr) {
      FML_LOG(ERROR) << "Failed to load libinput: " << dlerror();
      return result;
    }
    bool ok = true;
#define KMS_INPUT_LOAD(library, name)                                       \
  result.name = reinterpret_cast<decltype(&::name)>(dlsym(library, #name)); \
  ok = ok && result.name != nullptr;
    KMS_INPUT_FUNCTIONS(KMS_INPUT_LOAD)
#undef KMS_INPUT_LOAD
    if (!ok) {
      FML_LOG(ERROR) << "libinput is missing required functions";
      return result;
    }
    result.loaded = true;
    return result;
  }();
  return library;
}

uint32_t ToScrollSource(libinput_pointer_axis_source source) {
  switch (source) {
    case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
      return WL_POINTER_AXIS_SOURCE_FINGER;
    case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
      return WL_POINTER_AXIS_SOURCE_CONTINUOUS;
    case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
      return WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
    case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
    default:
      return WL_POINTER_AXIS_SOURCE_WHEEL;
  }
}

}  // namespace

std::unique_ptr<KmsInput> KmsInput::Create(EventLoop* event_loop,
                                           KmsSession* session,
                                           Delegate* delegate) {
  if (!LoadLibInput().loaded) {
    return nullptr;
  }
  std::unique_ptr<KmsInput> input(new KmsInput(event_loop, session, delegate));
  if (!input->Initialize()) {
    return nullptr;
  }
  return input;
}

KmsInput::KmsInput(EventLoop* event_loop,
                   KmsSession* session,
                   Delegate* delegate)
    : event_loop_(event_loop), session_(session), delegate_(delegate) {}

KmsInput::~KmsInput() {
  const LibInput& lib = LoadLibInput();
  if (watch_ != 0) {
    event_loop_->RemoveWatch(watch_);
  }
  for (libinput_device* device : keyboards_) {
    lib.libinput_device_unref(device);
  }
  for (libinput_device* device : pointers_) {
    lib.libinput_device_unref(device);
  }
  if (libinput_ != nullptr) {
    lib.libinput_unref(libinput_);
  }
  if (udev_ != nullptr) {
    lib.udev_unref(udev_);
  }
}

bool KmsInput::Initialize() {
  const LibInput& lib = LoadLibInput();
  static const libinput_interface interface = {
      .open_restricted = &KmsInput::OpenRestricted,
      .close_restricted = &KmsInput::CloseRestricted,
  };

  udev_ = lib.udev_new();
  if (udev_ == nullptr) {
    FML_LOG(ERROR) << "Failed to create udev context";
    return false;
  }
  libinput_ = lib.libinput_udev_create_context(&interface, this, udev_);
  if (libinput_ == nullptr) {
    FML_LOG(ERROR) << "Failed to create libinput context";
    return false;
  }
  if (lib.libinput_udev_assign_seat(libinput_, session_->seat().c_str()) != 0) {
    FML_LOG(ERROR) << "Failed to assign libinput to " << session_->seat();
    return false;
  }

  watch_ = event_loop_->AddWatch(lib.libinput_get_fd(libinput_), EPOLLIN,
                                 [this](uint32_t events) { Dispatch(); });
  // Process the devices added when the seat was assigned.
  Dispatch();
  return true;
}

int KmsInput::OpenRestricted(const char* path, int flags, void* user_data) {
  KmsInput* self = static_cast<KmsInput*>(user_data);
  fml::UniqueFD fd = self->session_->OpenDevice(path, flags);
  if (!fd.is_valid()) {
    return -errno;
  }
  return fd.release();
}

void KmsInput::CloseRestricted(int fd, void* user_data) {
  static_cast<KmsInput*>(user_data)->session_->CloseDevice(fd);
}

void KmsInput::SetLeds(bool caps_lock, bool num_lock, bool scroll_lock) {
  uint32_t leds = (caps_lock ? LIBINPUT_LED_CAPS_LOCK : 0) |
                  (num_lock ? LIBINPUT_LED_NUM_LOCK : 0) |
                  (scroll_lock ? LIBINPUT_LED_SCROLL_LOCK : 0);
  if (leds == leds_) {
    return;
  }
  leds_ = leds;
  const LibInput& lib = LoadLibInput();
  for (libinput_device* device : keyboards_) {
    lib.libinput_device_led_update(device, static_cast<libinput_led>(leds_));
  }
}

void KmsInput::Suspend() {
  LoadLibInput().libinput_suspend(libinput_);
  // Devices are removed on suspend, dispatch the events so they're released.
  Dispatch();
}

void KmsInput::Resume() {
  LoadLibInput().libinput_resume(libinput_);
  Dispatch();
}

void KmsInput::Dispatch() {
  const LibInput& lib = LoadLibInput();
  if (lib.libinput_dispatch(libinput_) != 0) {
    FML_LOG(ERROR) << "Failed to read input events";
  }
  libinput_event* event;
  while ((event = lib.libinput_get_event(libinput_)) != nullptr) {
    HandleEvent(event);
    lib.libinput_event_destroy(event);
  }
}

void KmsInput::UpdatePointerAvailable() {
  bool available = !pointers_.empty();
  if (available != pointer_available_) {
    pointer_available_ = available;
    delegate_->OnKmsPointerAvailable(available);
  }
}

void KmsInput::HandleEvent(libinput_event* event) {
  const LibInput& lib = LoadLibInput();
  switch (lib.libinput_event_get_type(event)) {
    case LIBINPUT_EVENT_DEVICE_ADDED: {
      libinput_device* device = lib.libinput_event_get_device(event);
      if (lib.libinput_device_has_capability(device,
                                             LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        keyboards_.push_back(lib.libinput_device_ref(device));
        lib.libinput_device_led_update(device,
                                       static_cast<libinput_led>(leds_));
      }
      if (lib.libinput_device_has_capability(device,
                                             LIBINPUT_DEVICE_CAP_POINTER)) {
        pointers_.push_back(lib.libinput_device_ref(device));
        UpdatePointerAvailable();
      }
      break;
    }
    case LIBINPUT_EVENT_DEVICE_REMOVED: {
      libinput_device* device = lib.libinput_event_get_device(event);
      for (auto* list : {&keyboards_, &pointers_}) {
        auto it = std::find(list->begin(), list->end(), device);
        if (it != list->end()) {
          lib.libinput_device_unref(device);
          list->erase(it);
        }
      }
      UpdatePointerAvailable();
      break;
    }
    case LIBINPUT_EVENT_KEYBOARD_KEY: {
      libinput_event_keyboard* key =
          lib.libinput_event_get_keyboard_event(event);
      bool pressed = lib.libinput_event_keyboard_get_key_state(key) ==
                     LIBINPUT_KEY_STATE_PRESSED;
      // Only report the first press and last release of a key pressed on
      // more than one keyboard.
      uint32_t count = lib.libinput_event_keyboard_get_seat_key_count(key);
      if ((pressed && count != 1) || (!pressed && count != 0)) {
        break;
      }
      delegate_->OnKmsKey(lib.libinput_event_keyboard_get_time_usec(key),
                          lib.libinput_event_keyboard_get_key(key), pressed);
      break;
    }
    case LIBINPUT_EVENT_POINTER_MOTION: {
      libinput_event_pointer* pointer =
          lib.libinput_event_get_pointer_event(event);
      delegate_->OnKmsPointerMotion(
          lib.libinput_event_pointer_get_time_usec(pointer),
          lib.libinput_event_pointer_get_dx(pointer),
          lib.libinput_event_pointer_get_dy(pointer));
      break;
    }
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
      libinput_event_pointer* pointer =
          lib.libinput_event_get_pointer_event(event);
      delegate_->OnKmsPointerMotionAbsolute(
          lib.libinput_event_pointer_get_time_usec(pointer),
          lib.libinput_event_pointer_get_absolute_x_transformed(pointer,
                                                                screen_width_),
          lib.libinput_event_pointer_get_absolute_y_transformed(
              pointer, screen_height_));
      break;
    }
    case LIBINPUT_EVENT_POINTER_BUTTON: {
      libinput_event_pointer* pointer =
          lib.libinput_event_get_pointer_event(event);
      bool pressed = lib.libinput_event_pointer_get_button_state(pointer) ==
                     LIBINPUT_BUTTON_STATE_PRESSED;
      uint32_t count =
          lib.libinput_event_pointer_get_seat_button_count(pointer);
      if ((pressed && count != 1) || (!pressed && count != 0)) {
        break;
      }
      delegate_->OnKmsPointerButton(
          lib.libinput_event_pointer_get_time_usec(pointer),
          lib.libinput_event_pointer_get_button(pointer), pressed);
      break;
    }
    case LIBINPUT_EVENT_POINTER_AXIS: {
      libinput_event_pointer* pointer =
          lib.libinput_event_get_pointer_event(event);
      PointerScroll scroll;
      scroll.source =
          ToScrollSource(lib.libinput_event_pointer_get_axis_source(pointer));
      bool stop = true;
      for (auto axis : {LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
                        LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL}) {
        if (!lib.libinput_event_pointer_has_axis(pointer, axis)) {
          continue;
        }
        double value = lib.libinput_event_pointer_get_axis_value(pointer, axis);
        (axis == LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL ? scroll.dx
                                                         : scroll.dy) = value;
        stop = stop && value == 0;
      }
      // A zero value from a touchpad ends the scroll sequence.
      scroll.stop = stop && scroll.source == WL_POINTER_AXIS_SOURCE_FINGER;
      delegate_->OnKmsPointerScroll(
          lib.libinput_event_pointer_get_time_usec(pointer), scroll);
      break;
    }
    case LIBINPUT_EVENT_TOUCH_DOWN:
    case LIBINPUT_EVENT_TOUCH_MOTION: {
      libinput_event_touch* touch = lib.libinput_event_get_touch_event(event);
      uint64_t time = lib.libinput_event_touch_get_time_usec(touch);
      int32_t id = lib.libinput_event_touch_get_seat_slot(touch);
      double x =
          lib.libinput_event_touch_get_x_transformed(touch, screen_width_);
      double y =
          lib.libinput_event_touch_get_y_transformed(touch, screen_height_);
      if (lib.libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN) {
        delegate_->OnKmsTouchDown(time, id, x, y);
      } else {
        delegate_->OnKmsTouchMotion(time, id, x, y);
      }
      break;
    }
    case LIBINPUT_EVENT_TOUCH_UP: {
      libinput_event_touch* touch = lib.libinput_event_get_touch_event(event);
      delegate_->OnKmsTouchUp(lib.libinput_event_touch_get_time_usec(touch),
                              lib.libinput_event_touch_get_seat_slot(touch));
      break;
    }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      delegate_->OnKmsTouchCancel();
      break;
    case LIBINPUT_EVENT_TOUCH_FRAME:
      delegate_->OnKmsTouchFrame();
      break;
    case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN: {
      libinput_event_gesture* gesture =
          lib.libinput_event_get_gesture_event(event);
      delegate_->OnKmsPinchBegin(
          lib.libinput_event_gesture_get_time_usec(gesture));
      break;
    }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE: {
      libinput_event_gesture* gesture =
          lib.libinput_event_get_gesture_event(event);
      delegate_->OnKmsPinchUpdate(
          lib.libinput_event_gesture_get_time_usec(gesture),
          lib.libinput_event_gesture_get_dx(gesture),
          lib.libinput_event_gesture_get_dy(gesture),
          lib.libinput_event_gesture_get_scale(gesture),
          lib.libinput_event_gesture_get_angle_delta(gesture));
      break;
    }
    case LIBINPUT_EVENT_GESTURE_PINCH_END: {
      libinput_event_gesture* gesture =
          lib.libinput_event_get_gesture_event(event);
      delegate_->OnKmsPinchEnd(
          lib.libinput_event_gesture_get_time_usec(gesture),
          lib.libinput_event_gesture_get_cancelled(gesture) != 0);
      break;
    }
    default:
      break;
  }
}

}  // namespace flutter
