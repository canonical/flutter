// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/x11/x11_window.h"

#include <drm_fourcc.h>
#include <gbm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/shape.h>
#include <xcb/shm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/popup_positioner.h"
#include "flutter/shell/platform/linux/texture_blitter.h"
#include "flutter/shell/platform/linux/texture_buffer.h"
#include "flutter/shell/platform/linux/x11/x11_display.h"

namespace flutter {

namespace {

// The size of a top-level window if none is requested.
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;

// The number of buffers frames are copied into. One may be waiting to be
// shown while another is being drawn.
constexpr size_t kMaxBuffers = 3;

// How long to wait for the server to release a buffer before dropping a
// frame.
constexpr auto kBufferTimeout = std::chrono::milliseconds(500);

// WM_NORMAL_HINTS flags.
constexpr uint32_t kPMinSize = 1 << 4;
constexpr uint32_t kPMaxSize = 1 << 5;

// The IconicState of WM_STATE, used to minimize windows.
constexpr uint32_t kIconicState = 3;

// _NET_WM_STATE actions.
constexpr uint32_t kNetWmStateRemove = 0;
constexpr uint32_t kNetWmStateAdd = 1;

// Converts a logical size to physical pixels.
int ToPhysical(int size, double scale) {
  return std::max(static_cast<int>(std::lround(size * scale)), 1);
}

}  // namespace

struct X11Window::Buffer {
  int width = 0;
  int height = 0;
  xcb_pixmap_t pixmap = XCB_NONE;

  // True if the server isn't reading the buffer.
  bool idle = true;
  // True if the buffer is being written to or copied from.
  bool in_use = false;
  // The serial the buffer was last presented with.
  uint32_t serial = 0;

  // Buffers shared with DRI3 are rendered to through a framebuffer.
  GpuDevice* gpu = nullptr;
  gbm_bo* bo = nullptr;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint texture = 0;
  GLuint framebuffer = 0;

  // Buffers in shared memory are read into from a framebuffer.
  xcb_shm_seg_t segment = XCB_NONE;
  void* data = MAP_FAILED;
  size_t size = 0;

  ~Buffer() {
    if (framebuffer != 0) {
      glDeleteFramebuffers(1, &framebuffer);
    }
    if (texture != 0) {
      glDeleteTextures(1, &texture);
    }
    if (image != EGL_NO_IMAGE_KHR) {
      gpu->DestroyImage(gpu->display(), image);
    }
    if (bo != nullptr) {
      gbm_bo_destroy(bo);
    }
    if (data != MAP_FAILED) {
      munmap(data, size);
    }
  }
};

std::unique_ptr<X11Window> X11Window::Create(X11Display* display,
                                             Window::Delegate* delegate,
                                             const Params& params) {
  std::unique_ptr<X11Window> window(new X11Window(display, delegate));
  if (!window->Initialize(params)) {
    return nullptr;
  }
  return window;
}

X11Window::X11Window(X11Display* display, Window::Delegate* delegate)
    : display_(display), delegate_(delegate) {}

X11Window::~X11Window() {
  *alive_ = false;
  xcb_connection_t* connection = display_->connection();

  // Detach from the parent first, so the display doesn't pick this window
  // again when it updates the keyboard focus.
  mapped_ = false;
  if (parent_ != nullptr) {
    auto& siblings = parent_->popups_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), this),
                   siblings.end());
  }
  if (window_ != XCB_NONE) {
    display_->RemoveWindow(this);
  }
  for (X11Window* popup : popups_) {
    popup->parent_ = nullptr;
  }

  std::vector<std::unique_ptr<Buffer>> buffers;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffers = std::move(buffers_);
    for (auto& buffer : stale_buffers_) {
      buffers.push_back(std::move(buffer));
    }
    stale_buffers_.clear();
  }
  for (auto& buffer : buffers) {
    ReleaseBuffer(buffer.get());
  }
  if (gc_ != XCB_NONE) {
    xcb_free_gc(connection, gc_);
  }
  if (window_ != XCB_NONE) {
    xcb_destroy_window(connection, window_);
  }
  display_->Flush();

  // GPU resources must be freed with the render context current.
  auto resources =
      std::make_shared<std::tuple<std::vector<std::unique_ptr<Buffer>>,
                                  std::unique_ptr<TextureBlitter>,
                                  std::unique_ptr<TextureBuffer>>>(
          std::move(buffers), std::move(blitter_), std::move(staging_));
  delegate_->RunWithRenderContext([resources]() mutable { resources.reset(); });
}

bool X11Window::Initialize(const Params& params) {
  xcb_connection_t* connection = display_->connection();
  xcb_screen_t* screen = display_->screen();

  role_ = params.role;
  parent_ = static_cast<X11Window*>(params.parent);
  modal_ = params.modal;
  resizable_ = params.resizable;
  decorated_ = params.decorated;
  accepts_input_ = params.accepts_input;
  title_ = params.title;
  limits_ = params.size_limits;
  positioner_ = params.positioner;

  if (role_ == Role::kPopup && parent_ == nullptr) {
    FML_LOG(ERROR) << "Popups require a parent window";
    return false;
  }

  width_ = params.width;
  height_ = params.height;
  if (role_ == Role::kToplevel) {
    if (width_ <= 0 || height_ <= 0) {
      width_ = kDefaultWidth;
      height_ = kDefaultHeight;
    }
    ClampSize(&width_, &height_);
  } else {
    width_ = std::max(width_, 1);
    height_ = std::max(height_, 1);
  }
  double scale = display_->scale();
  physical_width_ = ToPhysical(width_, scale);
  physical_height_ = ToPhysical(height_, scale);

  window_ = xcb_generate_id(connection);
  uint32_t event_mask =
      XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
      XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
      XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
      XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
      XCB_EVENT_MASK_LEAVE_WINDOW;
  // Values in the order of their mask bits.
  uint32_t values[] = {
      XCB_BACK_PIXMAP_NONE,
      0,
      XCB_GRAVITY_NORTH_WEST,
      role_ == Role::kPopup ? 1u : 0u,
      event_mask,
      display_->colormap(),
  };
  xcb_create_window(connection, display_->depth(), window_, screen->root, 0, 0,
                    physical_width_, physical_height_, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, display_->visual(),
                    XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL |
                        XCB_CW_BIT_GRAVITY | XCB_CW_OVERRIDE_REDIRECT |
                        XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                    values);
  display_->AddWindow(this);

  gc_ = xcb_generate_id(connection);
  xcb_create_gc(connection, gc_, window_, 0, nullptr);

  present_event_id_ = xcb_generate_id(connection);
  xcb_present_select_input(connection, present_event_id_, window_,
                           XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                               XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

  if (!accepts_input_ && display_->has_shape()) {
    xcb_shape_rectangles(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
                         XCB_CLIP_ORDERING_UNSORTED, window_, 0, 0, 0, nullptr);
  }

  SetWmHints(params);

  if (role_ == Role::kPopup) {
    parent_->popups_.push_back(this);
    PositionPopup();
  } else {
    // Center new windows on the monitor the pointer is on.
    xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(
        connection, xcb_query_pointer(connection, screen->root), nullptr);
    int pointer_x = pointer != nullptr ? pointer->root_x : 0;
    int pointer_y = pointer != nullptr ? pointer->root_y : 0;
    free(pointer);
    int area_x, area_y, area_width, area_height;
    display_->GetWorkArea(pointer_x, pointer_y, 0, 0, &area_x, &area_y,
                          &area_width, &area_height);
    Move(area_x + std::max((area_width - physical_width_) / 2, 0),
         area_y + std::max((area_height - physical_height_) / 2, 0));
  }
  UpdateDisplayId();

  xcb_map_window(connection, window_);
  display_->Flush();
  return true;
}

void X11Window::SetWmHints(const Params& params) {
  xcb_connection_t* connection = display_->connection();
  auto atom = [this](X11Display::Atom a) { return display_->atom(a); };

  pid_t pid = getpid();
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      atom(X11Display::Atom::kNetWmPid), XCB_ATOM_CARDINAL, 32,
                      1, &pid);

  if (role_ == Role::kPopup) {
    xcb_atom_t type = atom(X11Display::Atom::kNetWmWindowTypePopupMenu);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                        atom(X11Display::Atom::kNetWmWindowType), XCB_ATOM_ATOM,
                        32, 1, &type);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                        XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
                        &parent_->window_);
    return;
  }

  xcb_atom_t protocols[] = {atom(X11Display::Atom::kWmDeleteWindow),
                            atom(X11Display::Atom::kNetWmPing)};
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      atom(X11Display::Atom::kWmProtocols), XCB_ATOM_ATOM, 32,
                      std::size(protocols), protocols);

  // WM_HINTS: the window accepts input focus.
  uint32_t wm_hints[9] = {1, 1};
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32,
                      std::size(wm_hints), wm_hints);

  xcb_atom_t type = modal_ || parent_ != nullptr
                        ? atom(X11Display::Atom::kNetWmWindowTypeDialog)
                        : atom(X11Display::Atom::kNetWmWindowTypeNormal);
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      atom(X11Display::Atom::kNetWmWindowType), XCB_ATOM_ATOM,
                      32, 1, &type);

  if (!decorated_) {
    // _MOTIF_WM_HINTS with MWM_HINTS_DECORATIONS and no decorations.
    uint32_t motif_hints[5] = {1 << 1, 0, 0, 0, 0};
    xcb_atom_t motif = atom(X11Display::Atom::kMotifWmHints);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_, motif,
                        motif, 32, std::size(motif_hints), motif_hints);
  }

  if (modal_) {
    initial_states_.push_back(atom(X11Display::Atom::kNetWmStateModal));
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                        atom(X11Display::Atom::kNetWmState), XCB_ATOM_ATOM, 32,
                        initial_states_.size(), initial_states_.data());
  }

  if (parent_ != nullptr) {
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                        XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
                        &parent_->window_);
  }

  SetTitle(params.title);
  SetApplicationId(params.application_id);
  UpdateSizeHints();
}

void X11Window::UpdateSizeHints() {
  if (role_ != Role::kToplevel) {
    return;
  }
  double scale = display_->scale();
  uint32_t hints[18] = {};
  hints[0] = kPMinSize;
  if (resizable_) {
    hints[5] = ToPhysical(limits_.min_width, scale);
    hints[6] = ToPhysical(limits_.min_height, scale);
    if (limits_.max_width > 0 || limits_.max_height > 0) {
      hints[0] |= kPMaxSize;
      hints[7] = limits_.max_width > 0
                     ? std::max(ToPhysical(limits_.max_width, scale),
                                static_cast<int>(hints[5]))
                     : INT16_MAX;
      hints[8] = limits_.max_height > 0
                     ? std::max(ToPhysical(limits_.max_height, scale),
                                static_cast<int>(hints[6]))
                     : INT16_MAX;
    }
  } else {
    hints[0] |= kPMaxSize;
    hints[5] = hints[7] = physical_width_;
    hints[6] = hints[8] = physical_height_;
  }
  xcb_change_property(display_->connection(), XCB_PROP_MODE_REPLACE, window_,
                      XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32,
                      std::size(hints), hints);
}

void X11Window::ChangeNetWmState(bool add,
                                 xcb_atom_t state1,
                                 xcb_atom_t state2) {
  xcb_connection_t* connection = display_->connection();
  if (!mapped_) {
    // Before the window is shown, the client sets the state directly.
    for (xcb_atom_t state : {state1, state2}) {
      if (state == XCB_NONE) {
        continue;
      }
      auto it =
          std::find(initial_states_.begin(), initial_states_.end(), state);
      if (add && it == initial_states_.end()) {
        initial_states_.push_back(state);
      } else if (!add && it != initial_states_.end()) {
        initial_states_.erase(it);
      }
    }
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                        display_->atom(X11Display::Atom::kNetWmState),
                        XCB_ATOM_ATOM, 32, initial_states_.size(),
                        initial_states_.data());
    display_->Flush();
    return;
  }

  xcb_client_message_event_t event = {};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window_;
  event.type = display_->atom(X11Display::Atom::kNetWmState);
  event.data.data32[0] = add ? kNetWmStateAdd : kNetWmStateRemove;
  event.data.data32[1] = state1;
  event.data.data32[2] = state2;
  event.data.data32[3] = 1;
  xcb_send_event(
      connection, 0, display_->screen()->root,
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      reinterpret_cast<const char*>(&event));
  display_->Flush();
}

void X11Window::UpdateNetWmState() {
  xcb_connection_t* connection = display_->connection();
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, window_,
                       display_->atom(X11Display::Atom::kNetWmState),
                       XCB_ATOM_ATOM, 0, 1024),
      nullptr);
  bool maximized_horz = false;
  bool maximized_vert = false;
  bool fullscreen = false;
  bool hidden = false;
  if (reply != nullptr) {
    auto* states = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    int n_states = xcb_get_property_value_length(reply) / sizeof(xcb_atom_t);
    for (int i = 0; i < n_states; i++) {
      xcb_atom_t state = states[i];
      if (state == display_->atom(X11Display::Atom::kNetWmStateMaximizedHorz)) {
        maximized_horz = true;
      } else if (state ==
                 display_->atom(X11Display::Atom::kNetWmStateMaximizedVert)) {
        maximized_vert = true;
      } else if (state ==
                 display_->atom(X11Display::Atom::kNetWmStateFullscreen)) {
        fullscreen = true;
      } else if (state == display_->atom(X11Display::Atom::kNetWmStateHidden)) {
        hidden = true;
      }
    }
    free(reply);
  }

  bool maximized = maximized_horz && maximized_vert;
  bool suspended = !mapped_ || hidden;
  if (maximized == maximized_ && fullscreen == fullscreen_ &&
      hidden == hidden_ && suspended == suspended_) {
    return;
  }
  maximized_ = maximized;
  fullscreen_ = fullscreen;
  hidden_ = hidden;
  suspended_ = suspended;
  delegate_->OnWindowStateChanged();
}

void X11Window::QueryRootPosition(int* x, int* y) const {
  xcb_connection_t* connection = display_->connection();
  xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(
      connection,
      xcb_translate_coordinates(connection, window_, display_->screen()->root,
                                0, 0),
      nullptr);
  if (reply == nullptr) {
    *x = root_x_;
    *y = root_y_;
    return;
  }
  *x = reply->dst_x;
  *y = reply->dst_y;
  free(reply);
}

void X11Window::PositionPopup() {
  if (role_ != Role::kPopup || parent_ == nullptr) {
    return;
  }
  double scale = display_->scale();
  int parent_x, parent_y;
  parent_->QueryRootPosition(&parent_x, &parent_y);

  // Keep the popup in the work area of the parent's monitor, in logical
  // pixels relative to the parent.
  int area_x, area_y, area_width, area_height;
  display_->GetWorkArea(parent_x, parent_y, parent_->physical_width_,
                        parent_->physical_height_, &area_x, &area_y,
                        &area_width, &area_height);
  Rect bounds;
  bounds.x = static_cast<int>(std::floor((area_x - parent_x) / scale));
  bounds.y = static_cast<int>(std::floor((area_y - parent_y) / scale));
  bounds.width = static_cast<int>(area_width / scale);
  bounds.height = static_cast<int>(area_height / scale);

  int x, y;
  PlacePopup(positioner_, width_, height_, bounds, &x, &y);
  bool moved = x != popup_x_ || y != popup_y_;
  popup_x_ = x;
  popup_y_ = y;
  Move(parent_x + static_cast<int>(std::lround(x * scale)),
       parent_y + static_cast<int>(std::lround(y * scale)));
  if (moved && mapped_) {
    delegate_->OnWindowStateChanged();
  }
}

void X11Window::Move(int x, int y) {
  root_x_ = x;
  root_y_ = y;
  uint32_t values[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
  xcb_configure_window(display_->connection(), window_,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
  if (role_ == Role::kToplevel) {
    // Tell the window manager the position was chosen deliberately.
    uint32_t hints[18] = {};
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        display_->connection(),
        xcb_get_property(display_->connection(), 0, window_,
                         XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0,
                         std::size(hints)),
        nullptr);
    if (reply != nullptr) {
      memcpy(
          hints, xcb_get_property_value(reply),
          std::min(sizeof(hints),
                   static_cast<size_t>(xcb_get_property_value_length(reply))));
      free(reply);
    }
    // PPosition.
    hints[0] |= 1 << 2;
    hints[1] = x;
    hints[2] = y;
    xcb_change_property(display_->connection(), XCB_PROP_MODE_REPLACE, window_,
                        XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32,
                        std::size(hints), hints);
  }
}

void X11Window::ResizeWindow() {
  double scale = display_->scale();
  physical_width_ = ToPhysical(width_, scale);
  physical_height_ = ToPhysical(height_, scale);
  uint32_t values[] = {static_cast<uint32_t>(physical_width_),
                       static_cast<uint32_t>(physical_height_)};
  xcb_configure_window(display_->connection(), window_,
                       XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                       values);
  if (!resizable_) {
    UpdateSizeHints();
  }
  if (role_ == Role::kPopup) {
    PositionPopup();
  }
  display_->Flush();
}

void X11Window::ClampSize(int* width, int* height) const {
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

void X11Window::UpdateDisplayId() {
  const X11Display::Monitor* monitor =
      display_->MonitorAt(root_x_, root_y_, physical_width_, physical_height_);
  uint32_t id = monitor != nullptr ? monitor->output.id : 0;
  if (id == display_id_) {
    return;
  }
  bool notify = display_id_ != 0;
  display_id_ = id;
  if (notify) {
    delegate_->OnWindowDisplayChanged();
  }
}

X11Window* X11Window::top_popup() const {
  for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
    if ((*it)->mapped_ && !(*it)->dismissed_) {
      return *it;
    }
  }
  return nullptr;
}

void X11Window::Dismiss() {
  if (dismissed_) {
    return;
  }
  dismissed_ = true;
  delegate_->OnWindowClose();
}

void X11Window::HandleEvent(xcb_generic_event_t* event) {
  switch (event->response_type & 0x7f) {
    case XCB_CONFIGURE_NOTIFY:
      HandleConfigure(reinterpret_cast<xcb_configure_notify_event_t*>(event));
      break;
    case XCB_MAP_NOTIFY:
      mapped_ = true;
      UpdateNetWmState();
      display_->OnWindowMapped(this);
      break;
    case XCB_UNMAP_NOTIFY:
      mapped_ = false;
      UpdateNetWmState();
      display_->OnWindowMapped(this);
      break;
    case XCB_EXPOSE:
      if (reinterpret_cast<xcb_expose_event_t*>(event)->count == 0) {
        Redraw();
      }
      break;
    case XCB_PROPERTY_NOTIFY: {
      auto* property = reinterpret_cast<xcb_property_notify_event_t*>(event);
      if (property->atom == display_->atom(X11Display::Atom::kNetWmState)) {
        UpdateNetWmState();
      }
      break;
    }
    case XCB_CLIENT_MESSAGE:
      HandleClientMessage(reinterpret_cast<xcb_client_message_event_t*>(event));
      break;
    case XCB_GE_GENERIC:
      HandlePresentEvent(event);
      break;
    default:
      break;
  }
}

void X11Window::HandleConfigure(xcb_configure_notify_event_t* event) {
  // Synthetic events from the window manager and events for override-redirect
  // windows have positions relative to the root window. Otherwise the
  // position is relative to the window manager's frame.
  if ((event->response_type & 0x80) != 0 || role_ == Role::kPopup) {
    root_x_ = event->x;
    root_y_ = event->y;
  } else {
    int x, y;
    QueryRootPosition(&x, &y);
    root_x_ = x;
    root_y_ = y;
  }
  UpdateDisplayId();

  if (event->width == physical_width_ && event->height == physical_height_) {
    return;
  }
  double scale = display_->scale();
  physical_width_ = event->width;
  physical_height_ = event->height;
  width_ = std::max(static_cast<int>(std::lround(event->width / scale)), 1);
  height_ = std::max(static_cast<int>(std::lround(event->height / scale)), 1);
  delegate_->OnWindowConfigure();
}

void X11Window::HandleClientMessage(xcb_client_message_event_t* event) {
  if (event->type != display_->atom(X11Display::Atom::kWmProtocols)) {
    return;
  }
  xcb_atom_t protocol = event->data.data32[0];
  if (protocol == display_->atom(X11Display::Atom::kWmDeleteWindow)) {
    delegate_->OnWindowClose();
  } else if (protocol == display_->atom(X11Display::Atom::kNetWmPing)) {
    xcb_client_message_event_t reply = *event;
    reply.response_type = XCB_CLIENT_MESSAGE;
    reply.window = display_->screen()->root;
    xcb_send_event(display_->connection(), 0, display_->screen()->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   reinterpret_cast<const char*>(&reply));
    display_->Flush();
  }
}

void X11Window::HandlePresentEvent(xcb_generic_event_t* event) {
  auto* generic = reinterpret_cast<xcb_ge_generic_event_t*>(event);
  if (generic->event_type == XCB_PRESENT_EVENT_IDLE_NOTIFY) {
    auto* idle = reinterpret_cast<xcb_present_idle_notify_event_t*>(event);
    {
      std::lock_guard<std::mutex> lock(buffer_mutex_);
      for (auto& buffer : buffers_) {
        if (buffer->pixmap == idle->pixmap && buffer->serial == idle->serial) {
          buffer->idle = true;
        }
      }
    }
    buffer_condition_.notify_all();
  } else if (generic->event_type == XCB_PRESENT_EVENT_COMPLETE_NOTIFY) {
    auto* complete =
        reinterpret_cast<xcb_present_complete_notify_event_t*>(event);
    if (complete->kind != XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
      return;
    }
    frames_completed_++;
    if (complete->mode != XCB_PRESENT_COMPLETE_MODE_SKIP &&
        complete->ust != 0) {
      uint64_t refresh_ns = 0;
      const X11Display::Monitor* monitor = display_->MonitorAt(
          root_x_, root_y_, physical_width_, physical_height_);
      if (monitor != nullptr && monitor->output.refresh_rate > 0) {
        refresh_ns = static_cast<uint64_t>(1e9 / monitor->output.refresh_rate);
      }
      delegate_->OnWindowPresented(complete->ust * 1000, refresh_ns);
    }
    delegate_->OnWindowFrameDone();
  }
}

void X11Window::OnScaleChanged() {
  // Keep the logical size, so the content is the same size on screen.
  if (!maximized_ && !fullscreen_) {
    ResizeWindow();
  } else {
    double scale = display_->scale();
    width_ =
        std::max(static_cast<int>(std::lround(physical_width_ / scale)), 1);
    height_ =
        std::max(static_cast<int>(std::lround(physical_height_ / scale)), 1);
  }
  UpdateSizeHints();
  delegate_->OnWindowConfigure();
}

void X11Window::OnMonitorsChanged() {
  UpdateDisplayId();
  delegate_->OnWindowDisplayChanged();
}

void X11Window::SetFocused(bool focused) {
  if (focused == activated_) {
    return;
  }
  activated_ = focused;
  delegate_->OnWindowStateChanged();
}

double X11Window::scale() const {
  return display_->scale();
}

uint32_t X11Window::display_id() const {
  return display_id_;
}

Window* X11Window::parent() const {
  return parent_;
}

int X11Window::max_suggested_width() const {
  int area_x, area_y, area_width, area_height;
  display_->GetWorkArea(root_x_, root_y_, physical_width_, physical_height_,
                        &area_x, &area_y, &area_width, &area_height);
  return static_cast<int>(area_width / display_->scale());
}

int X11Window::max_suggested_height() const {
  int area_x, area_y, area_width, area_height;
  display_->GetWorkArea(root_x_, root_y_, physical_width_, physical_height_,
                        &area_x, &area_y, &area_width, &area_height);
  return static_cast<int>(area_height / display_->scale());
}

bool X11Window::has_pointer_focus() const {
  return display_->pointer_focus() == this;
}

bool X11Window::has_keyboard_focus() const {
  return display_->keyboard_focus() == this;
}

void X11Window::SetInputHandlers(InputHandler* input_handler,
                                 KeyboardHandler* keyboard_handler,
                                 InputMethodHandler* input_method_handler) {
  // Input methods aren't supported on X11; text is typed from key events.
  input_handler_ = input_handler;
  keyboard_handler_ = keyboard_handler;
  display_->OnInputHandlersChanged(this);
}

void X11Window::SetTitle(const std::string& title) {
  title_ = title;
  xcb_connection_t* connection = display_->connection();
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      display_->atom(X11Display::Atom::kNetWmName),
                      display_->atom(X11Display::Atom::kUtf8String), 8,
                      title.size(), title.data());
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_,
                      XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, title.size(),
                      title.data());
  display_->Flush();
}

void X11Window::SetApplicationId(const std::string& application_id) {
  if (application_id.empty()) {
    return;
  }
  // WM_CLASS is the instance and class names, each null-terminated.
  std::string wm_class = application_id + '\0' + application_id + '\0';
  xcb_change_property(display_->connection(), XCB_PROP_MODE_REPLACE, window_,
                      XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, wm_class.size(),
                      wm_class.data());
  display_->Flush();
}

void X11Window::SetSize(int width, int height) {
  if (role_ != Role::kToplevel) {
    return;
  }
  ClampSize(&width, &height);
  if (maximized_ || fullscreen_ || (width == width_ && height == height_)) {
    return;
  }
  width_ = width;
  height_ = height;
  ResizeWindow();
  delegate_->OnWindowConfigure();
}

void X11Window::ResizeToContent(int width, int height) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;
  ResizeWindow();
  delegate_->OnWindowStateChanged();
}

void X11Window::SetSizeLimits(const SizeLimits& limits) {
  if (role_ != Role::kToplevel) {
    return;
  }
  limits_ = limits;
  UpdateSizeHints();
  int width = width_;
  int height = height_;
  ClampSize(&width, &height);
  if (width != width_ || height != height_) {
    SetSize(width, height);
  }
  display_->Flush();
}

void X11Window::SetMaximized(bool maximized) {
  if (role_ != Role::kToplevel) {
    return;
  }
  ChangeNetWmState(maximized,
                   display_->atom(X11Display::Atom::kNetWmStateMaximizedHorz),
                   display_->atom(X11Display::Atom::kNetWmStateMaximizedVert));
}

void X11Window::SetMinimized() {
  if (role_ != Role::kToplevel) {
    return;
  }
  xcb_client_message_event_t event = {};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window_;
  event.type = display_->atom(X11Display::Atom::kWmChangeState);
  event.data.data32[0] = kIconicState;
  xcb_send_event(
      display_->connection(), 0, display_->screen()->root,
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      reinterpret_cast<const char*>(&event));
  display_->Flush();
}

void X11Window::SetFullscreen(bool fullscreen) {
  if (role_ != Role::kToplevel) {
    return;
  }
  ChangeNetWmState(fullscreen,
                   display_->atom(X11Display::Atom::kNetWmStateFullscreen),
                   XCB_NONE);
}

void X11Window::Activate() {
  if (role_ != Role::kToplevel) {
    return;
  }
  xcb_client_message_event_t event = {};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window_;
  event.type = display_->atom(X11Display::Atom::kNetActiveWindow);
  // Requested by an application, in response to the last input.
  event.data.data32[0] = 1;
  event.data.data32[1] = display_->last_input_time();
  xcb_send_event(
      display_->connection(), 0, display_->screen()->root,
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      reinterpret_cast<const char*>(&event));
  display_->Flush();
}

void X11Window::SetParent(Window* parent) {
  if (role_ != Role::kToplevel) {
    return;
  }
  parent_ = static_cast<X11Window*>(parent);
  if (parent_ != nullptr) {
    xcb_change_property(display_->connection(), XCB_PROP_MODE_REPLACE, window_,
                        XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
                        &parent_->window_);
  } else {
    xcb_delete_property(display_->connection(), window_,
                        XCB_ATOM_WM_TRANSIENT_FOR);
  }
  display_->Flush();
}

void X11Window::SetPositioner(const Positioner& positioner) {
  if (role_ != Role::kPopup) {
    return;
  }
  positioner_ = positioner;
  PositionPopup();
  display_->Flush();
}

void X11Window::StopWaiting() {
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    stop_waiting_ = true;
  }
  buffer_condition_.notify_all();
}

size_t X11Window::frames_in_flight() {
  uint64_t presented = frames_presented_;
  uint64_t completed = frames_completed_;
  return presented > completed ? presented - completed : 0;
}

void X11Window::Present(RenderBuffer* buffer,
                        const SyncPoints* sync,
                        const std::vector<Layer>& layers) {
  // Platform views aren't supported on X11, so there are no layers. The
  // compositor renders into textures for this backend.
  auto* source = static_cast<TextureBuffer*>(buffer);
  int width = source->width();
  int height = source->height();

  std::vector<std::unique_ptr<Buffer>> stale;
  Buffer* target = nullptr;
  {
    std::unique_lock<std::mutex> lock(buffer_mutex_);
    target = AcquireBuffer(lock, width, height);
    stale = std::move(stale_buffers_);
    stale_buffers_.clear();
    if (target == nullptr && !stop_waiting_ && buffers_.size() < kMaxBuffers) {
      lock.unlock();
      std::unique_ptr<Buffer> created = CreateBuffer(source, width, height);
      lock.lock();
      if (created != nullptr) {
        target = created.get();
        buffers_.push_back(std::move(created));
      }
    }
    if (target != nullptr) {
      target->in_use = true;
    }
  }
  // The render context is current, so stale buffers can be destroyed.
  stale.clear();
  if (target == nullptr) {
    return;
  }

  bool copied = CopyToBuffer(source, target);

  std::lock_guard<std::mutex> lock(buffer_mutex_);
  target->in_use = false;
  if (!copied) {
    return;
  }
  PresentBuffer(target);
}

X11Window::Buffer* X11Window::AcquireBuffer(std::unique_lock<std::mutex>& lock,
                                            int width,
                                            int height) {
  auto deadline = std::chrono::steady_clock::now() + kBufferTimeout;
  while (true) {
    // Discard idle buffers of the wrong size.
    for (auto it = buffers_.begin(); it != buffers_.end();) {
      Buffer* buffer = it->get();
      if (buffer->idle && !buffer->in_use &&
          (buffer->width != width || buffer->height != height)) {
        if (last_presented_ == buffer) {
          last_presented_ = nullptr;
        }
        ReleaseBuffer(buffer);
        stale_buffers_.push_back(std::move(*it));
        it = buffers_.erase(it);
      } else {
        ++it;
      }
    }

    for (auto& buffer : buffers_) {
      if (buffer->idle && !buffer->in_use && buffer.get() != last_presented_) {
        return buffer.get();
      }
    }
    if (buffers_.size() < kMaxBuffers || stop_waiting_) {
      return nullptr;
    }
    // The most recently shown buffer is only reused if all are in use, as
    // it's needed to redraw the window.
    if (last_presented_ != nullptr && last_presented_->idle &&
        !last_presented_->in_use) {
      return last_presented_;
    }
    if (buffer_condition_.wait_until(lock, deadline) ==
        std::cv_status::timeout) {
      FML_LOG(WARNING) << "X server didn't release a buffer, dropping frame";
      return nullptr;
    }
  }
}

std::unique_ptr<X11Window::Buffer>
X11Window::CreateBuffer(TextureBuffer* source, int width, int height) {
  if (display_->has_dri3() && !dri3_failed_) {
    std::unique_ptr<Buffer> buffer = CreateDri3Buffer(source, width, height);
    if (buffer != nullptr) {
      return buffer;
    }
    dri3_failed_ = true;
    if (display_->has_shm()) {
      FML_LOG(WARNING) << "Failed to share buffers with DRI3, using shared "
                          "memory instead";
    }
  }
  if (display_->has_shm()) {
    return CreateShmBuffer(width, height);
  }
  return nullptr;
}

std::unique_ptr<X11Window::Buffer>
X11Window::CreateDri3Buffer(TextureBuffer* source, int width, int height) {
  GpuDevice* gpu = source->egl_manager()->gpu();
  if (!gpu->supports_dmabuf_import()) {
    return nullptr;
  }

  auto buffer = std::make_unique<Buffer>();
  buffer->width = width;
  buffer->height = height;
  buffer->gpu = gpu;
  buffer->bo = gbm_bo_create(gpu->gbm(), width, height, GBM_FORMAT_ARGB8888,
                             GBM_BO_USE_RENDERING);
  if (buffer->bo == nullptr || gbm_bo_get_plane_count(buffer->bo) != 1) {
    return nullptr;
  }
  fml::UniqueFD fd(gbm_bo_get_fd(buffer->bo));
  if (!fd.is_valid()) {
    return nullptr;
  }
  uint32_t stride = gbm_bo_get_stride(buffer->bo);
  GpuDevice::DmabufPlane plane = {fd.get(), 0, stride};
  buffer->image = gpu->ImportDmabuf(width, height, DRM_FORMAT_ARGB8888,
                                    DRM_FORMAT_MOD_INVALID, &plane, 1);
  if (buffer->image == EGL_NO_IMAGE_KHR) {
    return nullptr;
  }

  GLint saved_texture = 0;
  GLint saved_framebuffer = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer);
  glGenTextures(1, &buffer->texture);
  glBindTexture(GL_TEXTURE_2D, buffer->texture);
  gpu->ImageTargetTexture2D(GL_TEXTURE_2D, buffer->image);
  glGenFramebuffers(1, &buffer->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         buffer->texture, 0);
  bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindTexture(GL_TEXTURE_2D, saved_texture);
  glBindFramebuffer(GL_FRAMEBUFFER, saved_framebuffer);
  if (!complete) {
    return nullptr;
  }

  // The connection takes ownership of the file descriptor.
  xcb_connection_t* connection = display_->connection();
  buffer->pixmap = xcb_generate_id(connection);
  xcb_void_cookie_t cookie = xcb_dri3_pixmap_from_buffer_checked(
      connection, buffer->pixmap, window_, stride * height, width, height,
      stride, display_->depth(), 32, fd.release());
  xcb_generic_error_t* error = xcb_request_check(connection, cookie);
  display_->Flush();
  if (error != nullptr) {
    free(error);
    buffer->pixmap = XCB_NONE;
    return nullptr;
  }
  return buffer;
}

std::unique_ptr<X11Window::Buffer> X11Window::CreateShmBuffer(int width,
                                                              int height) {
  auto buffer = std::make_unique<Buffer>();
  buffer->width = width;
  buffer->height = height;
  buffer->size = static_cast<size_t>(width) * height * 4;

  fml::UniqueFD fd(memfd_create("flutter-x11", MFD_CLOEXEC));
  if (!fd.is_valid() || ftruncate(fd.get(), buffer->size) != 0) {
    FML_LOG(ERROR) << "Failed to allocate shared memory";
    return nullptr;
  }
  buffer->data = mmap(nullptr, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd.get(), 0);
  if (buffer->data == MAP_FAILED) {
    FML_LOG(ERROR) << "Failed to map shared memory";
    return nullptr;
  }

  // The connection takes ownership of the file descriptor.
  xcb_connection_t* connection = display_->connection();
  buffer->segment = xcb_generate_id(connection);
  xcb_void_cookie_t attach = xcb_shm_attach_fd_checked(
      connection, buffer->segment, fd.release(), /*read_only=*/0);
  buffer->pixmap = xcb_generate_id(connection);
  xcb_void_cookie_t create = xcb_shm_create_pixmap_checked(
      connection, buffer->pixmap, window_, width, height, display_->depth(),
      buffer->segment, 0);
  xcb_generic_error_t* attach_error = xcb_request_check(connection, attach);
  xcb_generic_error_t* create_error = xcb_request_check(connection, create);
  display_->Flush();
  if (attach_error != nullptr || create_error != nullptr) {
    FML_LOG(ERROR) << "Failed to create shared memory pixmap";
    free(attach_error);
    free(create_error);
    ReleaseBuffer(buffer.get());
    return nullptr;
  }
  return buffer;
}

void X11Window::ReleaseBuffer(Buffer* buffer) {
  xcb_connection_t* connection = display_->connection();
  if (buffer->pixmap != XCB_NONE) {
    xcb_free_pixmap(connection, buffer->pixmap);
    buffer->pixmap = XCB_NONE;
  }
  if (buffer->segment != XCB_NONE) {
    xcb_shm_detach(connection, buffer->segment);
    buffer->segment = XCB_NONE;
  }
}

bool X11Window::CopyToBuffer(TextureBuffer* texture, Buffer* buffer) {
  if (blitter_ == nullptr) {
    blitter_ = std::make_unique<TextureBlitter>();
  }

  if (buffer->framebuffer != 0) {
    if (!blitter_->Draw(texture->texture(), buffer->framebuffer, buffer->width,
                        buffer->height, /*swap_red_blue=*/false)) {
      return false;
    }
    // The server reads the buffer without synchronizing with this context.
    glFinish();
    return true;
  }

  // Draw into a texture that is read into shared memory in the server's
  // byte order (BGRA).
  if (staging_ == nullptr || staging_->width() != buffer->width ||
      staging_->height() != buffer->height) {
    staging_ = TextureBuffer::Create(texture->egl_manager(), buffer->width,
                                     buffer->height, false);
    if (staging_ == nullptr) {
      return false;
    }
  }
  bool read_bgra = texture->egl_manager()->supports_read_bgra();
  if (!blitter_->Draw(texture->texture(), staging_->framebuffer(),
                      buffer->width, buffer->height,
                      /*swap_red_blue=*/!read_bgra)) {
    return false;
  }
  GLint saved_framebuffer = 0;
  GLint saved_alignment = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &saved_alignment);
  glBindFramebuffer(GL_FRAMEBUFFER, staging_->framebuffer());
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, buffer->width, buffer->height,
               read_bgra ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE,
               buffer->data);
  glPixelStorei(GL_PACK_ALIGNMENT, saved_alignment);
  glBindFramebuffer(GL_FRAMEBUFFER, saved_framebuffer);
  return glGetError() == GL_NO_ERROR;
}

void X11Window::PresentBuffer(Buffer* buffer) {
  buffer->idle = false;
  buffer->serial = ++present_serial_;
  last_presented_ = buffer;
  frames_presented_++;
  xcb_present_pixmap(display_->connection(), window_, buffer->pixmap,
                     buffer->serial, XCB_NONE, XCB_NONE, 0, 0, XCB_NONE,
                     XCB_NONE, XCB_NONE, XCB_PRESENT_OPTION_NONE, 0, 0, 0, 0,
                     nullptr);
  display_->Flush();
}

void X11Window::Redraw() {
  Buffer* buffer = nullptr;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer = last_presented_;
    if (buffer == nullptr || buffer->in_use) {
      return;
    }
    // Stop the buffer being drawn to while it's copied.
    buffer->in_use = true;
  }

  xcb_connection_t* connection = display_->connection();
  xcb_copy_area(connection, buffer->pixmap, window_, gc_, 0, 0, 0, 0,
                buffer->width, buffer->height);
  free(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection),
                                 nullptr));

  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer->in_use = false;
  }
  buffer_condition_.notify_all();
}

}  // namespace flutter
