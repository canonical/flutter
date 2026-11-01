// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/x11/x11_display.h"

#include <dlfcn.h>
#include <linux/input-event-codes.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/shm.h>
#include <xcb/xcbext.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/x11/x11_clipboard.h"
#include "flutter/shell/platform/linux/x11/x11_window.h"

namespace flutter {

namespace {

constexpr const char* kAtomNames[] = {
    "CLIPBOARD",
    "INCR",
    "_MOTIF_WM_HINTS",
    "_NET_ACTIVE_WINDOW",
    "_NET_WM_NAME",
    "_NET_WM_PID",
    "_NET_WM_PING",
    "_NET_WM_STATE",
    "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_HIDDEN",
    "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_MAXIMIZED_VERT",
    "_NET_WM_STATE_MODAL",
    "_NET_WM_WINDOW_TYPE",
    "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_NORMAL",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    "_NET_WORKAREA",
    "FLUTTER_SELECTION",
    "TARGETS",
    "UTF8_STRING",
    "WM_CHANGE_STATE",
    "WM_DELETE_WINDOW",
    "WM_PROTOCOLS",
    "_XKB_RULES_NAMES",
};
static_assert(std::size(kAtomNames) ==
              static_cast<size_t>(X11Display::Atom::kCount));

// Modifier bits in event state.
constexpr uint16_t kLockMask = XCB_MOD_MASK_LOCK;
constexpr uint16_t kNumLockMask = XCB_MOD_MASK_2;
constexpr uint16_t kModifierMask = 0xff;

// Set in the same_screen_focus field of crossing events if the window has
// input focus (XCB_ENTER_NOTIFY_FLAG_FOCUS, missing from older libxcb).
constexpr uint8_t kCrossingFocusFlag = 0x01;

// The distance in logical pixels one step of a scroll wheel scrolls, matching
// what Wayland compositors send.
constexpr double kScrollStep = 10.0;

// The XKEYBOARD extension, used to enable reporting the keyboard group in
// event state. libxcb-xkb isn't required, as this is the only request used.
xcb_extension_t xkb_extension = {"XKEYBOARD", 0};

// libxkbcommon-x11, loaded at runtime if available to read the keymap
// directly from the server.
struct XkbCommonX11 {
  void* handle = nullptr;
  int (*setup_xkb_extension)(xcb_connection_t*,
                             uint16_t,
                             uint16_t,
                             int,
                             uint16_t*,
                             uint16_t*,
                             uint8_t*,
                             uint8_t*) = nullptr;
  int32_t (*get_core_keyboard_device_id)(xcb_connection_t*) = nullptr;
  xkb_keymap* (*keymap_new_from_device)(xkb_context*,
                                        xcb_connection_t*,
                                        int32_t,
                                        int) = nullptr;
};

const XkbCommonX11& LoadXkbCommonX11() {
  static XkbCommonX11 library = []() {
    XkbCommonX11 result;
    result.handle = dlopen("libxkbcommon-x11.so.0", RTLD_NOW | RTLD_LOCAL);
    if (result.handle == nullptr) {
      return result;
    }
    result.setup_xkb_extension =
        reinterpret_cast<decltype(result.setup_xkb_extension)>(
            dlsym(result.handle, "xkb_x11_setup_xkb_extension"));
    result.get_core_keyboard_device_id =
        reinterpret_cast<decltype(result.get_core_keyboard_device_id)>(
            dlsym(result.handle, "xkb_x11_get_core_keyboard_device_id"));
    result.keymap_new_from_device =
        reinterpret_cast<decltype(result.keymap_new_from_device)>(
            dlsym(result.handle, "xkb_x11_keymap_new_from_device"));
    if (result.setup_xkb_extension == nullptr ||
        result.get_core_keyboard_device_id == nullptr ||
        result.keymap_new_from_device == nullptr) {
      result.setup_xkb_extension = nullptr;
    }
    return result;
  }();
  return library;
}

// Reads the server's keymap with libxkbcommon-x11, returning it in the XKB
// text format, or an empty string if unavailable.
std::string ReadServerKeymap(xcb_connection_t* connection) {
  const XkbCommonX11& library = LoadXkbCommonX11();
  if (library.setup_xkb_extension == nullptr) {
    return "";
  }
  if (!library.setup_xkb_extension(connection, 1, 0, 0, nullptr, nullptr,
                                   nullptr, nullptr)) {
    return "";
  }
  int32_t device = library.get_core_keyboard_device_id(connection);
  if (device < 0) {
    return "";
  }

  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (context == nullptr) {
    return "";
  }
  std::string result;
  xkb_keymap* keymap =
      library.keymap_new_from_device(context, connection, device, /*flags=*/0);
  if (keymap != nullptr) {
    char* text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (text != nullptr) {
      result = text;
      free(text);
    }
    xkb_keymap_unref(keymap);
  }
  xkb_context_unref(context);
  return result;
}

uint64_t MonotonicMillis() {
  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return static_cast<uint64_t>(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}

// Returns the Linux input event code for an X button, or zero for scroll
// wheel buttons and unknown buttons.
uint32_t ButtonCode(uint8_t button) {
  switch (button) {
    case 1:
      return BTN_LEFT;
    case 2:
      return BTN_MIDDLE;
    case 3:
      return BTN_RIGHT;
    case 8:
      return BTN_SIDE;
    case 9:
      return BTN_EXTRA;
    default:
      return 0;
  }
}

// Returns the X cursor font glyph that best matches a
// wp_cursor_shape_device_v1 shape. See X11/cursorfont.h.
uint16_t CursorGlyph(uint32_t shape) {
  switch (shape) {
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP:
      return 92;  // XC_question_arrow
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER:
      return 60;  // XC_hand2
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT:
      return 150;  // XC_watch
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL:
      return 90;  // XC_plus
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR:
      return 34;  // XC_crosshair
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT:
      return 152;  // XC_xterm
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY:
      return 40;  // XC_dotbox
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING:
      return 52;  // XC_fleur
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB:
      return 58;  // XC_hand1
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED:
      return 24;  // XC_circle
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
      return 96;  // XC_right_side
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
      return 138;  // XC_top_side
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE:
      return 136;  // XC_top_right_corner
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE:
      return 134;  // XC_top_left_corner
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
      return 16;  // XC_bottom_side
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE:
      return 14;  // XC_bottom_right_corner
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE:
      return 12;  // XC_bottom_left_corner
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
      return 70;  // XC_left_side
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE:
      return 108;  // XC_sb_h_double_arrow
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE:
      return 116;  // XC_sb_v_double_arrow
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT:
      return 142;  // XC_target
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU:
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT:
    default:
      return 68;  // XC_left_ptr
  }
}

// Parses the Xft.dpi resource from the RESOURCE_MANAGER string.
std::optional<double> ParseXftDpi(const std::string& resources) {
  size_t start = 0;
  while (start < resources.size()) {
    size_t end = resources.find('\n', start);
    if (end == std::string::npos) {
      end = resources.size();
    }
    std::string line = resources.substr(start, end - start);
    start = end + 1;
    constexpr std::string_view kPrefix = "Xft.dpi:";
    if (line.compare(0, kPrefix.size(), kPrefix) == 0) {
      double dpi = std::strtod(line.c_str() + kPrefix.size(), nullptr);
      if (dpi > 0) {
        return dpi;
      }
    }
  }
  return std::nullopt;
}

// Returns the window an event is for, or XCB_NONE.
xcb_window_t EventWindow(xcb_generic_event_t* event) {
  switch (event->response_type & 0x7f) {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
    case XCB_MOTION_NOTIFY:
      return reinterpret_cast<xcb_key_press_event_t*>(event)->event;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
      return reinterpret_cast<xcb_enter_notify_event_t*>(event)->event;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
      return reinterpret_cast<xcb_focus_in_event_t*>(event)->event;
    case XCB_EXPOSE:
      return reinterpret_cast<xcb_expose_event_t*>(event)->window;
    case XCB_CONFIGURE_NOTIFY:
      return reinterpret_cast<xcb_configure_notify_event_t*>(event)->window;
    case XCB_MAP_NOTIFY:
      return reinterpret_cast<xcb_map_notify_event_t*>(event)->window;
    case XCB_UNMAP_NOTIFY:
      return reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window;
    case XCB_PROPERTY_NOTIFY:
      return reinterpret_cast<xcb_property_notify_event_t*>(event)->window;
    case XCB_CLIENT_MESSAGE:
      return reinterpret_cast<xcb_client_message_event_t*>(event)->window;
    case XCB_SELECTION_CLEAR:
      return reinterpret_cast<xcb_selection_clear_event_t*>(event)->owner;
    case XCB_SELECTION_REQUEST:
      return reinterpret_cast<xcb_selection_request_event_t*>(event)->owner;
    case XCB_SELECTION_NOTIFY:
      return reinterpret_cast<xcb_selection_notify_event_t*>(event)->requestor;
    default:
      return XCB_NONE;
  }
}

}  // namespace

std::unique_ptr<X11Display> X11Display::Create(EventLoop* event_loop) {
  int screen = 0;
  xcb_connection_t* connection = xcb_connect(nullptr, &screen);
  if (xcb_connection_has_error(connection)) {
    xcb_disconnect(connection);
    return nullptr;
  }
  std::unique_ptr<X11Display> display(
      new X11Display(event_loop, connection, screen));
  if (!display->Initialize()) {
    return nullptr;
  }
  return display;
}

X11Display::X11Display(EventLoop* event_loop,
                       xcb_connection_t* connection,
                       int screen)
    : event_loop_(event_loop),
      connection_(connection),
      screen_number_(screen) {}

X11Display::~X11Display() {
  *alive_ = false;
  clipboard_.reset();
  for (auto& [shape, cursor] : cursors_) {
    xcb_free_cursor(connection_, cursor);
  }
  if (cursor_font_ != XCB_NONE) {
    xcb_close_font(connection_, cursor_font_);
  }
  if (colormap_ != XCB_NONE) {
    xcb_free_colormap(connection_, colormap_);
  }
  xcb_disconnect(connection_);
}

bool X11Display::Initialize() {
  const xcb_setup_t* setup = xcb_get_setup(connection_);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_number_ && screens.rem > 0; i++) {
    xcb_screen_next(&screens);
  }
  if (screens.rem == 0) {
    FML_LOG(ERROR) << "X server has no screen " << screen_number_;
    return false;
  }
  screen_ = screens.data;

  if (!InternAtoms()) {
    return false;
  }
  QueryExtensions();
  if (present_opcode_ == 0) {
    FML_LOG(ERROR) << "X server doesn't support the Present extension";
    return false;
  }
  if (!has_dri3_ && !has_shm_) {
    FML_LOG(ERROR) << "X server supports neither DRI3 nor MIT-SHM with file "
                      "descriptor passing";
    return false;
  }
  EnableXkb();
  ChooseVisual();

  // Focus events on the root window report changes to and from PointerRoot
  // focus.
  uint32_t root_events =
      XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;
  xcb_change_window_attributes(connection_, screen_->root, XCB_CW_EVENT_MASK,
                               &root_events);
  xcb_get_input_focus_reply_t* focus_reply = xcb_get_input_focus_reply(
      connection_, xcb_get_input_focus(connection_), nullptr);
  if (focus_reply != nullptr) {
    focus_follows_pointer_ = focus_reply->focus == XCB_INPUT_FOCUS_POINTER_ROOT;
    free(focus_reply);
  }
  if (has_randr_) {
    xcb_randr_select_input(connection_, screen_->root,
                           XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                               XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                               XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
  }

  UpdateScale();
  UpdateWorkArea();
  UpdateMonitors();
  UpdateKeymap();

  clipboard_ = X11Clipboard::Create(this);
  xcb_flush(connection_);
  return CheckConnection();
}

bool X11Display::InternAtoms() {
  xcb_intern_atom_cookie_t cookies[std::size(kAtomNames)];
  for (size_t i = 0; i < std::size(kAtomNames); i++) {
    cookies[i] =
        xcb_intern_atom(connection_, 0, strlen(kAtomNames[i]), kAtomNames[i]);
  }
  bool ok = true;
  for (size_t i = 0; i < std::size(kAtomNames); i++) {
    xcb_intern_atom_reply_t* reply =
        xcb_intern_atom_reply(connection_, cookies[i], nullptr);
    if (reply == nullptr) {
      ok = false;
      continue;
    }
    atoms_[i] = reply->atom;
    free(reply);
  }
  if (!ok) {
    FML_LOG(ERROR) << "Failed to intern X atoms";
  }
  return ok;
}

void X11Display::QueryExtensions() {
  for (xcb_extension_t* extension :
       {&xcb_present_id, &xcb_dri3_id, &xcb_shm_id, &xcb_randr_id,
        &xcb_shape_id, &xkb_extension}) {
    xcb_prefetch_extension_data(connection_, extension);
  }

  const xcb_query_extension_reply_t* present =
      xcb_get_extension_data(connection_, &xcb_present_id);
  if (present != nullptr && present->present) {
    xcb_present_query_version_reply_t* reply = xcb_present_query_version_reply(
        connection_,
        xcb_present_query_version(connection_, XCB_PRESENT_MAJOR_VERSION,
                                  XCB_PRESENT_MINOR_VERSION),
        nullptr);
    if (reply != nullptr) {
      present_opcode_ = present->major_opcode;
      free(reply);
    }
  }

  const xcb_query_extension_reply_t* dri3 =
      xcb_get_extension_data(connection_, &xcb_dri3_id);
  if (dri3 != nullptr && dri3->present) {
    xcb_dri3_query_version_reply_t* version = xcb_dri3_query_version_reply(
        connection_, xcb_dri3_query_version(connection_, 1, 0), nullptr);
    xcb_dri3_open_reply_t* open = xcb_dri3_open_reply(
        connection_, xcb_dri3_open(connection_, screen_->root, XCB_NONE),
        nullptr);
    if (version != nullptr && open != nullptr && open->nfd == 1) {
      fml::UniqueFD fd(xcb_dri3_open_reply_fds(connection_, open)[0]);
      struct stat info;
      if (fstat(fd.get(), &info) == 0 && S_ISCHR(info.st_mode)) {
        main_device_ = info.st_rdev;
        has_dri3_ = true;
      }
    }
    free(version);
    free(open);
  }

  const xcb_query_extension_reply_t* shm =
      xcb_get_extension_data(connection_, &xcb_shm_id);
  if (shm != nullptr && shm->present) {
    xcb_shm_query_version_reply_t* reply = xcb_shm_query_version_reply(
        connection_, xcb_shm_query_version(connection_), nullptr);
    // Version 1.2 adds passing shared memory as file descriptors.
    if (reply != nullptr && reply->shared_pixmaps &&
        (reply->major_version > 1 ||
         (reply->major_version == 1 && reply->minor_version >= 2))) {
      has_shm_ = true;
    }
    free(reply);
  }

  const xcb_query_extension_reply_t* randr =
      xcb_get_extension_data(connection_, &xcb_randr_id);
  if (randr != nullptr && randr->present) {
    xcb_randr_query_version_reply_t* reply = xcb_randr_query_version_reply(
        connection_, xcb_randr_query_version(connection_, 1, 5), nullptr);
    if (reply != nullptr &&
        (reply->major_version > 1 ||
         (reply->major_version == 1 && reply->minor_version >= 5))) {
      has_randr_ = true;
      randr_event_base_ = randr->first_event;
    }
    free(reply);
  }

  const xcb_query_extension_reply_t* shape =
      xcb_get_extension_data(connection_, &xcb_shape_id);
  has_shape_ = shape != nullptr && shape->present;

  const xcb_query_extension_reply_t* xkb =
      xcb_get_extension_data(connection_, &xkb_extension);
  if (xkb != nullptr && xkb->present) {
    xkb_opcode_ = xkb->major_opcode;
  }
}

void X11Display::EnableXkb() {
  if (xkb_opcode_ == 0) {
    return;
  }

  // XkbUseExtension, requesting version 1.0.
  struct {
    uint8_t major_opcode;
    uint8_t minor_opcode;
    uint16_t length;
    uint16_t wanted_major;
    uint16_t wanted_minor;
  } request = {0, 0, 0, 1, 0};
  static const xcb_protocol_request_t kRequest = {
      .count = 2, .ext = &xkb_extension, .opcode = 0, .isvoid = 0};
  iovec parts[4];
  parts[2].iov_base = &request;
  parts[2].iov_len = sizeof(request);
  parts[3].iov_base = nullptr;
  parts[3].iov_len = 0;
  unsigned int sequence =
      xcb_send_request(connection_, XCB_REQUEST_CHECKED, parts + 2, &kRequest);
  xcb_generic_error_t* error = nullptr;
  void* reply = xcb_wait_for_reply(connection_, sequence, &error);
  // The second byte of the reply is whether the extension is supported.
  if (reply == nullptr || static_cast<uint8_t*>(reply)[1] == 0) {
    xkb_opcode_ = 0;
  }
  free(reply);
  free(error);
}

void X11Display::ChooseVisual() {
  visual_ = screen_->root_visual;
  depth_ = screen_->root_depth;

  // Prefer a visual with alpha, so windows can be translucent when a
  // compositing manager is running.
  for (xcb_depth_iterator_t depths =
           xcb_screen_allowed_depths_iterator(screen_);
       depths.rem > 0; xcb_depth_next(&depths)) {
    if (depths.data->depth != 32) {
      continue;
    }
    for (xcb_visualtype_iterator_t visuals =
             xcb_depth_visuals_iterator(depths.data);
         visuals.rem > 0; xcb_visualtype_next(&visuals)) {
      xcb_visualtype_t* visual = visuals.data;
      if (visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
          visual->red_mask == 0xff0000 && visual->green_mask == 0xff00 &&
          visual->blue_mask == 0xff) {
        visual_ = visual->visual_id;
        depth_ = 32;
        break;
      }
    }
  }

  colormap_ = xcb_generate_id(connection_);
  xcb_create_colormap(connection_, XCB_COLORMAP_ALLOC_NONE, colormap_,
                      screen_->root, visual_);
}

void X11Display::UpdateMonitors() {
  std::vector<Monitor> monitors;
  if (has_randr_) {
    xcb_randr_get_screen_resources_current_reply_t* resources =
        xcb_randr_get_screen_resources_current_reply(
            connection_,
            xcb_randr_get_screen_resources_current(connection_, screen_->root),
            nullptr);
    xcb_randr_get_monitors_reply_t* reply = xcb_randr_get_monitors_reply(
        connection_, xcb_randr_get_monitors(connection_, screen_->root, 1),
        nullptr);
    if (reply != nullptr) {
      for (xcb_randr_monitor_info_iterator_t it =
               xcb_randr_get_monitors_monitors_iterator(reply);
           it.rem > 0; xcb_randr_monitor_info_next(&it)) {
        xcb_randr_monitor_info_t* info = it.data;
        Monitor monitor;
        monitor.x = info->x;
        monitor.y = info->y;
        monitor.output.width = info->width;
        monitor.output.height = info->height;
        monitor.output.pixel_ratio = scale_;
        monitor.output.id = info->name;
        int n_outputs = xcb_randr_monitor_info_outputs_length(info);
        if (n_outputs > 0) {
          xcb_randr_output_t output = xcb_randr_monitor_info_outputs(info)[0];
          monitor.output.id = output;
          xcb_randr_get_output_info_reply_t* output_info =
              xcb_randr_get_output_info_reply(
                  connection_,
                  xcb_randr_get_output_info(connection_, output,
                                            XCB_CURRENT_TIME),
                  nullptr);
          if (output_info != nullptr && output_info->crtc != XCB_NONE &&
              resources != nullptr) {
            xcb_randr_get_crtc_info_reply_t* crtc =
                xcb_randr_get_crtc_info_reply(
                    connection_,
                    xcb_randr_get_crtc_info(connection_, output_info->crtc,
                                            XCB_CURRENT_TIME),
                    nullptr);
            if (crtc != nullptr) {
              xcb_randr_mode_info_t* modes =
                  xcb_randr_get_screen_resources_current_modes(resources);
              int n_modes = xcb_randr_get_screen_resources_current_modes_length(
                  resources);
              for (int i = 0; i < n_modes; i++) {
                if (modes[i].id == crtc->mode && modes[i].htotal > 0 &&
                    modes[i].vtotal > 0) {
                  monitor.output.refresh_rate =
                      static_cast<double>(modes[i].dot_clock) /
                      (static_cast<double>(modes[i].htotal) * modes[i].vtotal);
                }
              }
              free(crtc);
            }
          }
          free(output_info);
        }
        if (info->primary) {
          monitors.insert(monitors.begin(), monitor);
        } else {
          monitors.push_back(monitor);
        }
      }
      free(reply);
    }
    free(resources);
  }
  if (monitors.empty()) {
    Monitor monitor;
    monitor.output.id = 1;
    monitor.output.width = screen_->width_in_pixels;
    monitor.output.height = screen_->height_in_pixels;
    monitor.output.pixel_ratio = scale_;
    monitors.push_back(monitor);
  }

  bool changed = monitors.size() != monitors_.size();
  for (size_t i = 0; !changed && i < monitors.size(); i++) {
    const Monitor& a = monitors[i];
    const Monitor& b = monitors_[i];
    changed = a.x != b.x || a.y != b.y || a.output.id != b.output.id ||
              a.output.width != b.output.width ||
              a.output.height != b.output.height ||
              a.output.refresh_rate != b.output.refresh_rate ||
              a.output.pixel_ratio != b.output.pixel_ratio;
  }
  if (!changed) {
    return;
  }
  monitors_ = std::move(monitors);
  NotifyOutputsChanged();
  for (auto& [id, window] : windows_) {
    window->OnMonitorsChanged();
  }
}

void X11Display::UpdateScale() {
  double scale = 1.0;
  std::optional<std::string> resources =
      GetRootProperty(XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING);
  if (resources.has_value()) {
    std::optional<double> dpi = ParseXftDpi(*resources);
    if (dpi.has_value()) {
      scale = *dpi / 96.0;
    }
  }
  scale = std::clamp(scale, 0.5, 8.0);
  if (scale == scale_) {
    return;
  }
  scale_ = scale;
  for (Monitor& monitor : monitors_) {
    monitor.output.pixel_ratio = scale_;
  }
  if (!monitors_.empty()) {
    NotifyOutputsChanged();
  }
  for (auto& [id, window] : windows_) {
    window->OnScaleChanged();
  }
}

void X11Display::UpdateWorkArea() {
  work_area_x_ = work_area_y_ = work_area_width_ = work_area_height_ = 0;
  std::optional<std::string> work_area =
      GetRootProperty(atom(Atom::kNetWorkarea), XCB_ATOM_CARDINAL);
  if (!work_area.has_value() || work_area->size() < 4 * sizeof(uint32_t)) {
    return;
  }
  uint32_t values[4];
  memcpy(values, work_area->data(), sizeof(values));
  work_area_x_ = static_cast<int32_t>(values[0]);
  work_area_y_ = static_cast<int32_t>(values[1]);
  work_area_width_ = static_cast<int32_t>(values[2]);
  work_area_height_ = static_cast<int32_t>(values[3]);
}

void X11Display::UpdateKeymap() {
  std::unique_ptr<XkbKeyboard> keyboard;
  std::string keymap = ReadServerKeymap(connection_);
  if (!keymap.empty()) {
    keyboard = XkbKeyboard::Create(keymap);
  }
  if (keyboard == nullptr) {
    // _XKB_RULES_NAMES is five null-terminated strings: rules, model,
    // layout, variant and options.
    std::optional<std::string> names =
        GetRootProperty(atom(Atom::kXkbRulesNames), XCB_ATOM_STRING);
    std::vector<std::string> parts(5);
    if (names.has_value()) {
      size_t start = 0;
      for (std::string& part : parts) {
        if (start >= names->size()) {
          break;
        }
        size_t end = names->find('\0', start);
        if (end == std::string::npos) {
          end = names->size();
        }
        part = names->substr(start, end - start);
        start = end + 1;
      }
    }
    keyboard = XkbKeyboard::CreateFromNames(parts[0], parts[1], parts[2],
                                            parts[3], parts[4]);
  }
  if (keyboard == nullptr) {
    FML_LOG(ERROR) << "Failed to load the keymap";
    return;
  }
  xkb_keyboard_ = std::move(keyboard);
  key_resolver_.Reset();
  reported_modifiers_.reset();
}

const X11Display::Monitor* X11Display::MonitorAt(int x,
                                                 int y,
                                                 int width,
                                                 int height) const {
  if (monitors_.empty()) {
    return nullptr;
  }
  int center_x = x + width / 2;
  int center_y = y + height / 2;
  for (const Monitor& monitor : monitors_) {
    if (center_x >= monitor.x && center_x < monitor.x + monitor.output.width &&
        center_y >= monitor.y && center_y < monitor.y + monitor.output.height) {
      return &monitor;
    }
  }
  return &monitors_[0];
}

void X11Display::GetWorkArea(int x,
                             int y,
                             int width,
                             int height,
                             int* area_x,
                             int* area_y,
                             int* area_width,
                             int* area_height) const {
  const Monitor* monitor = MonitorAt(x, y, width, height);
  int left = 0;
  int top = 0;
  int right = screen_->width_in_pixels;
  int bottom = screen_->height_in_pixels;
  if (monitor != nullptr) {
    left = monitor->x;
    top = monitor->y;
    right = monitor->x + monitor->output.width;
    bottom = monitor->y + monitor->output.height;
  }

  // _NET_WORKAREA covers all monitors, so only use it to exclude panels it
  // overlaps on this monitor.
  if (work_area_width_ > 0 && work_area_height_ > 0) {
    int work_right = work_area_x_ + work_area_width_;
    int work_bottom = work_area_y_ + work_area_height_;
    if (work_area_x_ < right && work_right > left && work_area_y_ < bottom &&
        work_bottom > top) {
      left = std::max(left, work_area_x_);
      top = std::max(top, work_area_y_);
      right = std::min(right, work_right);
      bottom = std::min(bottom, work_bottom);
    }
  }
  *area_x = left;
  *area_y = top;
  *area_width = std::max(right - left, 0);
  *area_height = std::max(bottom - top, 0);
}

void X11Display::AddWindow(X11Window* window) {
  windows_[window->window()] = window;
}

void X11Display::RemoveWindow(X11Window* window) {
  windows_.erase(window->window());
  if (pointer_focus_ == window) {
    pointer_focus_ = nullptr;
    pending_leave_ = false;
    pressed_buttons_ = 0;
  }
  if (explicit_focus_ == window) {
    explicit_focus_ = nullptr;
  }
  if (pointer_focus_in_ == window) {
    pointer_focus_in_ = nullptr;
  }
  if (focused_window_ == window) {
    focused_window_ = nullptr;
  }
  if (keyboard_focus_ == window) {
    keyboard_focus_ = nullptr;
  }
  UpdateKeyboardFocus();
}

void X11Display::OnInputHandlersChanged(X11Window* window) {
  if (window == pointer_focus_ && window->input_handler() != nullptr) {
    xcb_query_pointer_reply_t* reply = xcb_query_pointer_reply(
        connection_, xcb_query_pointer(connection_, window->window()), nullptr);
    if (reply != nullptr) {
      window->input_handler()->OnPointerEnter(MonotonicMillis() * 1000,
                                              reply->win_x / scale_,
                                              reply->win_y / scale_);
      free(reply);
    }
  }
  if (window == keyboard_focus_) {
    keyboard_focus_ = nullptr;
    UpdateKeyboardFocus();
  }
}

void X11Display::OnWindowMapped(X11Window* window) {
  UpdateKeyboardFocus();
}

X11Window* X11Display::FindWindow(xcb_window_t id) const {
  auto it = windows_.find(id);
  return it == windows_.end() ? nullptr : it->second;
}

int X11Display::GetFd() const {
  return xcb_get_file_descriptor(connection_);
}

void X11Display::Prepare() {
  // Events may have been read while waiting for a reply.
  DispatchQueuedEvents();
  xcb_flush(connection_);
}

void X11Display::Dispatch(bool readable) {
  while (xcb_generic_event_t* event = xcb_poll_for_event(connection_)) {
    HandleEvent(event);
    free(event);
  }
  FlushPendingRelease();
  CheckConnection();
}

void X11Display::DispatchQueuedEvents() {
  while (xcb_generic_event_t* event = xcb_poll_for_queued_event(connection_)) {
    HandleEvent(event);
    free(event);
  }
  FlushPendingRelease();
}

void X11Display::Flush() {
  xcb_flush(connection_);
  if (event_loop_->RunsTasksOnCurrentThread()) {
    CheckConnection();
    return;
  }

  // Flushing may read events, which must be dispatched on the loop thread.
  event_loop_->PostTask([this, alive = std::weak_ptr<bool>(alive_)]() {
    if (alive.lock()) {
      DispatchQueuedEvents();
      CheckConnection();
    }
  });
}

bool X11Display::CheckConnection() {
  if (failed_) {
    return false;
  }
  if (xcb_connection_has_error(connection_)) {
    failed_ = true;
    FML_LOG(ERROR) << "Lost connection to the X server";
    NotifyError();
    return false;
  }
  return true;
}

void X11Display::HandleEvent(xcb_generic_event_t* event) {
  uint8_t type = event->response_type & 0x7f;

  if (type == 0) {
    auto* error = reinterpret_cast<xcb_generic_error_t*>(event);
    FML_LOG(WARNING) << "X error " << static_cast<int>(error->error_code)
                     << " for request " << static_cast<int>(error->major_code)
                     << "." << error->minor_code;
    return;
  }

  // Any event other than a press of the same key means a pending release was
  // not part of a repeat.
  if (pending_release_.has_value() && type != XCB_KEY_PRESS) {
    FlushPendingRelease();
  }

  if (type == XCB_GE_GENERIC) {
    auto* generic = reinterpret_cast<xcb_ge_generic_event_t*>(event);
    if (generic->extension == present_opcode_) {
      xcb_window_t window = XCB_NONE;
      if (generic->event_type == XCB_PRESENT_EVENT_COMPLETE_NOTIFY) {
        window = reinterpret_cast<xcb_present_complete_notify_event_t*>(event)
                     ->window;
      } else if (generic->event_type == XCB_PRESENT_EVENT_IDLE_NOTIFY) {
        window =
            reinterpret_cast<xcb_present_idle_notify_event_t*>(event)->window;
      } else if (generic->event_type == XCB_PRESENT_EVENT_CONFIGURE_NOTIFY) {
        window = reinterpret_cast<xcb_present_configure_notify_event_t*>(event)
                     ->window;
      }
      if (X11Window* target = FindWindow(window)) {
        target->HandleEvent(event);
      }
    }
    return;
  }

  if (has_randr_ &&
      (type == randr_event_base_ + XCB_RANDR_SCREEN_CHANGE_NOTIFY ||
       type == randr_event_base_ + XCB_RANDR_NOTIFY)) {
    if (type == randr_event_base_ + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
      auto* change =
          reinterpret_cast<xcb_randr_screen_change_notify_event_t*>(event);
      screen_->width_in_pixels = change->width;
      screen_->height_in_pixels = change->height;
    }
    UpdateMonitors();
    return;
  }

  switch (type) {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
      HandleKey(reinterpret_cast<xcb_key_press_event_t*>(event),
                type == XCB_KEY_PRESS);
      return;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
      HandleButton(reinterpret_cast<xcb_button_press_event_t*>(event),
                   type == XCB_BUTTON_PRESS);
      return;
    case XCB_MOTION_NOTIFY:
      HandleMotion(reinterpret_cast<xcb_motion_notify_event_t*>(event));
      return;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
      HandleCrossing(reinterpret_cast<xcb_enter_notify_event_t*>(event),
                     type == XCB_ENTER_NOTIFY);
      return;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
      HandleFocus(reinterpret_cast<xcb_focus_in_event_t*>(event),
                  type == XCB_FOCUS_IN);
      return;
    case XCB_MAPPING_NOTIFY: {
      auto* mapping = reinterpret_cast<xcb_mapping_notify_event_t*>(event);
      if (mapping->request == XCB_MAPPING_KEYBOARD) {
        UpdateKeymap();
      }
      return;
    }
    default:
      break;
  }

  xcb_window_t window = EventWindow(event);
  if (window == screen_->root && type == XCB_PROPERTY_NOTIFY) {
    auto* property = reinterpret_cast<xcb_property_notify_event_t*>(event);
    if (property->atom == XCB_ATOM_RESOURCE_MANAGER) {
      UpdateScale();
    } else if (property->atom == atom(Atom::kNetWorkarea)) {
      UpdateWorkArea();
      for (auto& [id, target] : windows_) {
        target->OnMonitorsChanged();
      }
    } else if (property->atom == atom(Atom::kXkbRulesNames)) {
      UpdateKeymap();
    }
    return;
  }
  if (clipboard_ != nullptr && window == clipboard_->window()) {
    clipboard_->HandleEvent(event);
    return;
  }
  if (X11Window* target = FindWindow(window)) {
    target->HandleEvent(event);
  }
}

void X11Display::FlushPendingRelease() {
  if (!pending_release_.has_value()) {
    return;
  }
  xcb_key_release_event_t release = *pending_release_;
  pending_release_.reset();
  if (xkb_keyboard_ == nullptr) {
    return;
  }
  uint64_t time = EventTime(release.time);
  UpdateKeyboardState(release.state);
  KeyboardKey key = key_resolver_.Resolve(*xkb_keyboard_, release.detail);
  xkb_keyboard_->UpdateKey(release.detail, false);
  if (keyboard_focus_ != nullptr &&
      keyboard_focus_->keyboard_handler() != nullptr) {
    reported_modifiers_ = xkb_keyboard_->GetModifiers();
    keyboard_focus_->keyboard_handler()->OnKey(time, key, KeyAction::kUp,
                                               *reported_modifiers_);
  }
}

void X11Display::HandleKey(const xcb_key_press_event_t* event, bool pressed) {
  last_input_time_ = event->time;
  if (xkb_keyboard_ == nullptr) {
    return;
  }

  if (!pressed) {
    // The server repeats keys by sending a release and press with the same
    // time, so wait to see if a press follows.
    FlushPendingRelease();
    pending_release_ = *event;
    return;
  }

  bool repeat = pending_release_.has_value() &&
                pending_release_->detail == event->detail &&
                pending_release_->time == event->time;
  if (!repeat) {
    FlushPendingRelease();
  }
  pending_release_.reset();

  uint64_t time = EventTime(event->time);
  UpdateKeyboardState(event->state);
  KeyboardKey key = key_resolver_.Resolve(*xkb_keyboard_, event->detail);
  if (!repeat) {
    xkb_keyboard_->UpdateKey(event->detail, true);
  }
  if (keyboard_focus_ == nullptr ||
      keyboard_focus_->keyboard_handler() == nullptr) {
    return;
  }
  if (repeat && !xkb_keyboard_->KeyRepeats(event->detail)) {
    return;
  }
  reported_modifiers_ = xkb_keyboard_->GetModifiers();
  keyboard_focus_->keyboard_handler()->OnKey(
      time, key, repeat ? KeyAction::kRepeat : KeyAction::kDown,
      *reported_modifiers_);
}

void X11Display::UpdateKeyboardState(uint16_t state) {
  if (xkb_keyboard_ == nullptr) {
    return;
  }
  uint32_t mods = state & kModifierMask;
  uint32_t locked = mods & (kLockMask | kNumLockMask);
  uint32_t group = (state >> 13) & 0x3;
  xkb_keyboard_->UpdateState(mods & ~locked, 0, locked, group);
}

void X11Display::ReportModifiers(uint64_t time) {
  if (xkb_keyboard_ == nullptr || keyboard_focus_ == nullptr ||
      keyboard_focus_->keyboard_handler() == nullptr) {
    return;
  }
  KeyModifiers modifiers = xkb_keyboard_->GetModifiers();
  if (reported_modifiers_ == modifiers) {
    return;
  }
  reported_modifiers_ = modifiers;
  keyboard_focus_->keyboard_handler()->OnKeyModifiers(time, modifiers);
}

void X11Display::HandleButton(const xcb_button_press_event_t* event,
                              bool pressed) {
  last_input_time_ = event->time;
  X11Window* window = FindWindow(event->event);
  if (window == nullptr) {
    return;
  }
  uint64_t time = EventTime(event->time);
  UpdateKeyboardState(event->state);
  ReportModifiers(time);

  if (pressed) {
    DismissPopups(window);
  }

  if (window != pointer_focus_) {
    SetPointerFocus(window, time, event->event_x / scale_,
                    event->event_y / scale_);
  }

  uint8_t button = event->detail;
  if (button >= 4 && button <= 7) {
    if (pressed && window->input_handler() != nullptr) {
      PointerScroll scroll;
      scroll.source = WL_POINTER_AXIS_SOURCE_WHEEL;
      if (button == 4) {
        scroll.dy = -kScrollStep;
      } else if (button == 5) {
        scroll.dy = kScrollStep;
      } else if (button == 6) {
        scroll.dx = -kScrollStep;
      } else {
        scroll.dx = kScrollStep;
      }
      window->input_handler()->OnPointerScroll(time, scroll);
    }
    return;
  }

  uint32_t code = ButtonCode(button);
  if (code == 0) {
    return;
  }
  uint32_t mask = 1u << button;
  if (pressed) {
    pressed_buttons_ |= mask;
  } else {
    pressed_buttons_ &= ~mask;
  }
  if (window->input_handler() != nullptr) {
    window->input_handler()->OnPointerButton(event->sequence, time, code,
                                             pressed);
  }

  // The implicit grab ended, so report the pointer leaving if it did so
  // during the grab.
  if (pressed_buttons_ == 0 && pending_leave_) {
    pending_leave_ = false;
    SetPointerFocus(nullptr, time, 0, 0);
  }
}

void X11Display::HandleMotion(const xcb_motion_notify_event_t* event) {
  X11Window* window = FindWindow(event->event);
  if (window == nullptr) {
    return;
  }
  uint64_t time = EventTime(event->time);
  double x = event->event_x / scale_;
  double y = event->event_y / scale_;
  if (window != pointer_focus_) {
    SetPointerFocus(window, time, x, y);
    return;
  }
  if (window->input_handler() != nullptr) {
    window->input_handler()->OnPointerMotion(time, x, y);
  }
}

void X11Display::HandleCrossing(const xcb_enter_notify_event_t* event,
                                bool enter) {
  X11Window* window = FindWindow(event->event);
  if (window == nullptr) {
    return;
  }
  if (event->detail == XCB_NOTIFY_DETAIL_INFERIOR) {
    return;
  }

  // With PointerRoot focus, keyboard input goes to the window under the
  // pointer. The server reports this with the focus flag rather than focus
  // events. The flag is also set with explicit focus, which is tracked with
  // focus events instead.
  if (focus_follows_pointer_ &&
      (event->same_screen_focus & kCrossingFocusFlag) != 0 &&
      event->mode == XCB_NOTIFY_MODE_NORMAL) {
    X11Window* toplevel = window;
    while (toplevel->role() == Window::Role::kPopup &&
           toplevel->parent() != nullptr) {
      toplevel = static_cast<X11Window*>(toplevel->parent());
    }
    if (enter) {
      pointer_focus_in_ = toplevel;
    } else if (pointer_focus_in_ == toplevel) {
      pointer_focus_in_ = nullptr;
    }
    UpdateFocusedWindow(false);
  }

  uint64_t time = EventTime(event->time);
  if (enter) {
    if (window == pointer_focus_) {
      pending_leave_ = false;
      return;
    }
    SetPointerFocus(window, time, event->event_x / scale_,
                    event->event_y / scale_);
  } else if (window == pointer_focus_) {
    // Events continue to be sent to the window while buttons are held.
    if (pressed_buttons_ != 0 && event->mode == XCB_NOTIFY_MODE_NORMAL) {
      pending_leave_ = true;
      return;
    }
    SetPointerFocus(nullptr, time, 0, 0);
  }
}

void X11Display::SetPointerFocus(X11Window* window,
                                 uint64_t time,
                                 double x,
                                 double y) {
  if (window == pointer_focus_) {
    return;
  }
  if (pointer_focus_ != nullptr && pointer_focus_->input_handler() != nullptr) {
    pointer_focus_->input_handler()->OnPointerLeave(time);
  }
  pointer_focus_ = window;
  pending_leave_ = false;
  pressed_buttons_ = 0;
  if (window == nullptr) {
    return;
  }
  xcb_cursor_t cursor = GetCursor(cursor_shape_);
  xcb_change_window_attributes(connection_, window->window(), XCB_CW_CURSOR,
                               &cursor);
  if (window->input_handler() != nullptr) {
    window->input_handler()->OnPointerEnter(time, x, y);
  }
}

void X11Display::HandleFocus(const xcb_focus_in_event_t* event, bool focus_in) {
  if (event->event == screen_->root) {
    HandleRootFocus(event, focus_in);
    return;
  }
  X11Window* window = FindWindow(event->event);
  // Focus following the pointer is tracked with crossing events.
  if (window == nullptr || event->detail == XCB_NOTIFY_DETAIL_INFERIOR ||
      event->detail == XCB_NOTIFY_DETAIL_POINTER ||
      event->mode == XCB_NOTIFY_MODE_GRAB ||
      event->mode == XCB_NOTIFY_MODE_UNGRAB) {
    return;
  }
  if (focus_in) {
    explicit_focus_ = window;
  } else if (window == explicit_focus_) {
    explicit_focus_ = nullptr;
  }
  UpdateFocusedWindow(true);
}

void X11Display::HandleRootFocus(const xcb_focus_in_event_t* event,
                                 bool focus_in) {
  if (event->mode == XCB_NOTIFY_MODE_GRAB ||
      event->mode == XCB_NOTIFY_MODE_UNGRAB) {
    return;
  }
  bool follows_pointer = focus_follows_pointer_;
  if (focus_in && event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT) {
    follows_pointer = true;
  } else if ((!focus_in && event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT) ||
             (focus_in && event->detail == XCB_NOTIFY_DETAIL_NONE)) {
    follows_pointer = false;
  }
  if (follows_pointer == focus_follows_pointer_) {
    return;
  }
  focus_follows_pointer_ = follows_pointer;

  // The window under the pointer gains or loses focus.
  pointer_focus_in_ = nullptr;
  if (follows_pointer && pointer_focus_ != nullptr) {
    X11Window* toplevel = pointer_focus_;
    while (toplevel->role() == Window::Role::kPopup &&
           toplevel->parent() != nullptr) {
      toplevel = static_cast<X11Window*>(toplevel->parent());
    }
    pointer_focus_in_ = toplevel;
  }
  UpdateFocusedWindow(true);
}

void X11Display::UpdateFocusedWindow(bool dismiss_popups) {
  X11Window* window =
      explicit_focus_ != nullptr ? explicit_focus_ : pointer_focus_in_;
  if (window == focused_window_) {
    return;
  }
  X11Window* old_window = focused_window_;
  focused_window_ = window;
  if (old_window != nullptr) {
    old_window->SetFocused(false);
  }
  if (window != nullptr) {
    window->SetFocused(true);
  } else if (dismiss_popups) {
    DismissPopups(nullptr);
  }
  UpdateKeyboardFocus();
}

void X11Display::UpdateKeyboardFocus() {
  X11Window* target = focused_window_;
  if (target != nullptr) {
    // Send input to the most recently mapped popup of the focused window.
    for (X11Window* popup = target->top_popup(); popup != nullptr;
         popup = popup->top_popup()) {
      if (popup->accepts_input()) {
        target = popup;
      }
    }
  }
  if (target == keyboard_focus_) {
    return;
  }

  uint64_t time = MonotonicMillis() * 1000;
  if (keyboard_focus_ != nullptr &&
      keyboard_focus_->keyboard_handler() != nullptr) {
    keyboard_focus_->keyboard_handler()->OnKeyboardLeave(time);
  }
  keyboard_focus_ = target;
  reported_modifiers_.reset();
  if (target == nullptr || target->keyboard_handler() == nullptr ||
      xkb_keyboard_ == nullptr) {
    return;
  }

  // Find the keys already pressed, and the modifier state.
  std::vector<KeyboardKey> keys;
  xcb_query_keymap_reply_t* keymap = xcb_query_keymap_reply(
      connection_, xcb_query_keymap(connection_), nullptr);
  xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(
      connection_, xcb_query_pointer(connection_, screen_->root), nullptr);
  if (pointer != nullptr) {
    UpdateKeyboardState(pointer->mask);
  }
  if (keymap != nullptr) {
    for (uint32_t keycode = 8; keycode < 256; keycode++) {
      if (keymap->keys[keycode / 8] & (1 << (keycode % 8))) {
        keys.push_back(key_resolver_.Resolve(*xkb_keyboard_, keycode));
      }
    }
  }
  free(keymap);
  free(pointer);
  reported_modifiers_ = xkb_keyboard_->GetModifiers();
  target->keyboard_handler()->OnKeyboardEnter(time, keys, *reported_modifiers_);
}

void X11Display::DismissPopups(X11Window* window) {
  // Close popups of other windows, and popups above |window|.
  std::vector<X11Window*> to_close;
  for (auto& [id, candidate] : windows_) {
    if (candidate->role() != Window::Role::kPopup || !candidate->is_mapped()) {
      continue;
    }
    bool keep = false;
    for (X11Window* ancestor = window; ancestor != nullptr;
         ancestor = static_cast<X11Window*>(ancestor->parent())) {
      if (ancestor == candidate) {
        keep = true;
        break;
      }
    }
    if (!keep) {
      to_close.push_back(candidate);
    }
  }
  for (X11Window* popup : to_close) {
    // Closing one popup may destroy others.
    for (auto& [id, candidate] : windows_) {
      if (candidate == popup) {
        popup->Dismiss();
        break;
      }
    }
  }
}

xcb_cursor_t X11Display::GetCursor(uint32_t shape) {
  auto it = cursors_.find(shape);
  if (it != cursors_.end()) {
    return it->second;
  }

  xcb_cursor_t cursor = xcb_generate_id(connection_);
  if (shape == kHiddenCursorShape) {
    xcb_pixmap_t pixmap = xcb_generate_id(connection_);
    xcb_create_pixmap(connection_, 1, pixmap, screen_->root, 1, 1);
    xcb_gcontext_t gc = xcb_generate_id(connection_);
    uint32_t foreground = 0;
    xcb_create_gc(connection_, gc, pixmap, XCB_GC_FOREGROUND, &foreground);
    xcb_rectangle_t rectangle = {0, 0, 1, 1};
    xcb_poly_fill_rectangle(connection_, pixmap, gc, 1, &rectangle);
    xcb_create_cursor(connection_, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0,
                      0);
    xcb_free_gc(connection_, gc);
    xcb_free_pixmap(connection_, pixmap);
  } else {
    if (cursor_font_ == XCB_NONE) {
      cursor_font_ = xcb_generate_id(connection_);
      xcb_open_font(connection_, cursor_font_, strlen("cursor"), "cursor");
    }
    uint16_t glyph = CursorGlyph(shape);
    xcb_create_glyph_cursor(connection_, cursor, cursor_font_, cursor_font_,
                            glyph, glyph + 1, 0, 0, 0, 0xffff, 0xffff, 0xffff);
  }
  cursors_[shape] = cursor;
  return cursor;
}

uint64_t X11Display::EventTime(xcb_timestamp_t time) const {
  // X servers on Linux use CLOCK_MONOTONIC for event times, in milliseconds.
  // If this one doesn't the current time is used.
  constexpr uint32_t kMaxEventAge = 10000;
  uint64_t now = MonotonicMillis();
  uint32_t age = static_cast<uint32_t>(now) - time;
  if (age > kMaxEventAge) {
    return now * 1000;
  }
  return (now - age) * 1000;
}

std::optional<std::string> X11Display::GetRootProperty(xcb_atom_t property,
                                                       xcb_atom_t type) {
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection_,
      xcb_get_property(connection_, 0, screen_->root, property, type, 0,
                       UINT32_MAX / 4),
      nullptr);
  if (reply == nullptr) {
    return std::nullopt;
  }
  std::optional<std::string> result;
  if (reply->type == type) {
    result = std::string(static_cast<char*>(xcb_get_property_value(reply)),
                         xcb_get_property_value_length(reply));
  }
  free(reply);
  return result;
}

Clipboard* X11Display::clipboard() const {
  return clipboard_.get();
}

std::vector<Display::Output> X11Display::GetOutputs() const {
  std::vector<Output> outputs;
  for (const Monitor& monitor : monitors_) {
    outputs.push_back(monitor.output);
  }
  return outputs;
}

void X11Display::SetCursor(uint32_t shape) {
  cursor_shape_ = shape;
  if (pointer_focus_ == nullptr) {
    return;
  }
  xcb_cursor_t cursor = GetCursor(shape);
  xcb_change_window_attributes(connection_, pointer_focus_->window(),
                               XCB_CW_CURSOR, &cursor);
  xcb_flush(connection_);
}

void X11Display::RingBell(Window* window) {
  xcb_bell(connection_, 0);
  xcb_flush(connection_);
}

std::unique_ptr<Window> X11Display::CreateWindow(
    DecorationRenderer* decoration_renderer,
    Window::Delegate* delegate,
    const Window::Params& params) {
  // The window manager draws decorations.
  return X11Window::Create(this, delegate, params);
}

}  // namespace flutter
