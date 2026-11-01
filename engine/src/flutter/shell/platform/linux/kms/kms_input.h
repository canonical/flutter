// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_INPUT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_INPUT_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/input_handler.h"

struct libinput;
struct libinput_device;
struct libinput_event;
struct udev;

namespace flutter {

class KmsSession;

// Reads input devices on the session's seat with libinput, which is loaded
// at runtime.
//
// Times are in microseconds on CLOCK_MONOTONIC. Positions and distances are
// in physical pixels.
class KmsInput {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // |code| is a Linux input event code, e.g. KEY_A.
    virtual void OnKmsKey(uint64_t time, uint32_t code, bool pressed) = 0;

    virtual void OnKmsPointerMotion(uint64_t time, double dx, double dy) = 0;
    virtual void OnKmsPointerMotionAbsolute(uint64_t time,
                                            double x,
                                            double y) = 0;
    // |button| is a Linux input event code, e.g. BTN_LEFT.
    virtual void OnKmsPointerButton(uint64_t time,
                                    uint32_t button,
                                    bool pressed) = 0;
    virtual void OnKmsPointerScroll(uint64_t time,
                                    const PointerScroll& scroll) = 0;

    virtual void OnKmsTouchDown(uint64_t time,
                                int32_t id,
                                double x,
                                double y) = 0;
    virtual void OnKmsTouchUp(uint64_t time, int32_t id) = 0;
    virtual void OnKmsTouchMotion(uint64_t time,
                                  int32_t id,
                                  double x,
                                  double y) = 0;
    virtual void OnKmsTouchCancel() = 0;
    virtual void OnKmsTouchFrame() = 0;

    virtual void OnKmsPinchBegin(uint64_t time) = 0;
    virtual void OnKmsPinchUpdate(uint64_t time,
                                  double dx,
                                  double dy,
                                  double scale,
                                  double rotation) = 0;
    virtual void OnKmsPinchEnd(uint64_t time, bool cancelled) = 0;

    // Called when a pointer device is added or removed.
    virtual void OnKmsPointerAvailable(bool available) = 0;
  };

  // Returns nullptr if libinput isn't available or fails to start.
  static std::unique_ptr<KmsInput> Create(EventLoop* event_loop,
                                          KmsSession* session,
                                          Delegate* delegate);

  ~KmsInput();

  // Sets the size of the screen absolute positions are scaled to.
  void SetScreenSize(int32_t width, int32_t height) {
    screen_width_ = width;
    screen_height_ = height;
  }

  // Updates the keyboard LEDs to show the lock state.
  void SetLeds(bool caps_lock, bool num_lock, bool scroll_lock);

  // Releases the devices while the session is in the background, and
  // reopens them when it returns.
  void Suspend();
  void Resume();

 private:
  KmsInput(EventLoop* event_loop, KmsSession* session, Delegate* delegate);

  bool Initialize();
  void Dispatch();
  void HandleEvent(libinput_event* event);
  void UpdatePointerAvailable();

  static int OpenRestricted(const char* path, int flags, void* user_data);
  static void CloseRestricted(int fd, void* user_data);

  EventLoop* event_loop_;
  KmsSession* session_;
  Delegate* delegate_;
  udev* udev_ = nullptr;
  libinput* libinput_ = nullptr;
  uint64_t watch_ = 0;
  int32_t screen_width_ = 0;
  int32_t screen_height_ = 0;
  uint32_t leds_ = 0;
  std::vector<libinput_device*> keyboards_;
  std::vector<libinput_device*> pointers_;
  bool pointer_available_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(KmsInput);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_INPUT_H_
