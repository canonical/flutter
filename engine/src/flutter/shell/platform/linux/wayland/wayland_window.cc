// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/wayland_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/wayland/drm_timeline.h"

namespace flutter {

namespace {

// The fractional scale protocol expresses scales in 120ths.
constexpr double kFractionalScaleDenominator = 120.0;

// The smallest decorated window, in logical pixels.
constexpr int kMinimumWidth = 160;
constexpr int kMinimumContentHeight = 20;

// The framework's adjustment bits are used by the protocol.
static_assert(Window::ConstraintAdjustment::kSlideX ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X);
static_assert(Window::ConstraintAdjustment::kSlideY ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
static_assert(Window::ConstraintAdjustment::kFlipX ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X);
static_assert(Window::ConstraintAdjustment::kFlipY ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
static_assert(Window::ConstraintAdjustment::kResizeX ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X);
static_assert(Window::ConstraintAdjustment::kResizeY ==
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y);

xdg_positioner_anchor ToXdgAnchor(Window::Anchor anchor) {
  switch (anchor) {
    case Window::Anchor::kCenter:
      return XDG_POSITIONER_ANCHOR_NONE;
    case Window::Anchor::kTop:
      return XDG_POSITIONER_ANCHOR_TOP;
    case Window::Anchor::kBottom:
      return XDG_POSITIONER_ANCHOR_BOTTOM;
    case Window::Anchor::kLeft:
      return XDG_POSITIONER_ANCHOR_LEFT;
    case Window::Anchor::kRight:
      return XDG_POSITIONER_ANCHOR_RIGHT;
    case Window::Anchor::kTopLeft:
      return XDG_POSITIONER_ANCHOR_TOP_LEFT;
    case Window::Anchor::kBottomLeft:
      return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
    case Window::Anchor::kTopRight:
      return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
    case Window::Anchor::kBottomRight:
      return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
  }
  return XDG_POSITIONER_ANCHOR_NONE;
}

xdg_positioner_gravity ToXdgGravity(Window::Anchor gravity) {
  switch (gravity) {
    case Window::Anchor::kCenter:
      return XDG_POSITIONER_GRAVITY_NONE;
    case Window::Anchor::kTop:
      return XDG_POSITIONER_GRAVITY_TOP;
    case Window::Anchor::kBottom:
      return XDG_POSITIONER_GRAVITY_BOTTOM;
    case Window::Anchor::kLeft:
      return XDG_POSITIONER_GRAVITY_LEFT;
    case Window::Anchor::kRight:
      return XDG_POSITIONER_GRAVITY_RIGHT;
    case Window::Anchor::kTopLeft:
      return XDG_POSITIONER_GRAVITY_TOP_LEFT;
    case Window::Anchor::kBottomLeft:
      return XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
    case Window::Anchor::kTopRight:
      return XDG_POSITIONER_GRAVITY_TOP_RIGHT;
    case Window::Anchor::kBottomRight:
      return XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
  }
  return XDG_POSITIONER_GRAVITY_NONE;
}

// Calls |callback| with each uint32_t in |array|.
template <typename Callback>
void ForEachUint32(wl_array* array, Callback callback) {
  const auto* values = static_cast<const uint32_t*>(array->data);
  for (size_t i = 0; i < array->size / sizeof(uint32_t); i++) {
    callback(values[i]);
  }
}

}  // namespace

const wl_surface_listener WaylandWindow::kSurfaceListener = {
    .enter = SurfaceEnter,
    .leave = SurfaceLeave,
    .preferred_buffer_scale = SurfacePreferredBufferScale,
    .preferred_buffer_transform = SurfacePreferredBufferTransform,
};

const xdg_surface_listener WaylandWindow::kXdgSurfaceListener = {
    .configure = XdgSurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::kToplevelListener = {
    .configure = ToplevelConfigure,
    .close = ToplevelClose,
    .configure_bounds = ToplevelConfigureBounds,
    .wm_capabilities = ToplevelWmCapabilities,
};

const xdg_popup_listener WaylandWindow::kPopupListener = {
    .configure = PopupConfigure,
    .popup_done = PopupDone,
    .repositioned = PopupRepositioned,
};

const zxdg_toplevel_decoration_v1_listener
    WaylandWindow::kToplevelDecorationListener = {
        .configure = ToplevelDecorationConfigure,
};

const xdg_activation_token_v1_listener WaylandWindow::kActivationTokenListener =
    {
        .done = ActivationTokenDone,
};

const wp_presentation_feedback_listener WaylandWindow::kFeedbackListener = {
    .sync_output = FeedbackSyncOutput,
    .presented = FeedbackPresented,
    .discarded = FeedbackDiscarded,
};

const wl_callback_listener WaylandWindow::kFrameListener = {
    .done = FrameDone,
};

const wp_fractional_scale_v1_listener WaylandWindow::kFractionalScaleListener =
    {
        .preferred_scale = PreferredScale,
};

std::unique_ptr<WaylandWindow> WaylandWindow::Create(
    WaylandDisplay* display,
    DecorationRenderer* decoration_renderer,
    Window::Delegate* delegate,
    const Params& params) {
  std::unique_ptr<WaylandWindow> window(new WaylandWindow(display, delegate));
  if (!window->Initialize(decoration_renderer, params)) {
    return nullptr;
  }
  return window;
}

WaylandWindow::WaylandWindow(WaylandDisplay* display,
                             Window::Delegate* delegate)
    : display_(display), delegate_(delegate) {
  display_->AddOutputObserver(this);
}

WaylandWindow::~WaylandWindow() {
  display_->RemoveOutputObserver(this);
  {
    std::lock_guard<std::mutex> lock(surface_mutex_);
    for (const auto& view : platform_views_) {
      view->Detach(this);
    }
    platform_views_.clear();
    overlays_.clear();
  }
  decoration_.reset();
  if (toplevel_decoration_ != nullptr) {
    zxdg_toplevel_decoration_v1_destroy(toplevel_decoration_);
  }
  if (activation_token_ != nullptr) {
    xdg_activation_token_v1_destroy(activation_token_);
  }
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    for (wl_callback* callback : pending_frames_) {
      wl_callback_destroy(callback);
    }
    pending_frames_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    for (struct wp_presentation_feedback* feedback : pending_feedback_) {
      wp_presentation_feedback_destroy(feedback);
    }
    pending_feedback_.clear();
  }
  if (fifo_ != nullptr) {
    wp_fifo_v1_destroy(fifo_);
  }
  if (syncobj_surface_ != nullptr) {
    wp_linux_drm_syncobj_surface_v1_destroy(syncobj_surface_);
  }
  if (fractional_scale_ != nullptr) {
    wp_fractional_scale_v1_destroy(fractional_scale_);
  }
  if (viewport_ != nullptr) {
    wp_viewport_destroy(viewport_);
  }
  if (dialog_ != nullptr) {
    xdg_dialog_v1_destroy(dialog_);
  }
  if (toplevel_ != nullptr) {
    xdg_toplevel_destroy(toplevel_);
  }
  if (popup_ != nullptr) {
    xdg_popup_destroy(popup_);
  }
  if (xdg_surface_ != nullptr) {
    xdg_surface_destroy(xdg_surface_);
  }
  if (surface_ != nullptr) {
    wl_surface_destroy(surface_);
  }
  wl_display_flush(display_->display());
}

bool WaylandWindow::Initialize(DecorationRenderer* decoration_renderer,
                               const Params& params) {
  role_ = params.role;
  parent_ = static_cast<WaylandWindow*>(params.parent);
  decoration_renderer_ = decoration_renderer;
  decorated_ = params.decorated && role_ == Role::kToplevel;
  resizable_ = params.resizable;
  modal_ = params.modal;
  title_ = params.title;
  if (role_ == Role::kToplevel) {
    limits_ = params.size_limits;
  } else {
    if (parent_ == nullptr) {
      FML_LOG(ERROR) << "Popups require a parent window";
      return false;
    }
    positioner_ = params.positioner;
  }
  int width = std::max(params.width, 1);
  int height = std::max(params.height, 1);
  ClampSize(&width, &height);
  width_ = width;
  height_ = height;
  floating_width_ = width;
  floating_height_ = height;

  // Objects that receive events are created on a private queue until the
  // window is configured, so waiting for the configure doesn't dispatch
  // events for other windows. This may be called from Dart code, which
  // shouldn't be re-entered by events.
  wl_display* wl_display = display_->display();
  wl_event_queue* queue = wl_display_create_queue(wl_display);
  auto create_wrapper = [queue](auto* proxy) {
    auto* wrapper =
        static_cast<decltype(proxy)>(wl_proxy_create_wrapper(proxy));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
    return wrapper;
  };

  wl_compositor* compositor = create_wrapper(display_->compositor());
  surface_ = wl_compositor_create_surface(compositor);
  wl_proxy_wrapper_destroy(compositor);
  wl_surface_add_listener(surface_, &kSurfaceListener, this);

  // OpenGL renders the bottom row first, so have the compositor flip the
  // buffers vertically rather than doing an extra copy.
  wl_surface_set_buffer_transform(surface_, WL_OUTPUT_TRANSFORM_FLIPPED_180);

  // Fractional scaling works by rendering at the scaled size and having the
  // viewport scale back to the logical size.
  if (display_->viewporter() != nullptr) {
    viewport_ = wp_viewporter_get_viewport(display_->viewporter(), surface_);
    if (display_->fractional_scale_manager() != nullptr) {
      wp_fractional_scale_manager_v1* manager =
          create_wrapper(display_->fractional_scale_manager());
      fractional_scale_ = wp_fractional_scale_manager_v1_get_fractional_scale(
          manager, surface_);
      wl_proxy_wrapper_destroy(manager);
      wp_fractional_scale_v1_add_listener(fractional_scale_,
                                          &kFractionalScaleListener, this);
    }
  }

  if (display_->fifo_manager() != nullptr) {
    fifo_ = wp_fifo_manager_v1_get_fifo(display_->fifo_manager(), surface_);
  }

  xdg_wm_base* wm_base = create_wrapper(display_->wm_base());
  xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_base, surface_);
  wl_proxy_wrapper_destroy(wm_base);
  xdg_surface_add_listener(xdg_surface_, &kXdgSurfaceListener, this);

  if (role_ == Role::kToplevel) {
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    if (!title_.empty()) {
      xdg_toplevel_set_title(toplevel_, title_.c_str());
    }
    if (!params.application_id.empty()) {
      xdg_toplevel_set_app_id(toplevel_, params.application_id.c_str());
    }
    if (parent_ != nullptr) {
      SetParent(parent_);
    }
    if (params.modal && display_->wm_dialog() != nullptr) {
      dialog_ =
          xdg_wm_dialog_v1_get_xdg_dialog(display_->wm_dialog(), toplevel_);
      xdg_dialog_v1_set_modal(dialog_);
    }
    if (display_->decoration_manager() != nullptr) {
      // Draw our own decorations so windows look the same on every desktop,
      // but use the compositor's if it insists or we can't draw them.
      // Undecorated windows ask for client-side decorations, so the
      // compositor doesn't add any. The mode is applied on the configure.
      zxdg_decoration_manager_v1* manager =
          create_wrapper(display_->decoration_manager());
      toplevel_decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(
          manager, toplevel_);
      wl_proxy_wrapper_destroy(manager);
      zxdg_toplevel_decoration_v1_add_listener(
          toplevel_decoration_, &kToplevelDecorationListener, this);
      zxdg_toplevel_decoration_v1_set_mode(
          toplevel_decoration_,
          decorated_ && !CanDrawDecorations()
              ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
              : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    } else {
      SetClientSideDecorations(true);
    }
    UpdateSizeLimits();
  } else {
    positioned_width_ = width_;
    positioned_height_ = height_;
    xdg_positioner* positioner =
        CreatePositioner(positioner_, positioned_width_, positioned_height_);
    popup_ =
        xdg_surface_get_popup(xdg_surface_, parent_->xdg_surface_, positioner);
    xdg_positioner_destroy(positioner);
    xdg_popup_add_listener(popup_, &kPopupListener, this);
    if (!params.accepts_input) {
      wl_region* region = wl_compositor_create_region(display_->compositor());
      wl_surface_set_input_region(surface_, region);
      wl_region_destroy(region);
    }
  }

  // Commit without a buffer to have the compositor configure the window.
  wl_surface_commit(surface_);
  bool failed = false;
  while (!configured_) {
    if (wl_display_roundtrip_queue(wl_display, queue) < 0) {
      FML_LOG(ERROR) << "Failed to configure window";
      failed = true;
      break;
    }
  }

  // Move the objects to the default queue, and handle any events that
  // arrived after the configure.
  for (void* proxy :
       std::initializer_list<void*>{surface_, fractional_scale_, xdg_surface_,
                                    toplevel_, toplevel_decoration_, popup_}) {
    if (proxy != nullptr) {
      wl_proxy_set_queue(static_cast<wl_proxy*>(proxy), nullptr);
    }
  }
  wl_display_dispatch_queue_pending(wl_display, queue);
  wl_event_queue_destroy(queue);

  return !failed;
}

int WaylandWindow::title_bar_height() const {
  return decoration_ && !fullscreen_ ? WindowDecoration::kTitleBarHeight : 0;
}

int WaylandWindow::geometry_x() const {
  return 0;
}

int WaylandWindow::geometry_y() const {
  return -title_bar_height();
}

int WaylandWindow::popup_x() const {
  return popup_geometry_x_ + (parent_ != nullptr ? parent_->geometry_x() : 0);
}

int WaylandWindow::popup_y() const {
  return popup_geometry_y_ + (parent_ != nullptr ? parent_->geometry_y() : 0);
}

int WaylandWindow::max_suggested_width() const {
  return bounds_width_;
}

int WaylandWindow::max_suggested_height() const {
  return bounds_height_ > 0 ? std::max(bounds_height_ - title_bar_height(), 1)
                            : 0;
}

xdg_positioner* WaylandWindow::CreatePositioner(const Positioner& positioner,
                                                int width,
                                                int height) {
  xdg_positioner* result = xdg_wm_base_create_positioner(display_->wm_base());
  xdg_positioner_set_size(result, std::max(width, 1), std::max(height, 1));
  xdg_positioner_set_anchor_rect(
      result, positioner.anchor_rect.x - parent_->geometry_x(),
      positioner.anchor_rect.y - parent_->geometry_y(),
      std::max(positioner.anchor_rect.width, 1),
      std::max(positioner.anchor_rect.height, 1));
  xdg_positioner_set_anchor(result, ToXdgAnchor(positioner.anchor));
  xdg_positioner_set_gravity(result, ToXdgGravity(positioner.gravity));
  xdg_positioner_set_constraint_adjustment(result,
                                           positioner.constraint_adjustment);
  xdg_positioner_set_offset(result, positioner.offset_x, positioner.offset_y);
  if (xdg_positioner_get_version(result) >=
      XDG_POSITIONER_SET_REACTIVE_SINCE_VERSION) {
    // Keep the popup in place as the parent moves.
    xdg_positioner_set_reactive(result);
  }
  return result;
}

void WaylandWindow::SetPositioner(const Positioner& positioner) {
  if (popup_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(surface_mutex_);
  positioner_ = positioner;
  if (xdg_popup_get_version(popup_) < XDG_POPUP_REPOSITION_SINCE_VERSION) {
    return;
  }
  xdg_positioner* result =
      CreatePositioner(positioner_, positioned_width_, positioned_height_);
  xdg_popup_reposition(popup_, result, ++reposition_token_);
  xdg_positioner_destroy(result);
  wl_display_flush(display_->display());
}

void WaylandWindow::StopWaiting() {
  {
    std::lock_guard<std::mutex> lock(surface_mutex_);
    stop_waiting_ = true;
  }
  reposition_condition_.notify_all();
}

void WaylandWindow::RepositionForSize(std::unique_lock<std::mutex>& lock,
                                      int width,
                                      int height) {
  if (!configured_ || stop_waiting_ ||
      xdg_popup_get_version(popup_) < XDG_POPUP_REPOSITION_SINCE_VERSION ||
      (width == positioned_width_ && height == positioned_height_)) {
    return;
  }
  positioned_width_ = width;
  positioned_height_ = height;
  uint32_t token = ++reposition_token_;
  xdg_positioner* positioner = CreatePositioner(positioner_, width, height);
  xdg_popup_reposition(popup_, positioner, token);
  xdg_positioner_destroy(positioner);
  wl_display_flush(display_->display());

  // The configure is acknowledged on the platform thread, and applies with
  // the next commit, which will contain the buffer of the new size. Give up
  // after a while in case the compositor doesn't respond, as frames would
  // otherwise stop.
  awaited_reposition_token_ = token;
  reposition_condition_.wait_for(lock, std::chrono::milliseconds(100), [&]() {
    return stop_waiting_ ||
           static_cast<int32_t>(acked_reposition_token_ - token) >= 0;
  });
  awaited_reposition_token_.reset();
}

void WaylandWindow::ClampSize(int* width, int* height) const {
  *width = std::max(*width, limits_.min_width);
  *height = std::max(*height, limits_.min_height);
  if (limits_.max_width > 0) {
    *width = std::min(*width, std::max(limits_.max_width, limits_.min_width));
  }
  if (limits_.max_height > 0) {
    *height =
        std::min(*height, std::max(limits_.max_height, limits_.min_height));
  }
  *width = std::max(*width, 1);
  *height = std::max(*height, 1);
}

void WaylandWindow::UpdateSizeLimits() {
  if (toplevel_ == nullptr) {
    return;
  }

  // The limits are of the window geometry, which includes the title bar.
  int title_bar = title_bar_height();
  int min_width = limits_.min_width;
  int min_height = limits_.min_height + title_bar;
  int max_width = limits_.max_width;
  int max_height = limits_.max_height > 0 ? limits_.max_height + title_bar : 0;
  if (decoration_) {
    min_width = std::max(min_width, kMinimumWidth);
    min_height = std::max(min_height, title_bar + kMinimumContentHeight);
  }
  if (!resizable_) {
    min_width = max_width = width_;
    min_height = max_height = height_ + title_bar;
  }
  if (max_width > 0) {
    max_width = std::max(max_width, min_width);
  }
  if (max_height > 0) {
    max_height = std::max(max_height, min_height);
  }
  xdg_toplevel_set_min_size(toplevel_, min_width, min_height);
  xdg_toplevel_set_max_size(toplevel_, max_width, max_height);

  // Once configured, the limits apply on the next commit.
  if (configured_) {
    std::lock_guard<std::mutex> lock(surface_mutex_);
    wl_surface_commit(surface_);
    wl_display_flush(display_->display());
  }
}

void WaylandWindow::Resize(int width, int height) {
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;
  UpdateDecoration();
  delegate_->OnWindowConfigure();
}

void WaylandWindow::SetSize(int width, int height) {
  if (toplevel_ == nullptr) {
    return;
  }
  ClampSize(&width, &height);
  floating_width_ = width;
  floating_height_ = height;
  if (!maximized_ && !fullscreen_ && !tiled_) {
    Resize(width, height);
    if (!resizable_) {
      UpdateSizeLimits();
    }
  }
}

void WaylandWindow::ResizeToContent(int width, int height) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;
  if (!maximized_ && !fullscreen_ && !tiled_) {
    floating_width_ = width;
    floating_height_ = height;
  }
  UpdateDecoration();
  if (!resizable_) {
    UpdateSizeLimits();
  }
  delegate_->OnWindowStateChanged();
}

void WaylandWindow::SetSizeLimits(const SizeLimits& limits) {
  if (toplevel_ == nullptr) {
    return;
  }
  limits_ = limits;
  UpdateSizeLimits();

  // The compositor may not reconfigure the window, so apply the limits now.
  int width = floating_width_;
  int height = floating_height_;
  ClampSize(&width, &height);
  floating_width_ = width;
  floating_height_ = height;
  if (!maximized_ && !fullscreen_ && !tiled_) {
    Resize(width, height);
  }
}

void WaylandWindow::SetMaximized(bool maximized) {
  if (toplevel_ == nullptr) {
    return;
  }
  if (maximized) {
    xdg_toplevel_set_maximized(toplevel_);
  } else {
    xdg_toplevel_unset_maximized(toplevel_);
  }
  wl_display_flush(display_->display());
}

void WaylandWindow::SetMinimized() {
  if (toplevel_ == nullptr) {
    return;
  }
  xdg_toplevel_set_minimized(toplevel_);
  wl_display_flush(display_->display());
}

void WaylandWindow::SetFullscreen(bool fullscreen) {
  if (toplevel_ == nullptr) {
    return;
  }
  if (fullscreen) {
    xdg_toplevel_set_fullscreen(toplevel_, nullptr);
  } else {
    xdg_toplevel_unset_fullscreen(toplevel_);
  }
  wl_display_flush(display_->display());
}

void WaylandWindow::Activate() {
  xdg_activation_v1* activation = display_->activation();
  if (toplevel_ == nullptr || activation == nullptr ||
      activation_token_ != nullptr) {
    return;
  }

  // The compositor decides whether to allow the activation using the most
  // recent user input.
  activation_token_ = xdg_activation_v1_get_activation_token(activation);
  xdg_activation_token_v1_add_listener(activation_token_,
                                       &kActivationTokenListener, this);
  WaylandSeat* seat = display_->seat();
  if (seat != nullptr && seat->last_input_serial() != 0) {
    xdg_activation_token_v1_set_serial(activation_token_,
                                       seat->last_input_serial(), seat->seat());
    if (seat->keyboard_focus() != nullptr) {
      xdg_activation_token_v1_set_surface(activation_token_,
                                          seat->keyboard_focus());
    }
  }
  xdg_activation_token_v1_commit(activation_token_);
  wl_display_flush(display_->display());
}

void WaylandWindow::ActivationTokenDone(void* data,
                                        xdg_activation_token_v1* token,
                                        const char* token_string) {
  auto* self = static_cast<WaylandWindow*>(data);
  xdg_activation_v1_activate(self->display_->activation(), token_string,
                             self->surface_);
  xdg_activation_token_v1_destroy(token);
  self->activation_token_ = nullptr;
}

void WaylandWindow::SetParent(Window* new_parent) {
  if (toplevel_ == nullptr) {
    return;
  }
  auto* parent = static_cast<WaylandWindow*>(new_parent);
  parent_ = parent;

  // Popups can't be parents of top-level windows, so use the top-level
  // window they belong to.
  while (parent != nullptr && parent->toplevel_ == nullptr) {
    parent = parent->parent_;
  }
  xdg_toplevel_set_parent(toplevel_,
                          parent != nullptr ? parent->toplevel_ : nullptr);
  wl_display_flush(display_->display());
}

int WaylandWindow::physical_width() const {
  return static_cast<int>(std::round(width_ * scale_));
}

int WaylandWindow::physical_height() const {
  return static_cast<int>(std::round(height_ * scale_));
}

void WaylandWindow::SetTitle(const std::string& title) {
  if (toplevel_ == nullptr) {
    return;
  }
  xdg_toplevel_set_title(toplevel_, title.c_str());
  title_ = title;
  UpdateDecoration();
  delegate_->OnWindowTitleChanged();
}

void WaylandWindow::UpdateDecoration() {
  if (!decoration_ || !configured_) {
    return;
  }
  WindowDecoration::State state;
  state.width = width_;
  state.height = height_;
  state.scale = scale_;
  state.maximized = maximized_;
  state.fullscreen = fullscreen_;
  state.tiled = tiled_;
  state.activated = activated_;
  state.can_minimize = can_minimize_;
  state.can_maximize = can_maximize_ && resizable_;
  state.title = title_;
  decoration_->SetState(state);
}

bool WaylandWindow::CanDrawDecorations() const {
  return decoration_renderer_ != nullptr &&
         display_->subcompositor() != nullptr;
}

void WaylandWindow::SetClientSideDecorations(bool client_side) {
  if (client_side == (decoration_ != nullptr)) {
    return;
  }
  if (client_side) {
    if (!decorated_ || !CanDrawDecorations()) {
      return;
    }
    auto decoration = std::make_unique<WindowDecoration>(
        display_, decoration_renderer_, this, surface_, toplevel_);
    std::lock_guard<std::mutex> lock(surface_mutex_);
    decoration_ = std::move(decoration);
  } else {
    std::lock_guard<std::mutex> lock(surface_mutex_);
    pending_decoration_.reset();
    content_corner_radius_ = 0;
    decoration_.reset();
    if (window_geometry_set_) {
      // The geometry can't be unset, so make it the content.
      xdg_surface_set_window_geometry(xdg_surface_, 0, 0, width_, height_);
    }
  }
}

void WaylandWindow::ApplyDecorationFrame(const DecorationFrame& frame) {
  decoration_->Apply(frame);
  content_corner_radius_ = frame.content_corner_radius;
  window_geometry_set_ = true;
  xdg_surface_set_window_geometry(xdg_surface_, frame.geometry.x,
                                  frame.geometry.y, frame.geometry.width,
                                  frame.geometry.height);
}

void WaylandWindow::OnDecorationFrame(const DecorationFrame& frame) {
  std::lock_guard<std::mutex> lock(surface_mutex_);
  // If the content already matches, show the decorations now. Otherwise
  // wait for content of the new size so the two change together.
  if (frame.content_width == presented_width_ &&
      frame.content_height == presented_height_) {
    pending_decoration_.reset();
    ApplyDecorationFrame(frame);
    wl_surface_commit(surface_);
    wl_display_flush(display_->display());
  } else {
    pending_decoration_ = frame;
  }
}

void WaylandWindow::OnDecorationClose() {
  delegate_->OnWindowClose();
}

bool WaylandWindow::IsDecorationBufferPending(RenderBuffer* buffer) {
  std::lock_guard<std::mutex> lock(surface_mutex_);
  return pending_decoration_.has_value() && pending_decoration_->Uses(buffer);
}

void WaylandWindow::SetApplicationId(const std::string& application_id) {
  if (toplevel_ == nullptr) {
    return;
  }
  xdg_toplevel_set_app_id(toplevel_, application_id.c_str());
}

void WaylandWindow::Present(RenderBuffer* render_buffer,
                            const SyncPoints* sync,
                            const std::vector<Layer>& layers) {
  wl_buffer* buffer = render_buffer->buffer();
  int width = render_buffer->width();
  int height = render_buffer->height();
  std::unique_lock<std::mutex> surface_lock(surface_mutex_);
  if (popup_ != nullptr) {
    // The popup is placed using its logical size, matching the viewport
    // destination below.
    double scale = scale_;
    RepositionForSize(
        surface_lock, std::max(static_cast<int>(std::round(width / scale)), 1),
        std::max(static_cast<int>(std::round(height / scale)), 1));
  }
  if (pending_decoration_.has_value() &&
      pending_decoration_->content_width == width &&
      pending_decoration_->content_height == height) {
    ApplyDecorationFrame(*pending_decoration_);
    pending_decoration_.reset();
  }
  presented_width_ = width;
  presented_height_ = height;

  if (sync != nullptr) {
    if (syncobj_surface_ == nullptr) {
      syncobj_surface_ = wp_linux_drm_syncobj_manager_v1_get_surface(
          display_->syncobj_manager(), surface_);
    }
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(
        syncobj_surface_, sync->timeline->timeline(), sync->acquire_point >> 32,
        sync->acquire_point & 0xffffffff);
    wp_linux_drm_syncobj_surface_v1_set_release_point(
        syncobj_surface_, sync->timeline->timeline(), sync->release_point >> 32,
        sync->release_point & 0xffffffff);
  }
  if (window_geometry_set_ && !decoration_) {
    double scale = scale_;
    xdg_surface_set_window_geometry(
        xdg_surface_, 0, 0,
        std::max(static_cast<int>(std::round(width / scale)), 1),
        std::max(static_cast<int>(std::round(height / scale)), 1));
  }
  wl_surface_attach(surface_, buffer, 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, width, height);
  if (viewport_ != nullptr) {
    double scale = scale_;
    wp_viewport_set_destination(viewport_,
                                static_cast<int>(std::round(width / scale)),
                                static_cast<int>(std::round(height / scale)));
  }
  PresentLayers(layers, width, height, sync != nullptr);
  if (fifo_ != nullptr) {
    // Keep the previous frame on screen for at least one refresh, so frames
    // are not discarded if they are drawn faster than the display refreshes.
    if (fifo_barrier_set_) {
      wp_fifo_v1_wait_barrier(fifo_);
    }
    wp_fifo_v1_set_barrier(fifo_);
    fifo_barrier_set_ = true;
  }
  {
    // As with presentation feedback, the callback is on the default queue
    // and no events are sent before the commit.
    std::lock_guard<std::mutex> lock(frame_mutex_);
    wl_callback* callback = wl_surface_frame(surface_);
    wl_callback_add_listener(callback, &kFrameListener, this);
    pending_frames_.insert(callback);
  }
  if (display_->presentation() != nullptr) {
    // The feedback is created on the default queue, so its events are
    // delivered on the platform thread. No events are sent before the commit,
    // so the listener can be safely added here.
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    struct wp_presentation_feedback* feedback =
        wp_presentation_feedback(display_->presentation(), surface_);
    wp_presentation_feedback_add_listener(feedback, &kFeedbackListener, this);
    pending_feedback_.insert(feedback);
  }
  wl_surface_commit(surface_);
  wl_display_flush(display_->display());
}

void WaylandWindow::PresentLayers(const std::vector<Layer>& layers,
                                  int width,
                                  int height,
                                  bool explicit_sync) {
  if (layers.empty() && overlays_.empty() && platform_views_.empty()) {
    return;
  }

  // Each layer is placed above the one before it, starting with the content.
  wl_surface* above = surface_;
  size_t n_overlays = 0;
  std::vector<std::shared_ptr<LinuxPlatformView>> shown_views;
  for (const Layer& layer : layers) {
    if (layer.platform_view != nullptr) {
      wl_surface* view_surface = layer.platform_view->Show(
          this, surface_, above, layer.geometry, scale_);
      if (view_surface != nullptr) {
        above = view_surface;
      }
      shown_views.push_back(layer.platform_view);
      continue;
    }

    if (n_overlays == overlays_.size()) {
      overlays_.push_back(std::make_unique<Overlay>(display_, surface_));
    }
    Overlay* overlay = overlays_[n_overlays++].get();
    PresentOverlay(overlay, above, layer, width, height, explicit_sync);
    above = overlay->surface;
  }

  // Hide what isn't in this frame.
  for (size_t i = n_overlays; i < overlays_.size(); i++) {
    Overlay* overlay = overlays_[i].get();
    if (overlay->mapped) {
      wl_surface_attach(overlay->surface, nullptr, 0, 0);
      wl_surface_commit(overlay->surface);
      overlay->mapped = false;
    }
  }
  for (const auto& view : platform_views_) {
    if (std::find(shown_views.begin(), shown_views.end(), view) ==
            shown_views.end() &&
        view->IsInWindow(this)) {
      view->Hide(this);
      shown_views.push_back(view);
    }
  }
  platform_views_ = std::move(shown_views);
}

void WaylandWindow::PresentOverlay(Overlay* overlay,
                                   wl_surface* above,
                                   const Layer& layer,
                                   int width,
                                   int height,
                                   bool explicit_sync) {
  wl_subsurface_place_above(overlay->subsurface, above);

  // Only show the part with content, so the compositor has less to blend.
  // The area is expanded to whole logical pixels, as that is how
  // subsurfaces are placed.
  double scale = overlay->viewport != nullptr ? scale_.load() : 1.0;
  PixelRect content = layer.content.Intersect(
      {0, 0, static_cast<double>(width), static_cast<double>(height)});
  if (!content.empty()) {
    double left = std::floor(content.x / scale) * scale;
    double top = std::floor(content.y / scale) * scale;
    double right = std::ceil(content.right() / scale) * scale;
    double bottom = std::ceil(content.bottom() / scale) * scale;
    content = PixelRect{left, top, right - left, bottom - top}.Intersect(
        {0, 0, static_cast<double>(width), static_cast<double>(height)});
  }
  PixelRect full = {0, 0, static_cast<double>(width),
                    static_cast<double>(height)};
  SurfacePlacement placement;
  if (!PlaceBuffer(width, height, full, content, scale, &placement) &&
      !PlaceBuffer(width, height, full, full, scale, &placement)) {
    return;
  }

  if (!overlay->placed || placement.x != overlay->placement.x ||
      placement.y != overlay->placement.y) {
    wl_subsurface_set_position(overlay->subsurface, placement.x, placement.y);
  }
  if (overlay->viewport != nullptr &&
      (!overlay->placed || !(placement == overlay->placement))) {
    wp_viewport_set_source(overlay->viewport, placement.source_x,
                           placement.source_y, placement.source_width,
                           placement.source_height);
    wp_viewport_set_destination(overlay->viewport, placement.width,
                                placement.height);
  }
  overlay->placement = placement;
  overlay->placed = true;

  if (explicit_sync) {
    if (overlay->syncobj_surface == nullptr) {
      overlay->syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(
          display_->syncobj_manager(), overlay->surface);
    }
    const SyncPoints& sync = layer.sync;
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(
        overlay->syncobj_surface, sync.timeline->timeline(),
        sync.acquire_point >> 32, sync.acquire_point & 0xffffffff);
    wp_linux_drm_syncobj_surface_v1_set_release_point(
        overlay->syncobj_surface, sync.timeline->timeline(),
        sync.release_point >> 32, sync.release_point & 0xffffffff);
  }
  wl_surface_attach(overlay->surface, layer.buffer->buffer(), 0, 0);
  wl_surface_damage_buffer(overlay->surface, 0, 0, width, height);
  wl_surface_commit(overlay->surface);
  overlay->mapped = true;
}

void WaylandWindow::CommitPlatformView(LinuxPlatformView* view) {
  std::lock_guard<std::mutex> lock(surface_mutex_);
  if (!view->CommitBuffer(this)) {
    return;
  }
  // The view's surface is synchronized with this one, so commit this to
  // apply it. Nothing else is pending, as the content and decorations are
  // only changed with this lock held and committed straight away.
  wl_surface_commit(surface_);
  wl_display_flush(display_->display());
}

void WaylandWindow::RemovePlatformView(LinuxPlatformView* view) {
  std::lock_guard<std::mutex> lock(surface_mutex_);
  view->Detach(this);
  platform_views_.erase(
      std::remove_if(platform_views_.begin(), platform_views_.end(),
                     [view](const auto& v) { return v.get() == view; }),
      platform_views_.end());
}

WaylandWindow::Overlay::Overlay(WaylandDisplay* display, wl_surface* parent)
    : display(display) {
  surface = wl_compositor_create_surface(display->compositor());
  subsurface = wl_subcompositor_get_subsurface(display->subcompositor(),
                                               surface, parent);
  // Changes are applied with the content, so frames are atomic.
  wl_subsurface_set_sync(subsurface);
  wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  if (display->viewporter() != nullptr) {
    viewport = wp_viewporter_get_viewport(display->viewporter(), surface);
  }
  // Input goes to the content surface below.
  wl_region* region = wl_compositor_create_region(display->compositor());
  wl_surface_set_input_region(surface, region);
  wl_region_destroy(region);
}

WaylandWindow::Overlay::~Overlay() {
  if (syncobj_surface != nullptr) {
    wp_linux_drm_syncobj_surface_v1_destroy(syncobj_surface);
  }
  if (viewport != nullptr) {
    wp_viewport_destroy(viewport);
  }
  wl_subsurface_destroy(subsurface);
  wl_surface_destroy(surface);
}

double WaylandWindow::ContentCornerRadius(int width, int height) {
  std::lock_guard<std::mutex> lock(surface_mutex_);
  int radius = content_corner_radius_;
  if (pending_decoration_.has_value() &&
      pending_decoration_->content_width == width &&
      pending_decoration_->content_height == height) {
    radius = pending_decoration_->content_corner_radius;
  }
  return radius * scale_;
}

size_t WaylandWindow::frames_in_flight() {
  std::lock_guard<std::mutex> lock(frame_mutex_);
  return pending_frames_.size();
}

void WaylandWindow::FrameDone(void* data,
                              wl_callback* callback,
                              uint32_t time) {
  auto* self = static_cast<WaylandWindow*>(data);
  {
    std::lock_guard<std::mutex> lock(self->frame_mutex_);
    self->pending_frames_.erase(callback);
    wl_callback_destroy(callback);
  }
  self->delegate_->OnWindowFrameDone();
}

void WaylandWindow::RemoveFeedback(struct wp_presentation_feedback* feedback) {
  std::lock_guard<std::mutex> lock(feedback_mutex_);
  pending_feedback_.erase(feedback);
  wp_presentation_feedback_destroy(feedback);
}

void WaylandWindow::FeedbackSyncOutput(
    void* data,
    struct wp_presentation_feedback* feedback,
    wl_output* output) {}

void WaylandWindow::FeedbackPresented(void* data,
                                      struct wp_presentation_feedback* feedback,
                                      uint32_t tv_sec_hi,
                                      uint32_t tv_sec_lo,
                                      uint32_t tv_nsec,
                                      uint32_t refresh,
                                      uint32_t seq_hi,
                                      uint32_t seq_lo,
                                      uint32_t flags) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->RemoveFeedback(feedback);
  uint64_t seconds = (static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo;
  self->delegate_->OnWindowPresented(
      self->display_->PresentationTimeToMonotonic(seconds, tv_nsec), refresh);
}

void WaylandWindow::FeedbackDiscarded(
    void* data,
    struct wp_presentation_feedback* feedback) {
  static_cast<WaylandWindow*>(data)->RemoveFeedback(feedback);
}

bool WaylandWindow::has_pointer_focus() const {
  WaylandSeat* seat = display_->seat();
  return seat != nullptr && seat->pointer_focus() == surface_;
}

bool WaylandWindow::has_keyboard_focus() const {
  WaylandSeat* seat = display_->seat();
  return seat != nullptr && seat->keyboard_focus() == surface_;
}

void WaylandWindow::SetInputHandlers(InputHandler* input_handler,
                                     KeyboardHandler* keyboard_handler,
                                     InputMethodHandler* input_method_handler) {
  WaylandSeat* seat = display_->seat();
  if (seat == nullptr) {
    return;
  }
  seat->SetInputHandler(surface_, input_handler);
  seat->SetKeyboardHandler(surface_, keyboard_handler);
  seat->SetInputMethodHandler(surface_, input_method_handler);
}

uint32_t WaylandWindow::display_id() const {
  for (uint32_t id : entered_outputs_) {
    WaylandOutput* output = display_->GetOutput(id);
    if (output != nullptr && output->ready()) {
      return id;
    }
  }
  for (const auto& [id, output] : display_->outputs()) {
    if (output->ready()) {
      return id;
    }
  }
  return 0;
}

void WaylandWindow::OnOutputsChanged() {
  // Forget outputs that have been removed.
  entered_outputs_.erase(
      std::remove_if(
          entered_outputs_.begin(), entered_outputs_.end(),
          [this](uint32_t id) { return display_->GetOutput(id) == nullptr; }),
      entered_outputs_.end());
  UpdateOutputScale();
  uint32_t id = display_id();
  if (id != last_display_id_) {
    last_display_id_ = id;
    delegate_->OnWindowDisplayChanged();
  }
}

void WaylandWindow::SetScale(double scale) {
  if (scale == scale_) {
    return;
  }
  scale_ = scale;
  if (configured_) {
    UpdateDecoration();
    delegate_->OnWindowConfigure();
  }
}

void WaylandWindow::UpdateOutputScale() {
  // Scales other than 1 are drawn by having the viewport scale the buffer
  // to the logical size.
  if (fractional_scale_ != nullptr || viewport_ == nullptr) {
    return;
  }
  if (preferred_buffer_scale_ > 0) {
    SetScale(preferred_buffer_scale_);
    return;
  }
  // Draw at the highest scale of the outputs the window is on, so it is
  // sharp on all of them.
  int32_t scale = 0;
  for (uint32_t id : entered_outputs_) {
    WaylandOutput* output = display_->GetOutput(id);
    if (output != nullptr && output->ready()) {
      scale = std::max(scale, output->scale());
    }
  }
  if (scale > 0) {
    SetScale(scale);
  }
}

void WaylandWindow::SurfaceEnter(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {
  auto* self = static_cast<WaylandWindow*>(data);
  WaylandOutput* wayland_output = self->display_->FindOutput(output);
  if (wayland_output == nullptr) {
    return;
  }
  uint32_t id = wayland_output->id();
  if (std::find(self->entered_outputs_.begin(), self->entered_outputs_.end(),
                id) == self->entered_outputs_.end()) {
    self->entered_outputs_.push_back(id);
  }
  self->OnOutputsChanged();
}

void WaylandWindow::SurfaceLeave(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {
  auto* self = static_cast<WaylandWindow*>(data);
  WaylandOutput* wayland_output = self->display_->FindOutput(output);
  if (wayland_output == nullptr) {
    return;
  }
  uint32_t id = wayland_output->id();
  self->entered_outputs_.erase(std::remove(self->entered_outputs_.begin(),
                                           self->entered_outputs_.end(), id),
                               self->entered_outputs_.end());
  self->OnOutputsChanged();
}

void WaylandWindow::SurfacePreferredBufferScale(void* data,
                                                wl_surface* surface,
                                                int32_t factor) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->preferred_buffer_scale_ = factor;
  self->UpdateOutputScale();
}

void WaylandWindow::SurfacePreferredBufferTransform(void* data,
                                                    wl_surface* surface,
                                                    uint32_t transform) {
  // The buffers are always flipped, which compositors handle efficiently.
}

void WaylandWindow::XdgSurfaceConfigure(void* data,
                                        xdg_surface* xdg_surface,
                                        uint32_t serial) {
  auto* self = static_cast<WaylandWindow*>(data);
  if (self->popup_ != nullptr) {
    bool first_configure;
    {
      std::lock_guard<std::mutex> lock(self->surface_mutex_);
      xdg_surface_ack_configure(xdg_surface, serial);
      first_configure = !self->configured_;
      self->configured_ = true;
      self->acked_reposition_token_ = self->received_reposition_token_;
      if (self->awaited_reposition_token_.has_value()) {
        // |Present| commits this configure with the buffer of the new size.
        self->reposition_condition_.notify_all();
      } else if (!first_configure && self->presented_width_ > 0) {
        // Commit for the popup to move, as there may not be another frame.
        wl_surface_commit(self->surface_);
        wl_display_flush(self->display_->display());
      }
    }

    bool moved = self->pending_popup_x_ != self->popup_geometry_x_ ||
                 self->pending_popup_y_ != self->popup_geometry_y_;
    self->popup_geometry_x_ = self->pending_popup_x_;
    self->popup_geometry_y_ = self->pending_popup_y_;
    int width =
        self->pending_width_ > 0 ? self->pending_width_ : self->width_.load();
    int height = self->pending_height_ > 0 ? self->pending_height_
                                           : self->height_.load();
    bool resized = width != self->width_ || height != self->height_;
    self->width_ = width;
    self->height_ = height;
    if (resized || first_configure) {
      self->delegate_->OnWindowConfigure();
    }
    if (moved && !first_configure) {
      self->delegate_->OnWindowStateChanged();
    }
    return;
  }

  xdg_surface_ack_configure(xdg_surface, serial);
  bool first_configure = !self->configured_;
  self->configured_ = true;

  bool decorations_changed = false;
  if (self->toplevel_decoration_ != nullptr) {
    bool had_decorations = self->decoration_ != nullptr;
    self->SetClientSideDecorations(
        self->pending_decoration_mode_ !=
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    decorations_changed = had_decorations != (self->decoration_ != nullptr);
  }

  bool state_changed = self->maximized_ != self->pending_maximized_ ||
                       self->fullscreen_ != self->pending_fullscreen_ ||
                       self->activated_ != self->pending_activated_ ||
                       self->suspended_ != self->pending_suspended_;
  bool fullscreen_changed = self->fullscreen_ != self->pending_fullscreen_;
  self->maximized_ = self->pending_maximized_;
  self->fullscreen_ = self->pending_fullscreen_;
  self->tiled_ = self->pending_tiled_;
  self->activated_ = self->pending_activated_;
  self->suspended_ = self->pending_suspended_;
  bool floating = !self->maximized_ && !self->fullscreen_ && !self->tiled_;

  // The configured size is the window geometry, which includes the title
  // bar. A size of zero means the client chooses.
  int width = self->pending_width_;
  int height = self->pending_height_;
  if (height > 0) {
    height = std::max(height - self->title_bar_height(), 1);
  }
  if (width <= 0) {
    width = floating ? self->floating_width_ : self->width_.load();
  }
  if (height <= 0) {
    height = floating ? self->floating_height_ : self->height_.load();
  }
  if (floating) {
    // Floating windows may be smaller than suggested, so keep to the limits.
    self->ClampSize(&width, &height);
    self->floating_width_ = width;
    self->floating_height_ = height;
  }

  bool changed = width != self->width_ || height != self->height_;
  self->width_ = width;
  self->height_ = height;

  if (fullscreen_changed || decorations_changed) {
    // The title bar is hidden when fullscreen or drawn by the compositor,
    // changing the geometry limits.
    self->UpdateSizeLimits();
  }
  self->UpdateDecoration();
  if (changed || first_configure) {
    self->delegate_->OnWindowConfigure();
  }
  if (state_changed && !first_configure) {
    self->delegate_->OnWindowStateChanged();
  }
}

void WaylandWindow::ToplevelConfigure(void* data,
                                      xdg_toplevel* toplevel,
                                      int32_t width,
                                      int32_t height,
                                      wl_array* states) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->pending_width_ = width;
  self->pending_height_ = height;
  self->pending_maximized_ = false;
  self->pending_fullscreen_ = false;
  self->pending_tiled_ = false;
  self->pending_activated_ = false;
  self->pending_suspended_ = false;
  ForEachUint32(states, [self](uint32_t state) {
    switch (state) {
      case XDG_TOPLEVEL_STATE_MAXIMIZED:
        self->pending_maximized_ = true;
        break;
      case XDG_TOPLEVEL_STATE_FULLSCREEN:
        self->pending_fullscreen_ = true;
        break;
      case XDG_TOPLEVEL_STATE_ACTIVATED:
        self->pending_activated_ = true;
        break;
      case XDG_TOPLEVEL_STATE_SUSPENDED:
        self->pending_suspended_ = true;
        break;
      case XDG_TOPLEVEL_STATE_TILED_LEFT:
      case XDG_TOPLEVEL_STATE_TILED_RIGHT:
      case XDG_TOPLEVEL_STATE_TILED_TOP:
      case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
        self->pending_tiled_ = true;
        break;
    }
  });
}

void WaylandWindow::ToplevelClose(void* data, xdg_toplevel* toplevel) {
  static_cast<WaylandWindow*>(data)->delegate_->OnWindowClose();
}

void WaylandWindow::ToplevelConfigureBounds(void* data,
                                            xdg_toplevel* toplevel,
                                            int32_t width,
                                            int32_t height) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->bounds_width_ = width;
  self->bounds_height_ = height;
}

void WaylandWindow::PopupConfigure(void* data,
                                   xdg_popup* popup,
                                   int32_t x,
                                   int32_t y,
                                   int32_t width,
                                   int32_t height) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->pending_popup_x_ = x;
  self->pending_popup_y_ = y;
  self->pending_width_ = width;
  self->pending_height_ = height;
}

void WaylandWindow::PopupDone(void* data, xdg_popup* popup) {
  static_cast<WaylandWindow*>(data)->delegate_->OnWindowClose();
}

void WaylandWindow::PopupRepositioned(void* data,
                                      xdg_popup* popup,
                                      uint32_t token) {
  auto* self = static_cast<WaylandWindow*>(data);
  std::lock_guard<std::mutex> lock(self->surface_mutex_);
  self->received_reposition_token_ = token;
}

void WaylandWindow::ToplevelDecorationConfigure(
    void* data,
    zxdg_toplevel_decoration_v1* decoration,
    uint32_t mode) {
  static_cast<WaylandWindow*>(data)->pending_decoration_mode_ = mode;
}

void WaylandWindow::ToplevelWmCapabilities(void* data,
                                           xdg_toplevel* toplevel,
                                           wl_array* capabilities) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->can_minimize_ = false;
  self->can_maximize_ = false;
  ForEachUint32(capabilities, [self](uint32_t capability) {
    if (capability == XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE) {
      self->can_minimize_ = true;
    } else if (capability == XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE) {
      self->can_maximize_ = true;
    }
  });
  self->UpdateDecoration();
}

void WaylandWindow::PreferredScale(void* data,
                                   wp_fractional_scale_v1* fractional_scale,
                                   uint32_t scale) {
  static_cast<WaylandWindow*>(data)->SetScale(scale /
                                              kFractionalScaleDenominator);
}

}  // namespace flutter
