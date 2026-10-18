// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_DECORATION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_DECORATION_H_

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/decoration_renderer.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland_display.h"
#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

// A rectangle in logical pixels.
struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// How the decoration surface should look for a given content size. Applied
// atomically with a content buffer of the matching size.
struct DecorationFrame {
  // The buffer to show, or nullptr to hide the decorations.
  RenderBuffer* buffer = nullptr;

  // The position and size of the decoration surface relative to the content
  // surface.
  Rect surface;

  // The parts of the decoration surface that accept input, and that are
  // opaque.
  std::vector<Rect> input;
  std::vector<Rect> opaque;

  // The window geometry, relative to the content surface.
  Rect geometry;

  // The size of the content buffer this frame matches, in physical pixels.
  int content_width = 0;
  int content_height = 0;
};

// Client-side window decorations: a title bar with window controls and a
// shadow, drawn in a subsurface below the window content so Flutter's
// buffers are shown without copying.
//
// Used on the platform thread.
class WindowDecoration : public InputHandler {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the decoration has a new frame to show.
    virtual void OnDecorationFrame(const DecorationFrame& frame) = 0;

    // Called when the user presses the close button.
    virtual void OnDecorationClose() = 0;

    // Returns true if |buffer| is waiting to be shown.
    virtual bool IsDecorationBufferPending(RenderBuffer* buffer) = 0;
  };

  // The state of the window the decorations are drawn for.
  struct State {
    // The size of the content in logical pixels.
    int width = 0;
    int height = 0;
    double scale = 1.0;

    bool maximized = false;
    bool fullscreen = false;
    bool tiled = false;
    bool activated = false;
    bool can_minimize = true;
    bool can_maximize = true;

    std::string title;

    bool operator==(const State& other) const = default;
  };

  // The height of the title bar in logical pixels.
  static constexpr int kTitleBarHeight = 44;

  // The space around a floating window used to draw its shadow.
  static constexpr int kShadowMargin = 28;

  WindowDecoration(WaylandDisplay* display,
                   DecorationRenderer* renderer,
                   Delegate* delegate,
                   wl_surface* parent,
                   xdg_toplevel* toplevel);
  ~WindowDecoration() override;

  // Redraws the decorations if |state| has changed.
  void SetState(const State& state);

  // Applies |frame| to the decoration surface. The change is shown when the
  // parent surface is next committed. May be called from any thread, but
  // not concurrently with other surface changes.
  void Apply(const DecorationFrame& frame);

  // |InputHandler|
  void OnPointerEnter(uint64_t time, double x, double y) override;
  void OnPointerLeave(uint64_t time) override;
  void OnPointerMotion(uint64_t time, double x, double y) override;
  void OnPointerButton(uint32_t serial,
                       uint64_t time,
                       uint32_t button,
                       bool pressed) override;
  void OnTouchDown(uint32_t serial,
                   uint64_t time,
                   int32_t id,
                   double x,
                   double y) override;
  void OnTouchUp(uint64_t time, int32_t id) override;
  void OnTouchMotion(uint64_t time, int32_t id, double x, double y) override;
  void OnTouchCancel() override;

 private:
  enum class Button { kNone, kMinimize, kMaximize, kClose };

  // Where the pointer is.
  struct Hit {
    // A combination of xdg_toplevel_resize_edge bits, or zero if not on an
    // edge.
    uint32_t edges = 0;
    Button button = Button::kNone;
    bool title_bar = false;
  };

  // Whether the window floats, with a shadow and resizable edges.
  bool floating() const;

  // The window outline relative to the decoration surface.
  Rect WindowRect() const;

  // The buttons shown, from left to right.
  std::vector<Button> Buttons() const;

  // The centre of |button| relative to the window, in logical pixels.
  void ButtonCentre(Button button, double* x, double* y) const;

  Hit HitTest(double x, double y) const;

  void UpdatePointer(double x, double y);

  // Performs the action of |button|.
  void ActivateButton(Button button);

  // Starts a move, or toggles maximization on a double press.
  void PressTitleBar(uint32_t serial);

  void SetPressedButton(Button button);

  // Draws and submits a new frame.
  void Redraw();

  // Returns a buffer that can be drawn into, or nullptr.
  RenderBuffer* GetBuffer(int width, int height);

  DecorationScene BuildScene();

  // Returns the title rendered to fit within |max_width| pixels, reusing the
  // previous image if nothing has changed.
  std::shared_ptr<const AlphaImage> GetTitleImage(int max_width);

  WaylandDisplay* display_;
  DecorationRenderer* renderer_;
  Delegate* delegate_;
  xdg_toplevel* toplevel_;

  wl_surface* surface_ = nullptr;
  wl_subsurface* subsurface_ = nullptr;
  wp_viewport* viewport_ = nullptr;

  State state_;
  bool has_state_ = false;

  std::vector<std::unique_ptr<RenderBuffer>> buffers_;

  double pointer_x_ = 0;
  double pointer_y_ = 0;
  Hit hover_;
  Button pressed_button_ = Button::kNone;

  // The touch point pressing |pressed_button_|, if any, and where it is.
  std::optional<int32_t> button_touch_id_;
  double touch_x_ = 0;
  double touch_y_ = 0;
  std::chrono::steady_clock::time_point last_title_press_;

  // The cached title image and what it was rendered with.
  std::shared_ptr<const AlphaImage> title_image_;
  std::string title_image_text_;
  double title_image_scale_ = 0;
  int title_image_max_width_ = -1;

  FML_DISALLOW_COPY_AND_ASSIGN(WindowDecoration);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_DECORATION_H_
