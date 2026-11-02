// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_DISPLAY_H_

#include <xcb/xcb.h>

#include <sys/types.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/native_display.h"
#include "flutter/shell/platform/linux/xkb.h"

namespace flutter {

class X11Clipboard;
class X11Window;

// A connection to an X11 server, using XCB.
class X11Display : public NativeDisplay {
 public:
  // Atoms the embedder uses.
  enum class Atom {
    kClipboard,
    kIncr,
    kMotifWmHints,
    kNetActiveWindow,
    kNetWmName,
    kNetWmPid,
    kNetWmPing,
    kNetWmState,
    kNetWmStateFullscreen,
    kNetWmStateHidden,
    kNetWmStateMaximizedHorz,
    kNetWmStateMaximizedVert,
    kNetWmStateModal,
    kNetWmWindowType,
    kNetWmWindowTypeDialog,
    kNetWmWindowTypeNormal,
    kNetWmWindowTypePopupMenu,
    kNetWorkarea,
    kSelectionProperty,
    kTargets,
    kUtf8String,
    kWmChangeState,
    kWmDeleteWindow,
    kWmProtocols,
    kXkbRulesNames,
    kCount,
  };

  // A monitor and where it is on the screen, in physical pixels.
  struct Monitor {
    Output output;
    int x = 0;
    int y = 0;
  };

  // Connects to the server named by the DISPLAY environment variable.
  // Returns nullptr on failure.
  static std::unique_ptr<X11Display> Create(EventLoop* event_loop);

  ~X11Display() override;

  xcb_connection_t* connection() const { return connection_; }
  xcb_screen_t* screen() const { return screen_; }
  EventLoop* event_loop() const { return event_loop_; }

  // The visual, depth and colormap windows are created with. A 32-bit visual
  // with alpha is used if available.
  xcb_visualid_t visual() const { return visual_; }
  uint8_t depth() const { return depth_; }
  xcb_colormap_t colormap() const { return colormap_; }

  xcb_atom_t atom(Atom atom) const { return atoms_[static_cast<int>(atom)]; }

  // The major opcode of the Present extension, or zero if it is not
  // supported.
  uint8_t present_opcode() const { return present_opcode_; }

  // True if buffers can be shared with the server using DRI3.
  bool has_dri3() const { return has_dri3_; }

  // True if pixmaps can be created in shared memory (MIT-SHM with file
  // descriptor passing).
  bool has_shm() const { return has_shm_; }

  // True if the SHAPE extension is supported.
  bool has_shape() const { return has_shape_; }

  // The number of physical pixels per logical pixel, from the Xft.dpi
  // resource.
  double scale() const { return scale_; }

  // The time of the most recent user input, for requests the window manager
  // only allows in response to input.
  xcb_timestamp_t last_input_time() const { return last_input_time_; }

  // The monitors connected to the screen.
  const std::vector<Monitor>& monitors() const { return monitors_; }

  // Returns the monitor containing the center of the given rectangle, or the
  // first monitor. Returns nullptr if there are no monitors.
  const Monitor* MonitorAt(int x, int y, int width, int height) const;

  // Returns the area of the screen windows may cover on the monitor
  // containing the given rectangle, in physical pixels.
  void GetWorkArea(int x,
                   int y,
                   int width,
                   int height,
                   int* area_x,
                   int* area_y,
                   int* area_width,
                   int* area_height) const;

  // Called by windows when they are created and destroyed.
  void AddWindow(X11Window* window);
  void RemoveWindow(X11Window* window);

  // Called when a window's input handlers change.
  void OnInputHandlersChanged(X11Window* window);

  // The window input events are currently sent to.
  X11Window* pointer_focus() const { return pointer_focus_; }
  X11Window* keyboard_focus() const { return keyboard_focus_; }

  // Dispatches events that have already been read from the connection.
  void DispatchQueuedEvents();

  // Called by windows when they are mapped or unmapped, so keyboard input
  // can be sent to popups.
  void OnWindowMapped(X11Window* window);

  // Flushes requests to the server, reporting an error if the connection has
  // failed. May be called from any thread.
  void Flush();

  // |EventLoop::Source|
  int GetFd() const override;

  // |EventLoop::Source|
  void Prepare() override;

  // |EventLoop::Source|
  void Dispatch(bool readable) override;

  // |NativeDisplay|
  Backend backend() const override { return Backend::kX11; }

  // |NativeDisplay|
  Clipboard* clipboard() const override;

  // |NativeDisplay|
  std::vector<Output> GetOutputs() const override;

  // |NativeDisplay|
  std::optional<dev_t> main_device() const override { return main_device_; }

  // |NativeDisplay|
  void SetCursor(uint32_t shape) override;

  // |NativeDisplay|
  void RingBell(Window* window) override;

  // |NativeDisplay|
  std::unique_ptr<Window> CreateWindow(DecorationRenderer* decoration_renderer,
                                       Window::Delegate* delegate,
                                       const Window::Params& params) override;

 private:
  X11Display(EventLoop* event_loop, xcb_connection_t* connection, int screen);

  bool Initialize();

  // Interns the atoms in |atoms_|.
  bool InternAtoms();

  // Queries the extensions the embedder uses.
  void QueryExtensions();

  // Enables the XKEYBOARD extension, so the keyboard group is reported in
  // event state.
  void EnableXkb();

  // Chooses the visual windows are created with.
  void ChooseVisual();

  // Reads the monitors from RandR, notifying observers if they changed.
  void UpdateMonitors();

  // Reads the scale from the Xft.dpi resource, informing windows if it
  // changed.
  void UpdateScale();

  // Reads the work area from _NET_WORKAREA.
  void UpdateWorkArea();

  // Compiles the keymap named by the server.
  void UpdateKeymap();

  // Returns the window with |id|, or nullptr if it isn't one of ours.
  X11Window* FindWindow(xcb_window_t id) const;

  // Processes an event.
  void HandleEvent(xcb_generic_event_t* event);

  // Sends a key release that wasn't followed by a repeated press.
  void FlushPendingRelease();

  void HandleKey(const xcb_key_press_event_t* event, bool pressed);
  void HandleButton(const xcb_button_press_event_t* event, bool pressed);
  void HandleMotion(const xcb_motion_notify_event_t* event);
  void HandleCrossing(const xcb_enter_notify_event_t* event, bool enter);
  void HandleFocus(const xcb_focus_in_event_t* event, bool focus_in);

  // Tracks whether focus follows the pointer (PointerRoot focus) from focus
  // events on the root window.
  void HandleRootFocus(const xcb_focus_in_event_t* event, bool focus_in);

  // Updates the focused window after |explicit_focus_| or |pointer_focus_in_|
  // change. Popups are dismissed if |dismiss_popups| and the focus is lost.
  void UpdateFocusedWindow(bool dismiss_popups);

  // Moves the pointer focus to |window|, which may be nullptr, sending enter
  // and leave events.
  void SetPointerFocus(X11Window* window, uint64_t time, double x, double y);

  // Sends keyboard events to the window that should receive them: the most
  // recent popup of the focused window that accepts input, or the focused
  // window itself.
  void UpdateKeyboardFocus();

  // Closes the popups of the top-level window containing |window| that
  // |window| isn't part of, as the user interacted outside them.
  void DismissPopups(X11Window* window);

  // Sets the keyboard state from the modifier and group state reported in
  // an event.
  void UpdateKeyboardState(uint16_t state);

  // Informs the focused window if the modifiers changed since they were
  // last reported.
  void ReportModifiers(uint64_t time);

  // Returns a cursor for the wp_cursor_shape_device_v1 |shape|.
  xcb_cursor_t GetCursor(uint32_t shape);

  // Converts an X server time to microseconds on CLOCK_MONOTONIC.
  uint64_t EventTime(xcb_timestamp_t time) const;

  // Reads a property of the root window.
  std::optional<std::string> GetRootProperty(xcb_atom_t property,
                                             xcb_atom_t type);

  // Checks the connection, reporting an error if it has failed.
  bool CheckConnection();

  EventLoop* event_loop_;
  xcb_connection_t* connection_;
  int screen_number_;
  xcb_screen_t* screen_ = nullptr;
  bool failed_ = false;

  xcb_atom_t atoms_[static_cast<int>(Atom::kCount)] = {};

  xcb_visualid_t visual_ = 0;
  uint8_t depth_ = 0;
  xcb_colormap_t colormap_ = 0;

  uint8_t present_opcode_ = 0;
  uint8_t randr_event_base_ = 0;
  uint8_t xkb_opcode_ = 0;
  bool has_randr_ = false;
  bool has_dri3_ = false;
  bool has_shm_ = false;
  bool has_shape_ = false;
  std::optional<dev_t> main_device_;

  double scale_ = 1.0;
  std::vector<Monitor> monitors_;

  // The work area from _NET_WORKAREA, or empty if unknown.
  int work_area_x_ = 0;
  int work_area_y_ = 0;
  int work_area_width_ = 0;
  int work_area_height_ = 0;

  std::map<xcb_window_t, X11Window*> windows_;
  X11Window* pointer_focus_ = nullptr;
  X11Window* keyboard_focus_ = nullptr;

  // The top-level window the server has given input focus.
  X11Window* explicit_focus_ = nullptr;

  // The top-level window that has input focus because focus follows the
  // pointer (PointerRoot focus, e.g. without a window manager).
  X11Window* pointer_focus_in_ = nullptr;

  // Whether the server's input focus is PointerRoot.
  bool focus_follows_pointer_ = false;

  // The top-level window with input focus: |explicit_focus_| if set,
  // otherwise |pointer_focus_in_|.
  X11Window* focused_window_ = nullptr;
  xcb_timestamp_t last_input_time_ = XCB_CURRENT_TIME;

  // The buttons pressed, which hold an implicit grab on the window they
  // were pressed in, and whether the pointer left that window during the
  // grab.
  uint32_t pressed_buttons_ = 0;
  bool pending_leave_ = false;

  std::unique_ptr<XkbKeyboard> xkb_keyboard_;
  std::string keymap_names_;
  KeyResolver key_resolver_;
  std::optional<KeyModifiers> reported_modifiers_;

  // A key release that may be followed by a press of the same key at the
  // same time, indicating the server is repeating the key.
  std::optional<xcb_key_release_event_t> pending_release_;

  std::map<uint32_t, xcb_cursor_t> cursors_;
  xcb_font_t cursor_font_ = XCB_NONE;
  uint32_t cursor_shape_ = 1;

  std::unique_ptr<X11Clipboard> clipboard_;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(X11Display);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_DISPLAY_H_
