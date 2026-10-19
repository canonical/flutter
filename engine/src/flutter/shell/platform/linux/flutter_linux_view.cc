// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_view.h"

#include <algorithm>
#include <cmath>

#include "flutter/shell/platform/linux/flutter_linux_engine.h"

namespace flutter {

namespace {

// The largest size content may grow to if it is unconstrained and the
// compositor doesn't suggest a limit, in logical pixels.
constexpr double kDefaultMaxContentSize = 4096;

size_t ToPhysical(double logical, double scale) {
  return static_cast<size_t>(std::round(logical * scale));
}

}  // namespace

std::unique_ptr<FlutterLinuxView> FlutterLinuxView::Create(
    FlutterLinuxEngine* engine,
    FlutterViewId view_id,
    const WaylandWindow::Params& params,
    Sizing sizing,
    const Constraints& constraints) {
  std::unique_ptr<FlutterLinuxView> view(
      new FlutterLinuxView(engine, view_id, sizing, constraints));
  FlutterLinuxApplication* application = engine->application();
  view->window_ = WaylandWindow::Create(application->display(),
                                        application->decoration_renderer(),
                                        view.get(), params);
  if (!view->window_) {
    return nullptr;
  }
  AtspiApplication* atspi = application->atspi();
  view->accessible_ = std::make_unique<AccessibleView>(
      view.get(), atspi, atspi->root(), atspi->NewWindowPath());
  atspi->AddWindow(view->accessible_.get());

  WaylandSeat* seat = application->display()->seat();
  view->pointer_converter_ =
      std::make_unique<PointerEventConverter>(view.get(), view_id);
  if (seat != nullptr) {
    seat->SetInputHandler(view->window_->surface(),
                          view->pointer_converter_.get());
    seat->SetKeyboardHandler(view->window_->surface(),
                             engine->keyboard_manager());
    seat->SetInputMethodHandler(view->window_->surface(),
                                engine->keyboard_manager()->text_input());
  }
  return view;
}

FlutterLinuxView::FlutterLinuxView(FlutterLinuxEngine* engine,
                                   FlutterViewId view_id,
                                   Sizing sizing,
                                   const Constraints& constraints)
    : engine_(engine),
      view_id_(view_id),
      sizing_(sizing),
      constraints_(constraints) {}

FlutterLinuxView::~FlutterLinuxView() {
  if (accessible_) {
    engine_->application()->atspi()->RemoveWindow(accessible_.get());
    accessible_.reset();
  }
  WaylandSeat* seat = engine_->application()->display()->seat();
  if (seat != nullptr && window_) {
    seat->SetInputHandler(window_->surface(), nullptr);
    seat->SetKeyboardHandler(window_->surface(), nullptr);
    seat->SetInputMethodHandler(window_->surface(), nullptr);
  }
}

FlutterWindowMetricsEvent FlutterLinuxView::GetWindowMetrics() const {
  FlutterWindowMetricsEvent event = {};
  event.struct_size = sizeof(FlutterWindowMetricsEvent);
  event.width = window_->physical_width();
  event.height = window_->physical_height();
  event.pixel_ratio = window_->scale();
  event.view_id = view_id_;

  if (sizing_ != Sizing::kWindow) {
    // Let the content choose its size within the constraints, keeping it
    // within the space the compositor suggests.
    double scale = window_->scale();
    double max_width = constraints_.max_width;
    double max_height = constraints_.max_height;
    if (!std::isfinite(max_width)) {
      int suggested = window_->max_suggested_width();
      max_width = suggested > 0 ? suggested : kDefaultMaxContentSize;
    }
    if (!std::isfinite(max_height)) {
      int suggested = window_->max_suggested_height();
      max_height = suggested > 0 ? suggested : kDefaultMaxContentSize;
    }
    event.has_constraints = true;
    event.min_width_constraint = ToPhysical(constraints_.min_width, scale);
    event.min_height_constraint = ToPhysical(constraints_.min_height, scale);
    event.max_width_constraint =
        std::max(ToPhysical(max_width, scale), event.min_width_constraint);
    event.max_height_constraint =
        std::max(ToPhysical(max_height, scale), event.min_height_constraint);
    event.width = std::clamp(event.width, event.min_width_constraint,
                             event.max_width_constraint);
    event.height = std::clamp(event.height, event.min_height_constraint,
                              event.max_height_constraint);
  }
  return event;
}

void FlutterLinuxView::SetConstraints(const Constraints& constraints) {
  constraints_ = constraints;
  if (sizing_ != Sizing::kWindow) {
    SendWindowMetrics();
  }
}

void FlutterLinuxView::SendWindowMetrics() {
  engine_->SendWindowMetrics(GetWindowMetrics());
}

void FlutterLinuxView::PresentBuffer(RenderBuffer* buffer,
                                     const WaylandWindow::SyncPoints* sync) {
  int width = buffer->width();
  int height = buffer->height();
  window_->Present(buffer->buffer(), width, height, sync);

  if (sizing_ != Sizing::kWindow &&
      (width != presented_width_ || height != presented_height_)) {
    presented_width_ = width;
    presented_height_ = height;
    double scale = window_->scale();
    engine_->application()->event_loop()->PostTask(
        [this, alive = std::weak_ptr<bool>(alive_), width, height, scale]() {
          if (!alive.expired()) {
            OnContentResized(width, height, scale);
          }
        });
  }
}

void FlutterLinuxView::OnContentResized(int width, int height, double scale) {
  if (sizing_ == Sizing::kWindow) {
    return;
  }
  window_->ResizeToContent(static_cast<int>(std::round(width / scale)),
                           static_cast<int>(std::round(height / scale)));
  if (sizing_ == Sizing::kInitialContent) {
    // From now on the user sizes the window, and the content fills it.
    sizing_ = Sizing::kWindow;
    SendWindowMetrics();
  }
}

void FlutterLinuxView::OnWindowConfigure() {
  // Ignore the configure that occurs while the window is being created.
  if (!window_) {
    return;
  }
  SendWindowMetrics();
  if (state_callback_) {
    state_callback_();
  }
}

void FlutterLinuxView::OnWindowStateChanged() {
  if (accessible_) {
    accessible_->OnWindowStateChanged();
  }
  if (state_callback_) {
    state_callback_();
  }
}

void FlutterLinuxView::OnWindowTitleChanged() {
  if (accessible_) {
    accessible_->OnWindowStateChanged();
  }
}

void FlutterLinuxView::OnWindowClose() {
  // Handle the request outside of Wayland event dispatch so the callback can
  // destroy the view.
  engine_->application()->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_)]() {
        if (alive.expired()) {
          return;
        }
        if (close_callback_) {
          // Copy the callback as it may destroy the view.
          std::function<void()> callback = close_callback_;
          callback();
          return;
        }
        engine_->OnViewClose(this);
      });
}

void FlutterLinuxView::OnWindowPresented(uint64_t presentation_time_ns,
                                         uint64_t refresh_ns) {
  engine_->OnFramePresented(presentation_time_ns, refresh_ns);
}

void FlutterLinuxView::OnWindowFrameDone() {
  engine_->OnFrameDone();
}

double FlutterLinuxView::GetPointerScale() {
  return window_->scale();
}

void FlutterLinuxView::SendPointerEvents(const FlutterPointerEvent* events,
                                         size_t count) {
  engine_->SendPointerEvents(events, count);
}

void FlutterLinuxView::OnPointerEntered() {
  WaylandSeat* seat = engine_->application()->display()->seat();
  if (seat != nullptr) {
    seat->SetCursor(engine_->cursor());
  }
}

std::string FlutterLinuxView::GetAccessibleTitle() {
  return window_ ? window_->title() : "";
}

AtspiRole FlutterLinuxView::GetAccessibleRole() {
  if (!window_) {
    return AtspiRole::kFrame;
  }
  if (window_->role() == WaylandWindow::Role::kPopup) {
    return AtspiRole::kWindow;
  }
  return window_->parent() != nullptr ? AtspiRole::kDialog : AtspiRole::kFrame;
}

AtspiStateSet FlutterLinuxView::GetWindowStates() {
  AtspiStateSet states;
  if (!window_) {
    return states;
  }
  states.Set(AtspiState::kActive, window_->activated());
  states.Set(AtspiState::kModal, window_->modal());
  states.Set(AtspiState::kResizable,
             window_->role() == WaylandWindow::Role::kToplevel &&
                 window_->resizable());
  return states;
}

std::vector<AtspiRelation> FlutterLinuxView::GetWindowRelations() {
  if (!window_) {
    return {};
  }
  FlutterLinuxView* parent = engine_->FindViewForWindow(window_->parent());
  if (parent == nullptr || parent->accessible() == nullptr) {
    return {};
  }
  AtspiRelationType type = window_->role() == WaylandWindow::Role::kPopup
                               ? AtspiRelationType::kPopupFor
                               : AtspiRelationType::kSubwindowOf;
  return {{type, {parent->accessible()}}};
}

AtspiRect FlutterLinuxView::GetWindowExtents() {
  if (!window_) {
    return {};
  }
  return {.width = window_->width(), .height = window_->height()};
}

double FlutterLinuxView::GetPixelRatio() {
  return window_ ? window_->scale() : 1.0;
}

void FlutterLinuxView::DispatchSemanticsAction(uint64_t node_id,
                                               FlutterSemanticsAction action,
                                               std::vector<uint8_t> data) {
  engine_->SendSemanticsAction(view_id_, node_id, action, std::move(data));
}

}  // namespace flutter
