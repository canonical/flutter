// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_OUTPUT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_OUTPUT_H_

#include <cstdint>
#include <functional>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-output-unstable-v1-client-protocol.h"

namespace flutter {

// A display connected to the compositor.
//
// The properties are updated atomically, and |changed_callback| is called
// once all the properties of a change have been received.
class WaylandOutput {
 public:
  // Takes ownership of |output|, which is the global with registry name |id|.
  WaylandOutput(uint32_t id,
                wl_output* output,
                std::function<void()> changed_callback);
  ~WaylandOutput();

  // Gets the logical size of the output from |manager|, which may be
  // nullptr.
  void SetXdgOutputManager(zxdg_output_manager_v1* manager);

  // The registry name of the output, which is unique for the lifetime of the
  // connection.
  uint32_t id() const { return id_; }

  wl_output* output() const { return output_; }

  // True once the compositor has sent all the properties of the output.
  bool ready() const { return ready_; }

  // The size of the output in physical pixels, as shown on the screen (i.e.
  // after rotation).
  int32_t width() const { return width_; }
  int32_t height() const { return height_; }

  // The refresh rate in frames per second, or zero if unknown.
  double refresh_rate() const { return refresh_mhz_ / 1000.0; }

  // The integer scale the compositor prefers buffers to be drawn at.
  int32_t scale() const { return scale_; }

  // The number of physical pixels per logical pixel. Unlike |scale| this
  // may be fractional.
  double pixel_ratio() const;

  // The name of the output, e.g. "DP-1", which may be empty.
  const std::string& name() const { return name_; }

  // A human readable description of the output, which may be empty.
  const std::string& description() const { return description_; }

 private:
  // The properties of the output, which are applied together.
  struct State {
    int32_t mode_width = 0;
    int32_t mode_height = 0;
    int32_t refresh_mhz = 0;
    int32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
    int32_t scale = 1;
    int32_t logical_width = 0;
    int32_t logical_height = 0;
    std::string name;
    std::string description;
  };

  // Applies the pending state.
  void ApplyPending();

  static void Geometry(void* data,
                       wl_output* output,
                       int32_t x,
                       int32_t y,
                       int32_t physical_width,
                       int32_t physical_height,
                       int32_t subpixel,
                       const char* make,
                       const char* model,
                       int32_t transform);
  static void Mode(void* data,
                   wl_output* output,
                   uint32_t flags,
                   int32_t width,
                   int32_t height,
                   int32_t refresh);
  static void Done(void* data, wl_output* output);
  static void Scale(void* data, wl_output* output, int32_t factor);
  static void Name(void* data, wl_output* output, const char* name);
  static void Description(void* data,
                          wl_output* output,
                          const char* description);
  static void XdgLogicalPosition(void* data,
                                 zxdg_output_v1* xdg_output,
                                 int32_t x,
                                 int32_t y);
  static void XdgLogicalSize(void* data,
                             zxdg_output_v1* xdg_output,
                             int32_t width,
                             int32_t height);
  static void XdgDone(void* data, zxdg_output_v1* xdg_output);
  static void XdgName(void* data, zxdg_output_v1* xdg_output, const char* name);
  static void XdgDescription(void* data,
                             zxdg_output_v1* xdg_output,
                             const char* description);

  static const wl_output_listener kOutputListener;
  static const zxdg_output_v1_listener kXdgOutputListener;

  uint32_t id_;
  wl_output* output_;
  zxdg_output_v1* xdg_output_ = nullptr;
  std::function<void()> changed_callback_;

  State pending_;
  bool ready_ = false;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t refresh_mhz_ = 0;
  int32_t scale_ = 1;
  int32_t logical_width_ = 0;
  std::string name_;
  std::string description_;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandOutput);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_OUTPUT_H_
