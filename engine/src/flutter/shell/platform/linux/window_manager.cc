// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/window_manager.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/flutter_linux_engine.h"

namespace flutter {

namespace {

// The window anchor for a WindowPositionerAnchor, which are in the same
// order.
::flutter::Window::Anchor ToAnchor(int32_t anchor) {
  using Anchor = ::flutter::Window::Anchor;
  if (anchor < static_cast<int32_t>(Anchor::kCenter) ||
      anchor > static_cast<int32_t>(Anchor::kBottomRight)) {
    return Anchor::kCenter;
  }
  return static_cast<Anchor>(anchor);
}

// The direction a popup extends from the anchor point, so that |anchor| of
// the popup is at that point.
::flutter::Window::Anchor ToGravity(int32_t anchor) {
  using Anchor = ::flutter::Window::Anchor;
  switch (ToAnchor(anchor)) {
    case Anchor::kCenter:
      return Anchor::kCenter;
    case Anchor::kTop:
      return Anchor::kBottom;
    case Anchor::kBottom:
      return Anchor::kTop;
    case Anchor::kLeft:
      return Anchor::kRight;
    case Anchor::kRight:
      return Anchor::kLeft;
    case Anchor::kTopLeft:
      return Anchor::kBottomRight;
    case Anchor::kBottomLeft:
      return Anchor::kTopRight;
    case Anchor::kTopRight:
      return Anchor::kBottomLeft;
    case Anchor::kBottomRight:
      return Anchor::kTopLeft;
  }
  return Anchor::kCenter;
}

::flutter::Window::Positioner ToPositioner(
    const WindowPositionerRequest& request) {
  ::flutter::Window::Positioner positioner;
  positioner.anchor_rect = {
      static_cast<int>(std::round(request.anchor_x)),
      static_cast<int>(std::round(request.anchor_y)),
      static_cast<int>(std::round(request.anchor_width)),
      static_cast<int>(std::round(request.anchor_height)),
  };
  positioner.anchor = ToAnchor(request.parent_anchor);
  positioner.gravity = ToGravity(request.child_anchor);
  using Adjustment = ::flutter::Window::ConstraintAdjustment;
  positioner.constraint_adjustment =
      request.constraint_adjustment &
      (Adjustment::kSlideX | Adjustment::kSlideY | Adjustment::kFlipX |
       Adjustment::kFlipY | Adjustment::kResizeX | Adjustment::kResizeY);
  positioner.offset_x = static_cast<int>(std::round(request.offset_x));
  positioner.offset_y = static_cast<int>(std::round(request.offset_y));
  return positioner;
}

FlutterLinuxView::Constraints ToViewConstraints(
    const WindowConstraints& constraints) {
  FlutterLinuxView::Constraints result;
  if (constraints.has_constraints) {
    result.min_width = std::max(constraints.min_width, 0.0);
    result.min_height = std::max(constraints.min_height, 0.0);
    result.max_width = std::max(constraints.max_width, result.min_width);
    result.max_height = std::max(constraints.max_height, result.min_height);
  }
  return result;
}

// Converts a logical limit to the window's, where zero is unlimited.
int ToMaxLimit(double value) {
  if (!std::isfinite(value) || value >= std::numeric_limits<int>::max()) {
    return 0;
  }
  return std::max(static_cast<int>(std::floor(value)), 1);
}

::flutter::Window::SizeLimits ToSizeLimits(
    const WindowConstraints& constraints) {
  ::flutter::Window::SizeLimits limits;
  if (constraints.has_constraints) {
    limits.min_width =
        static_cast<int>(std::ceil(std::max(constraints.min_width, 0.0)));
    limits.min_height =
        static_cast<int>(std::ceil(std::max(constraints.min_height, 0.0)));
    limits.max_width = ToMaxLimit(constraints.max_width);
    limits.max_height = ToMaxLimit(constraints.max_height);
  }
  return limits;
}

bool IsPopup(WindowArchetype archetype) {
  return archetype == WindowArchetype::kPopup ||
         archetype == WindowArchetype::kTooltip;
}

}  // namespace

WindowManager::WindowManager(FlutterLinuxEngine* engine) : engine_(engine) {}

WindowManager::~WindowManager() {
  on_event_ = nullptr;
  // Destroy attached windows before the windows they are attached to.
  std::vector<FlutterViewId> view_ids;
  for (const auto& [view_id, window] : windows_) {
    if (FindWindow(window.parent_view_id) == nullptr) {
      GetDescendants(view_id, &view_ids);
      view_ids.push_back(view_id);
    }
  }
  for (FlutterViewId view_id : view_ids) {
    auto it = windows_.find(view_id);
    if (it != windows_.end()) {
      engine_->RemoveView(it->second.view.get());
      windows_.erase(it);
    }
  }
}

void WindowManager::Initialize(const WindowingInitRequest& request) {
  on_event_ = request.on_event;
}

WindowManager::Window* WindowManager::FindWindow(FlutterViewId view_id) {
  auto it = windows_.find(view_id);
  return it != windows_.end() ? &it->second : nullptr;
}

const WindowManager::Window* WindowManager::FindWindow(
    FlutterViewId view_id) const {
  auto it = windows_.find(view_id);
  return it != windows_.end() ? &it->second : nullptr;
}

FlutterLinuxView* WindowManager::GetView(FlutterViewId view_id) const {
  const Window* window = FindWindow(view_id);
  return window != nullptr ? window->view.get() : nullptr;
}

FlutterViewId WindowManager::CreateWindow(
    const WindowCreationRequest& request) {
  if (request.archetype < static_cast<int32_t>(WindowArchetype::kRegular) ||
      request.archetype > static_cast<int32_t>(WindowArchetype::kSatellite)) {
    FML_LOG(ERROR) << "Unknown window archetype " << request.archetype;
    return -1;
  }
  auto archetype = static_cast<WindowArchetype>(request.archetype);

  FlutterLinuxView* parent = nullptr;
  if (request.parent_view_id >= 0) {
    parent = GetView(request.parent_view_id);
    if (parent == nullptr) {
      FML_LOG(ERROR) << "Unknown parent window " << request.parent_view_id;
      return -1;
    }
  }
  if (parent == nullptr &&
      (IsPopup(archetype) || archetype == WindowArchetype::kSatellite)) {
    FML_LOG(ERROR) << "Popups, tooltips and satellites require a parent";
    return -1;
  }

  ::flutter::Window::Params params;
  params.parent = parent != nullptr ? parent->window() : nullptr;
  FlutterLinuxView::Constraints constraints =
      ToViewConstraints(request.constraints);
  FlutterLinuxView::Sizing sizing;
  if (IsPopup(archetype)) {
    params.role = ::flutter::Window::Role::kPopup;
    params.positioner = ToPositioner(request.positioner);
    params.accepts_input = archetype != WindowArchetype::kTooltip;
    sizing = FlutterLinuxView::Sizing::kContent;
  } else {
    params.role = ::flutter::Window::Role::kToplevel;
    params.modal = archetype == WindowArchetype::kDialog && parent != nullptr;
    params.resizable = request.resizable;
    params.size_limits = ToSizeLimits(request.constraints);
    if (request.title != nullptr) {
      params.title = request.title;
    }
    if (request.has_size) {
      sizing = FlutterLinuxView::Sizing::kWindow;
    } else if (request.resizable) {
      sizing = FlutterLinuxView::Sizing::kInitialContent;
    } else {
      sizing = FlutterLinuxView::Sizing::kContent;
    }
  }
  if (sizing == FlutterLinuxView::Sizing::kWindow) {
    params.width = static_cast<int>(std::round(request.width));
    params.height = static_cast<int>(std::round(request.height));
  } else {
    // Start at the smallest size until the content has been drawn.
    params.width = static_cast<int>(std::ceil(constraints.min_width));
    params.height = static_cast<int>(std::ceil(constraints.min_height));
  }

  std::unique_ptr<FlutterLinuxView> view =
      engine_->CreateView(params, sizing, constraints);
  if (!view) {
    return -1;
  }
  FlutterViewId view_id = view->view_id();
  view->SetCloseCallback([this, view_id]() { OnCloseRequested(view_id); });
  view->SetStateCallback(
      [this, view_id]() { SendEvent(view_id, WindowEvent::kChanged); });
  windows_[view_id] = {
      .view = std::move(view),
      .archetype = archetype,
      .parent_view_id = parent != nullptr ? request.parent_view_id : -1,
      .resizable = request.resizable,
  };
  return view_id;
}

void WindowManager::GetDescendants(FlutterViewId view_id,
                                   std::vector<FlutterViewId>* result) const {
  for (const auto& [child_id, window] : windows_) {
    if (window.parent_view_id == view_id) {
      GetDescendants(child_id, result);
      result->push_back(child_id);
    }
  }
}

void WindowManager::DestroyWindow(FlutterViewId view_id) {
  if (FindWindow(view_id) == nullptr) {
    return;
  }

  // Popups must be destroyed before their parents, and attached windows go
  // with the window they are attached to.
  std::vector<FlutterViewId> view_ids;
  GetDescendants(view_id, &view_ids);
  view_ids.push_back(view_id);
  for (FlutterViewId id : view_ids) {
    auto it = windows_.find(id);
    if (it == windows_.end()) {
      continue;
    }
    std::unique_ptr<FlutterLinuxView> view = std::move(it->second.view);
    windows_.erase(it);
    engine_->RemoveView(view.get());
    view.reset();
    SendEvent(id, WindowEvent::kDestroyed);
  }

  // Applications using windows end when the last one is closed.
  FlutterLinuxApplication* application = engine_->application();
  if (application->view_count() == 0) {
    application->Quit();
  }
}

void WindowManager::OnCloseRequested(FlutterViewId view_id) {
  const Window* window = FindWindow(view_id);
  if (window == nullptr) {
    return;
  }
  // The compositor dismisses popups, which can't refuse.
  if (IsPopup(window->archetype)) {
    DestroyWindow(view_id);
    return;
  }
  SendEvent(view_id, WindowEvent::kCloseRequested);
}

void WindowManager::SendEvent(FlutterViewId view_id, WindowEvent event) {
  if (on_event_ != nullptr) {
    on_event_(view_id, static_cast<int32_t>(event));
  }
}

bool WindowManager::GetWindowState(FlutterViewId view_id,
                                   WindowState* state) const {
  const Window* window = FindWindow(view_id);
  if (window == nullptr) {
    return false;
  }
  ::flutter::Window* window_state = window->view->window();
  *state = {};
  state->width = window_state->width();
  state->height = window_state->height();
  state->activated = window_state->activated();
  state->maximized = window_state->maximized();
  state->fullscreen = window_state->fullscreen();
  if (window_state->role() == ::flutter::Window::Role::kPopup) {
    state->offset_x = window_state->popup_x();
    state->offset_y = window_state->popup_y();
  }
  return true;
}

void WindowManager::SetSize(FlutterViewId view_id,
                            double width,
                            double height) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetSize(static_cast<int>(std::round(width)),
                            static_cast<int>(std::round(height)));
  }
}

void WindowManager::SetConstraints(FlutterViewId view_id,
                                   const WindowConstraints& constraints) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetSizeLimits(ToSizeLimits(constraints));
    view->SetConstraints(ToViewConstraints(constraints));
  }
}

void WindowManager::SetTitle(FlutterViewId view_id, const char* title) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetTitle(title != nullptr ? title : "");
  }
}

void WindowManager::SetMaximized(FlutterViewId view_id, bool maximized) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetMaximized(maximized);
  }
}

void WindowManager::SetMinimized(FlutterViewId view_id) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetMinimized();
  }
}

void WindowManager::SetFullscreen(FlutterViewId view_id, bool fullscreen) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->SetFullscreen(fullscreen);
  }
}

void WindowManager::Activate(FlutterViewId view_id) {
  if (FlutterLinuxView* view = GetView(view_id)) {
    view->window()->Activate();
  }
}

void WindowManager::SetParent(FlutterViewId view_id,
                              FlutterViewId parent_view_id) {
  Window* window = FindWindow(view_id);
  if (window == nullptr || IsPopup(window->archetype)) {
    return;
  }
  FlutterLinuxView* parent = GetView(parent_view_id);
  if (parent_view_id >= 0 && parent == nullptr) {
    return;
  }

  // Don't allow a window to become attached to itself.
  for (FlutterViewId id = parent_view_id; id >= 0;) {
    if (id == view_id) {
      FML_LOG(ERROR) << "Can't attach a window to its own descendant";
      return;
    }
    const Window* ancestor = FindWindow(id);
    id = ancestor != nullptr ? ancestor->parent_view_id : -1;
  }

  window->parent_view_id = parent != nullptr ? parent_view_id : -1;
  window->view->window()->SetParent(parent != nullptr ? parent->window()
                                                      : nullptr);
}

void WindowManager::UpdatePosition(FlutterViewId view_id,
                                   const WindowPositionerRequest& positioner) {
  const Window* window = FindWindow(view_id);
  if (window == nullptr || !IsPopup(window->archetype)) {
    return;
  }
  window->view->window()->SetPositioner(ToPositioner(positioner));
}

}  // namespace flutter

namespace {

flutter::WindowManager* GetWindowManager(int64_t engine_id) {
  flutter::FlutterLinuxEngine* engine =
      flutter::FlutterLinuxEngine::FromEngineId(engine_id);
  if (engine == nullptr) {
    FML_LOG(ERROR) << "Unknown engine " << engine_id;
    return nullptr;
  }
  return engine->window_manager();
}

}  // namespace

void InternalFlutterLinux_WindowManager_Initialize(
    int64_t engine_id,
    const flutter::WindowingInitRequest* request) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->Initialize(*request);
  }
}

int64_t InternalFlutterLinux_WindowManager_CreateWindow(
    int64_t engine_id,
    const flutter::WindowCreationRequest* request) {
  flutter::WindowManager* manager = GetWindowManager(engine_id);
  return manager != nullptr ? manager->CreateWindow(*request) : -1;
}

void InternalFlutterLinux_WindowManager_DestroyWindow(int64_t engine_id,
                                                      int64_t view_id) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->DestroyWindow(view_id);
  }
}

bool InternalFlutterLinux_WindowManager_GetWindowState(
    int64_t engine_id,
    int64_t view_id,
    flutter::WindowState* state) {
  flutter::WindowManager* manager = GetWindowManager(engine_id);
  return manager != nullptr && manager->GetWindowState(view_id, state);
}

void InternalFlutterLinux_WindowManager_SetSize(int64_t engine_id,
                                                int64_t view_id,
                                                double width,
                                                double height) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetSize(view_id, width, height);
  }
}

void InternalFlutterLinux_WindowManager_SetConstraints(
    int64_t engine_id,
    int64_t view_id,
    const flutter::WindowConstraints* constraints) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetConstraints(view_id, *constraints);
  }
}

void InternalFlutterLinux_WindowManager_SetTitle(int64_t engine_id,
                                                 int64_t view_id,
                                                 const char* title) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetTitle(view_id, title);
  }
}

void InternalFlutterLinux_WindowManager_SetMaximized(int64_t engine_id,
                                                     int64_t view_id,
                                                     bool maximized) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetMaximized(view_id, maximized);
  }
}

void InternalFlutterLinux_WindowManager_SetMinimized(int64_t engine_id,
                                                     int64_t view_id) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetMinimized(view_id);
  }
}

void InternalFlutterLinux_WindowManager_SetFullscreen(int64_t engine_id,
                                                      int64_t view_id,
                                                      bool fullscreen) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetFullscreen(view_id, fullscreen);
  }
}

void InternalFlutterLinux_WindowManager_Activate(int64_t engine_id,
                                                 int64_t view_id) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->Activate(view_id);
  }
}

void InternalFlutterLinux_WindowManager_SetParent(int64_t engine_id,
                                                  int64_t view_id,
                                                  int64_t parent_view_id) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->SetParent(view_id, parent_view_id);
  }
}

void InternalFlutterLinux_WindowManager_UpdatePosition(
    int64_t engine_id,
    int64_t view_id,
    const flutter::WindowPositionerRequest* positioner) {
  if (flutter::WindowManager* manager = GetWindowManager(engine_id)) {
    manager->UpdatePosition(view_id, *positioner);
  }
}
