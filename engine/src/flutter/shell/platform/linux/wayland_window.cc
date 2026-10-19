// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

// The fractional scale protocol expresses scales in 120ths.
constexpr double kFractionalScaleDenominator = 120.0;

// The smallest decorated window, in logical pixels.
constexpr int kMinimumWidth = 160;
constexpr int kMinimumContentHeight = 20;

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
    Delegate* delegate,
    const Params& params) {
  std::unique_ptr<WaylandWindow> window(new WaylandWindow(display, delegate));
  if (!window->Initialize(decoration_renderer, params)) {
    return nullptr;
  }
  return window;
}

WaylandWindow::WaylandWindow(WaylandDisplay* display, Delegate* delegate)
    : display_(display), delegate_(delegate) {}

WaylandWindow::~WaylandWindow() {
  decoration_.reset();
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
  parent_ = params.parent;
  resizable_ = params.resizable;
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
    if (params.decorated && decoration_renderer != nullptr &&
        display_->subcompositor() != nullptr) {
      decoration_ = std::make_unique<WindowDecoration>(
          display_, decoration_renderer, this, surface_, toplevel_);
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
  for (void* proxy : std::initializer_list<void*>{
           surface_, fractional_scale_, xdg_surface_, toplevel_, popup_}) {
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
  xdg_positioner_set_anchor(result, positioner.anchor);
  xdg_positioner_set_gravity(result, positioner.gravity);
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

void WaylandWindow::SetParent(WaylandWindow* parent) {
  if (toplevel_ == nullptr) {
    return;
  }
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

void WaylandWindow::ApplyDecorationFrame(const DecorationFrame& frame) {
  decoration_->Apply(frame);
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
  return pending_decoration_.has_value() &&
         pending_decoration_->buffer == buffer;
}

void WaylandWindow::SetApplicationId(const std::string& application_id) {
  if (toplevel_ == nullptr) {
    return;
  }
  xdg_toplevel_set_app_id(toplevel_, application_id.c_str());
}

void WaylandWindow::Present(wl_buffer* buffer,
                            int width,
                            int height,
                            const SyncPoints* sync) {
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
        syncobj_surface_, sync->timeline, sync->acquire_point >> 32,
        sync->acquire_point & 0xffffffff);
    wp_linux_drm_syncobj_surface_v1_set_release_point(
        syncobj_surface_, sync->timeline, sync->release_point >> 32,
        sync->release_point & 0xffffffff);
  }
  wl_surface_attach(surface_, buffer, 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, width, height);
  if (viewport_ != nullptr) {
    double scale = scale_;
    wp_viewport_set_destination(viewport_,
                                static_cast<int>(std::round(width / scale)),
                                static_cast<int>(std::round(height / scale)));
  }
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

void WaylandWindow::SurfaceEnter(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {}

void WaylandWindow::SurfaceLeave(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {}

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

  bool state_changed = self->maximized_ != self->pending_maximized_ ||
                       self->fullscreen_ != self->pending_fullscreen_ ||
                       self->activated_ != self->pending_activated_;
  bool fullscreen_changed = self->fullscreen_ != self->pending_fullscreen_;
  self->maximized_ = self->pending_maximized_;
  self->fullscreen_ = self->pending_fullscreen_;
  self->tiled_ = self->pending_tiled_;
  self->activated_ = self->pending_activated_;
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

  if (fullscreen_changed) {
    // The title bar is hidden when fullscreen, changing the geometry limits.
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
  auto* self = static_cast<WaylandWindow*>(data);
  double new_scale = scale / kFractionalScaleDenominator;
  if (new_scale == self->scale_) {
    return;
  }
  self->scale_ = new_scale;
  if (self->configured_) {
    self->UpdateDecoration();
    self->delegate_->OnWindowConfigure();
  }
}

}  // namespace flutter
