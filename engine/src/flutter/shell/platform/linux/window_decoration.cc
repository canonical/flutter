// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/window_decoration.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>

namespace flutter {

namespace {

// Layout in logical pixels, following Material 3.
constexpr int kResizeOutset = 10;
constexpr int kResizeCorner = 20;
constexpr int kTopResizeInset = 4;
constexpr int kButtonDiameter = 32;
constexpr int kButtonSpacing = 6;
constexpr int kButtonMargin = 8;
constexpr double kIconStroke = 1.5;

constexpr auto kDoubleClickTime = std::chrono::milliseconds(400);

// Material 3 baseline colors.
using Color = DecorationScene::Color;
constexpr Color kActiveTitleBar = {0xf3 / 255.0, 0xed / 255.0, 0xf7 / 255.0,
                                   1.0};
constexpr Color kInactiveTitleBar = {0xfe / 255.0, 0xf7 / 255.0, 0xff / 255.0,
                                     1.0};
constexpr Color kOnSurfaceVariant = {0x49 / 255.0, 0x45 / 255.0, 0x4f / 255.0,
                                     1.0};
constexpr Color kOnSurface = {0x1d / 255.0, 0x1b / 255.0, 0x20 / 255.0, 1.0};

Color WithAlpha(Color color, float alpha) {
  color[3] = alpha;
  return color;
}

// Icon strokes on a 16x16 grid centred on the button.
struct Stroke {
  float x0, y0, x1, y1;
};
constexpr Stroke kMinimizeIcon[] = {{4, 11, 12, 11}};
constexpr Stroke kMaximizeIcon[] = {{4, 4, 12, 4},
                                    {12, 4, 12, 12},
                                    {12, 12, 4, 12},
                                    {4, 12, 4, 4}};
constexpr Stroke kRestoreIcon[] = {
    {3, 6, 10, 6}, {10, 6, 10, 13}, {10, 13, 3, 13}, {3, 13, 3, 6},
    {6, 3, 13, 3}, {13, 3, 13, 10}, {6, 3, 6, 6},    {10, 10, 13, 10}};
constexpr Stroke kCloseIcon[] = {{4, 4, 12, 12}, {12, 4, 4, 12}};

wl_region* CreateRegion(wl_compositor* compositor,
                        const std::vector<Rect>& rects) {
  wl_region* region = wl_compositor_create_region(compositor);
  for (const Rect& r : rects) {
    wl_region_add(region, r.x, r.y, r.width, r.height);
  }
  return region;
}

}  // namespace

WindowDecoration::WindowDecoration(WaylandDisplay* display,
                                   DecorationRenderer* renderer,
                                   Delegate* delegate,
                                   wl_surface* parent,
                                   xdg_toplevel* toplevel)
    : display_(display),
      renderer_(renderer),
      delegate_(delegate),
      toplevel_(toplevel) {
  surface_ = wl_compositor_create_surface(display_->compositor());
  subsurface_ = wl_subcompositor_get_subsurface(display_->subcompositor(),
                                                surface_, parent);
  // Changes are applied with the content, so resizes are atomic.
  wl_subsurface_set_sync(subsurface_);
  wl_subsurface_place_below(subsurface_, parent);
  wl_surface_set_buffer_transform(surface_, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  if (display_->viewporter() != nullptr) {
    viewport_ = wp_viewporter_get_viewport(display_->viewporter(), surface_);
  }
  if (display_->seat() != nullptr) {
    display_->seat()->SetInputHandler(surface_, this);
  }
}

WindowDecoration::~WindowDecoration() {
  if (display_->seat() != nullptr) {
    display_->seat()->SetInputHandler(surface_, nullptr);
  }
  if (renderer_->MakeCurrent()) {
    buffers_.clear();
  }
  if (viewport_ != nullptr) {
    wp_viewport_destroy(viewport_);
  }
  wl_subsurface_destroy(subsurface_);
  wl_surface_destroy(surface_);
}

void WindowDecoration::SetState(const State& state) {
  if (has_state_ && state == state_) {
    return;
  }
  state_ = state;
  has_state_ = true;
  // The buttons may have moved under the pointer.
  hover_ = Hit();
  pressed_button_ = Button::kNone;
  button_touch_id_.reset();
  Redraw();
}

bool WindowDecoration::floating() const {
  return !state_.maximized && !state_.fullscreen && !state_.tiled;
}

Rect WindowDecoration::WindowRect() const {
  int margin = floating() ? kShadowMargin : 0;
  return {margin, margin, state_.width, kTitleBarHeight + state_.height};
}

std::vector<WindowDecoration::Button> WindowDecoration::Buttons() const {
  std::vector<Button> buttons;
  if (state_.can_minimize) {
    buttons.push_back(Button::kMinimize);
  }
  if (state_.can_maximize) {
    buttons.push_back(Button::kMaximize);
  }
  buttons.push_back(Button::kClose);
  return buttons;
}

void WindowDecoration::ButtonCentre(Button button, double* x, double* y) const {
  std::vector<Button> buttons = Buttons();
  auto it = std::find(buttons.begin(), buttons.end(), button);
  int index_from_right = static_cast<int>(buttons.end() - it) - 1;
  *x = state_.width - kButtonMargin - kButtonDiameter / 2.0 -
       index_from_right * (kButtonDiameter + kButtonSpacing);
  *y = kTitleBarHeight / 2.0;
}

WindowDecoration::Hit WindowDecoration::HitTest(double x, double y) const {
  Rect window = WindowRect();
  double wx = x - window.x;
  double wy = y - window.y;

  Hit hit;
  if (floating()) {
    bool left = wx < 0;
    bool right = wx >= window.width;
    bool top = wy < 0;
    bool bottom = wy >= window.height;
    if (left || right) {
      top = top || wy < kResizeCorner;
      bottom = bottom || wy >= window.height - kResizeCorner;
    }
    if (top || bottom || wy < kTopResizeInset) {
      top = top || wy < kTopResizeInset;
      left = left || wx < kResizeCorner;
      right = right || wx >= window.width - kResizeCorner;
    }
    if (top) {
      hit.edges |= XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    }
    if (bottom) {
      hit.edges |= XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    }
    if (left) {
      hit.edges |= XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    }
    if (right) {
      hit.edges |= XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    }
    if (hit.edges != 0) {
      return hit;
    }
  }

  if (wy < 0 || wy >= kTitleBarHeight || wx < 0 || wx >= window.width) {
    return hit;
  }
  for (Button button : Buttons()) {
    double cx, cy;
    ButtonCentre(button, &cx, &cy);
    if (std::hypot(wx - cx, wy - cy) <= kButtonDiameter / 2.0) {
      hit.button = button;
      return hit;
    }
  }
  hit.title_bar = true;
  return hit;
}

void WindowDecoration::UpdatePointer(double x, double y) {
  pointer_x_ = x;
  pointer_y_ = y;
  Hit hit = HitTest(x, y);
  bool redraw = hit.button != hover_.button;
  hover_ = hit;

  WaylandSeat* seat = display_->seat();
  if (seat != nullptr) {
    uint32_t shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    switch (hit.edges) {
      case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE;
        break;
      case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
        break;
    }
    seat->SetCursor(shape);
  }

  if (redraw) {
    Redraw();
  }
}

void WindowDecoration::OnPointerEnter(uint64_t time, double x, double y) {
  UpdatePointer(x, y);
}

void WindowDecoration::OnPointerLeave(uint64_t time) {
  bool redraw = hover_.button != Button::kNone;
  hover_ = Hit();
  if (!button_touch_id_.has_value()) {
    redraw = redraw || pressed_button_ != Button::kNone;
    pressed_button_ = Button::kNone;
  }
  if (redraw) {
    Redraw();
  }
}

void WindowDecoration::OnPointerMotion(uint64_t time, double x, double y) {
  UpdatePointer(x, y);
}

void WindowDecoration::SetPressedButton(Button button) {
  if (button != pressed_button_) {
    pressed_button_ = button;
    Redraw();
  }
}

void WindowDecoration::ActivateButton(Button button) {
  switch (button) {
    case Button::kMinimize:
      xdg_toplevel_set_minimized(toplevel_);
      break;
    case Button::kMaximize:
      if (state_.maximized) {
        xdg_toplevel_unset_maximized(toplevel_);
      } else {
        xdg_toplevel_set_maximized(toplevel_);
      }
      break;
    case Button::kClose:
      delegate_->OnDecorationClose();
      break;
    case Button::kNone:
      break;
  }
}

void WindowDecoration::PressTitleBar(uint32_t serial) {
  auto now = std::chrono::steady_clock::now();
  if (now - last_title_press_ < kDoubleClickTime) {
    last_title_press_ = {};
    if (state_.can_maximize) {
      ActivateButton(Button::kMaximize);
    }
  } else {
    last_title_press_ = now;
    xdg_toplevel_move(toplevel_, display_->seat()->seat(), serial);
  }
}

void WindowDecoration::OnPointerButton(uint32_t serial,
                                       uint64_t time,
                                       uint32_t button,
                                       bool pressed) {
  if (button_touch_id_.has_value()) {
    return;
  }

  if (!pressed) {
    if (button != BTN_LEFT || pressed_button_ == Button::kNone) {
      return;
    }
    Button released = pressed_button_;
    SetPressedButton(Button::kNone);
    if (hover_.button == released) {
      ActivateButton(released);
    }
    return;
  }

  if (hover_.edges != 0) {
    if (button == BTN_LEFT) {
      xdg_toplevel_resize(toplevel_, display_->seat()->seat(), serial,
                          hover_.edges);
    }
  } else if (hover_.button != Button::kNone) {
    if (button == BTN_LEFT) {
      SetPressedButton(hover_.button);
    }
  } else if (hover_.title_bar) {
    if (button == BTN_LEFT) {
      PressTitleBar(serial);
    } else if (button == BTN_RIGHT) {
      Rect window = WindowRect();
      xdg_toplevel_show_window_menu(toplevel_, display_->seat()->seat(), serial,
                                    static_cast<int>(pointer_x_) - window.x,
                                    static_cast<int>(pointer_y_) - window.y);
    }
  }
}

void WindowDecoration::OnTouchDown(uint32_t serial,
                                   uint64_t time,
                                   int32_t id,
                                   double x,
                                   double y) {
  if (pressed_button_ != Button::kNone) {
    return;
  }
  Hit hit = HitTest(x, y);
  if (hit.edges != 0) {
    xdg_toplevel_resize(toplevel_, display_->seat()->seat(), serial, hit.edges);
  } else if (hit.button != Button::kNone) {
    button_touch_id_ = id;
    touch_x_ = x;
    touch_y_ = y;
    SetPressedButton(hit.button);
  } else if (hit.title_bar) {
    PressTitleBar(serial);
  }
}

void WindowDecoration::OnTouchMotion(uint64_t time,
                                     int32_t id,
                                     double x,
                                     double y) {
  if (button_touch_id_ == id) {
    touch_x_ = x;
    touch_y_ = y;
  }
}

void WindowDecoration::OnTouchUp(uint64_t time, int32_t id) {
  if (button_touch_id_ != id) {
    return;
  }
  button_touch_id_.reset();
  Button released = pressed_button_;
  SetPressedButton(Button::kNone);
  if (HitTest(touch_x_, touch_y_).button == released) {
    ActivateButton(released);
  }
}

void WindowDecoration::OnTouchCancel() {
  if (button_touch_id_.has_value()) {
    button_touch_id_.reset();
    SetPressedButton(Button::kNone);
  }
}

RenderBuffer* WindowDecoration::GetBuffer(int width, int height) {
  // Drop buffers that are the wrong size and not in use.
  buffers_.erase(
      std::remove_if(
          buffers_.begin(), buffers_.end(),
          [&](const std::unique_ptr<RenderBuffer>& buffer) {
            return (buffer->width() != width || buffer->height() != height) &&
                   !buffer->busy() &&
                   !delegate_->IsDecorationBufferPending(buffer.get());
          }),
      buffers_.end());

  for (const std::unique_ptr<RenderBuffer>& buffer : buffers_) {
    if (buffer->width() == width && buffer->height() == height &&
        !buffer->busy() &&
        !delegate_->IsDecorationBufferPending(buffer.get())) {
      return buffer.get();
    }
  }

  std::unique_ptr<RenderBuffer> buffer = renderer_->CreateBuffer(width, height);
  if (!buffer) {
    return nullptr;
  }
  buffers_.push_back(std::move(buffer));
  return buffers_.back().get();
}

std::shared_ptr<const AlphaImage> WindowDecoration::GetTitleImage(
    int max_width) {
  if (state_.title != title_image_text_ || state_.scale != title_image_scale_ ||
      max_width != title_image_max_width_) {
    title_image_ = renderer_->title_renderer()->Render(state_.title,
                                                       state_.scale, max_width);
    title_image_text_ = state_.title;
    title_image_scale_ = state_.scale;
    title_image_max_width_ = max_width;
  }
  return title_image_;
}

DecorationScene WindowDecoration::BuildScene() {
  float s = state_.scale;
  Rect window = WindowRect();

  DecorationScene scene;
  scene.window_x = window.x * s;
  scene.window_y = window.y * s;
  scene.window_width = window.width * s;
  scene.window_height = window.height * s;
  scene.corner_radius = floating() ? kCornerRadius * s : 0;
  scene.shadow = floating();
  scene.scale = s;
  scene.title_bar_height = kTitleBarHeight * s;
  scene.title_bar_color =
      state_.activated ? kActiveTitleBar : kInactiveTitleBar;

  // The title is centred on the window, so leave the same space on the left
  // as the buttons take on the right.
  std::vector<Button> buttons = Buttons();
  int buttons_width = kButtonMargin + static_cast<int>(buttons.size()) *
                                          (kButtonDiameter + kButtonSpacing);
  int max_title_width = static_cast<int>(
      std::floor(std::max(window.width - 2 * buttons_width, 0) * s));
  scene.title = GetTitleImage(max_title_width);
  if (scene.title) {
    scene.title_x = std::round((window.x + window.width / 2.0) * s -
                               scene.title->width / 2.0);
    scene.title_y = std::round((window.y + kTitleBarHeight / 2.0) * s -
                               scene.title->height / 2.0);
    scene.title_color =
        state_.activated ? kOnSurface : WithAlpha(kOnSurface, 0.38);
  }

  Button highlighted =
      pressed_button_ != Button::kNone ? pressed_button_ : hover_.button;
  for (Button button : buttons) {
    double cx, cy;
    ButtonCentre(button, &cx, &cy);
    cx += window.x;
    cy += window.y;

    if (button == highlighted) {
      float alpha = pressed_button_ == button ? 0.12 : 0.08;
      scene.circles.push_back(
          {static_cast<float>(cx * s), static_cast<float>(cy * s),
           kButtonDiameter / 2.0f * s, WithAlpha(kOnSurface, alpha)});
    }

    const Stroke* strokes = nullptr;
    size_t count = 0;
    switch (button) {
      case Button::kMinimize:
        strokes = kMinimizeIcon;
        count = std::size(kMinimizeIcon);
        break;
      case Button::kMaximize:
        if (state_.maximized) {
          strokes = kRestoreIcon;
          count = std::size(kRestoreIcon);
        } else {
          strokes = kMaximizeIcon;
          count = std::size(kMaximizeIcon);
        }
        break;
      case Button::kClose:
        strokes = kCloseIcon;
        count = std::size(kCloseIcon);
        break;
      case Button::kNone:
        break;
    }
    // Align strokes to pixel centres so they are crisp.
    auto snap = [&](double v) {
      return static_cast<float>(std::floor(v * s) + 0.5);
    };
    for (size_t i = 0; i < count; i++) {
      const Stroke& st = strokes[i];
      scene.lines.push_back({snap(cx - 8 + st.x0), snap(cy - 8 + st.y0),
                             snap(cx - 8 + st.x1), snap(cy - 8 + st.y1)});
    }
  }
  scene.line_width = kIconStroke * s;
  scene.line_color =
      state_.activated ? kOnSurfaceVariant : WithAlpha(kOnSurface, 0.38);

  return scene;
}

void WindowDecoration::Redraw() {
  if (!has_state_) {
    return;
  }

  DecorationFrame frame;
  frame.content_width =
      static_cast<int>(std::round(state_.width * state_.scale));
  frame.content_height =
      static_cast<int>(std::round(state_.height * state_.scale));

  if (state_.fullscreen) {
    frame.geometry = {0, 0, state_.width, state_.height};
    delegate_->OnDecorationFrame(frame);
    return;
  }

  int margin = floating() ? kShadowMargin : 0;
  int surface_height = floating() ? kTitleBarHeight + state_.height + 2 * margin
                                  : kTitleBarHeight;
  frame.surface = {-margin, -kTitleBarHeight - margin,
                   state_.width + 2 * margin, surface_height};
  frame.geometry = {0, -kTitleBarHeight, state_.width,
                    kTitleBarHeight + state_.height};

  Rect window = WindowRect();
  if (floating()) {
    frame.content_corner_radius = kCornerRadius;
    frame.input.push_back({window.x - kResizeOutset, window.y - kResizeOutset,
                           window.width + 2 * kResizeOutset,
                           window.height + 2 * kResizeOutset});
    frame.opaque.push_back({window.x + kCornerRadius, window.y,
                            window.width - 2 * kCornerRadius, kCornerRadius});
    frame.opaque.push_back({window.x, window.y + kCornerRadius, window.width,
                            kTitleBarHeight - kCornerRadius});
  } else {
    frame.input.push_back({0, 0, window.width, kTitleBarHeight});
    frame.opaque.push_back({0, 0, window.width, kTitleBarHeight});
  }

  int buffer_width =
      static_cast<int>(std::round(frame.surface.width * state_.scale));
  int buffer_height =
      static_cast<int>(std::round(frame.surface.height * state_.scale));
  if (buffer_width <= 0 || buffer_height <= 0) {
    return;
  }
  RenderBuffer* buffer = GetBuffer(buffer_width, buffer_height);
  if (buffer == nullptr || !renderer_->Draw(buffer, BuildScene())) {
    return;
  }
  frame.buffer = buffer;
  delegate_->OnDecorationFrame(frame);
}

void WindowDecoration::Apply(const DecorationFrame& frame) {
  wl_compositor* compositor = display_->compositor();
  wl_surface* surface = surface_;
  if (frame.buffer == nullptr) {
    wl_surface_attach(surface, nullptr, 0, 0);
    wl_surface_commit(surface);
    return;
  }
  frame.buffer->set_busy(true);
  wl_subsurface_set_position(subsurface_, frame.surface.x, frame.surface.y);
  wl_surface_attach(surface, frame.buffer->buffer(), 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, frame.buffer->width(),
                           frame.buffer->height());
  if (viewport_ != nullptr) {
    wp_viewport_set_destination(viewport_, frame.surface.width,
                                frame.surface.height);
  }
  wl_region* input = CreateRegion(compositor, frame.input);
  wl_surface_set_input_region(surface, input);
  wl_region_destroy(input);
  wl_region* opaque = CreateRegion(compositor, frame.opaque);
  wl_surface_set_opaque_region(surface, opaque);
  wl_region_destroy(opaque);
  wl_surface_commit(surface);
}

}  // namespace flutter
