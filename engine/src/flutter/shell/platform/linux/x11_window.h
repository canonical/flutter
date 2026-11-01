// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_X11_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_X11_WINDOW_H_

#include <xcb/xcb.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/window.h"

namespace flutter {

class TextureBlitter;
class TextureBuffer;
class X11Display;

// A window on an X11 server. Top-level windows are managed and decorated by
// the window manager. Popups are override-redirect windows the embedder
// positions itself.
//
// Frames are copied from the renderer's textures into pixmaps that are
// shared with the server, using DRI3 if available and shared memory
// otherwise, and shown with the Present extension.
class X11Window : public Window {
 public:
  // Returns nullptr on failure.
  static std::unique_ptr<X11Window> Create(X11Display* display,
                                           Window::Delegate* delegate,
                                           const Params& params);

  ~X11Window() override;

  xcb_window_t window() const { return window_; }

  InputHandler* input_handler() const { return input_handler_; }
  KeyboardHandler* keyboard_handler() const { return keyboard_handler_; }

  // Processes an event for this window from the display.
  void HandleEvent(xcb_generic_event_t* event);

  // Called by the display when the scale or monitors change.
  void OnScaleChanged();
  void OnMonitorsChanged();

  // Called by the display when the window gains or loses input focus.
  void SetFocused(bool focused);

  // True if the window is shown on the screen.
  bool is_mapped() const { return mapped_; }

  // True if a popup receives input.
  bool accepts_input() const { return accepts_input_; }

  // The most recently shown popup placed relative to this window, or nullptr.
  X11Window* top_popup() const;

  // Asks the delegate to close a popup, as the user interacted outside it.
  void Dismiss();

  // |Window|
  int width() const override { return width_; }
  int height() const override { return height_; }
  double scale() const override;
  uint32_t display_id() const override;
  int physical_width() const override { return physical_width_; }
  int physical_height() const override { return physical_height_; }
  Role role() const override { return role_; }
  Window* parent() const override;
  bool maximized() const override { return maximized_; }
  bool fullscreen() const override { return fullscreen_; }
  bool activated() const override { return activated_; }
  bool suspended() const override { return suspended_; }
  bool modal() const override { return modal_; }
  bool resizable() const override { return resizable_; }
  const std::string& title() const override { return title_; }
  int popup_x() const override { return popup_x_; }
  int popup_y() const override { return popup_y_; }
  int max_suggested_width() const override;
  int max_suggested_height() const override;
  bool has_pointer_focus() const override;
  bool has_keyboard_focus() const override;
  void SetInputHandlers(InputHandler* input_handler,
                        KeyboardHandler* keyboard_handler,
                        InputMethodHandler* input_method_handler) override;
  void SetTitle(const std::string& title) override;
  void SetApplicationId(const std::string& application_id) override;
  void SetSize(int width, int height) override;
  void ResizeToContent(int width, int height) override;
  void SetSizeLimits(const SizeLimits& limits) override;
  void SetMaximized(bool maximized) override;
  void SetMinimized() override;
  void SetFullscreen(bool fullscreen) override;
  void Activate() override;
  void SetParent(Window* parent) override;
  void SetPositioner(const Positioner& positioner) override;
  void StopWaiting() override;
  void Present(RenderBuffer* buffer,
               const SyncPoints* sync,
               const std::vector<Layer>& layers) override;
  double ContentCornerRadius(int width, int height) override { return 0; }
  size_t frames_in_flight() override;

 private:
  // A pixmap frames are copied into and shown from.
  struct Buffer;

  X11Window(X11Display* display, Window::Delegate* delegate);

  bool Initialize(const Params& params);

  // Sets the window manager hints describing the window.
  void SetWmHints(const Params& params);

  // Sets the limits on the size the user can resize the window to.
  void UpdateSizeHints();

  // Asks the window manager to add or remove _NET_WM_STATE |state1| (and
  // |state2| if not XCB_NONE).
  void ChangeNetWmState(bool add, xcb_atom_t state1, xcb_atom_t state2);

  // Reads _NET_WM_STATE, informing the delegate of changes.
  void UpdateNetWmState();

  // Places a popup relative to its parent.
  void PositionPopup();

  // Moves the window to |x|, |y| relative to the root window, in physical
  // pixels.
  void Move(int x, int y);

  // Resizes the window to the logical size, in physical pixels.
  void ResizeWindow();

  // Clamps a logical size to the size limits.
  void ClampSize(int* width, int* height) const;

  // Returns the position of the window relative to the root window, in
  // physical pixels, from the server.
  void QueryRootPosition(int* x, int* y) const;

  // Updates the output the window is on, informing the delegate if it
  // changed.
  void UpdateDisplayId();

  void HandleConfigure(xcb_configure_notify_event_t* event);
  void HandleClientMessage(xcb_client_message_event_t* event);
  void HandlePresentEvent(xcb_generic_event_t* event);

  // Returns a buffer to copy a |width| x |height| frame into, waiting for
  // the server to release one if necessary. Called with |buffer_mutex_|
  // held.
  Buffer* AcquireBuffer(std::unique_lock<std::mutex>& lock,
                        int width,
                        int height);

  // Creates a buffer, or returns nullptr on failure. Buffers are shared
  // with the server using DRI3 if possible, otherwise MIT-SHM. Called on the
  // raster thread.
  std::unique_ptr<Buffer> CreateBuffer(TextureBuffer* source,
                                       int width,
                                       int height);
  std::unique_ptr<Buffer> CreateDri3Buffer(TextureBuffer* source,
                                           int width,
                                           int height);
  std::unique_ptr<Buffer> CreateShmBuffer(int width, int height);

  // Frees the server resources of |buffer|. The GPU resources are freed
  // when |buffer| is destroyed, which must be on the raster thread with the
  // render context current, or after the context is destroyed.
  void ReleaseBuffer(Buffer* buffer);

  // Copies |texture| into |buffer|. Called on the raster thread.
  bool CopyToBuffer(TextureBuffer* texture, Buffer* buffer);

  // Shows |buffer| in the window.
  void PresentBuffer(Buffer* buffer);

  // Copies the most recently presented frame into the window.
  void Redraw();

  X11Display* display_;
  Window::Delegate* delegate_;
  xcb_window_t window_ = XCB_NONE;
  Role role_ = Role::kToplevel;
  X11Window* parent_ = nullptr;
  // Popups placed relative to this window, in the order they were created.
  std::vector<X11Window*> popups_;
  bool dismissed_ = false;
  bool decorated_ = true;
  bool modal_ = false;
  bool resizable_ = true;
  bool accepts_input_ = true;
  std::string title_;
  SizeLimits limits_;
  Positioner positioner_;

  InputHandler* input_handler_ = nullptr;
  KeyboardHandler* keyboard_handler_ = nullptr;

  // The logical size of the window, and its size in physical pixels.
  int width_ = 1;
  int height_ = 1;
  std::atomic<int> physical_width_ = 1;
  std::atomic<int> physical_height_ = 1;

  // The position of the window relative to the root window, in physical
  // pixels.
  int root_x_ = 0;
  int root_y_ = 0;

  int popup_x_ = 0;
  int popup_y_ = 0;

  bool maximized_ = false;
  bool fullscreen_ = false;
  bool activated_ = false;
  bool suspended_ = false;
  std::atomic<bool> mapped_ = false;
  bool hidden_ = false;
  uint32_t display_id_ = 0;

  // The _NET_WM_STATE atoms to set when the window is first shown.
  std::vector<xcb_atom_t> initial_states_;

  // Fields used to present frames, which are accessed from the raster and
  // platform threads.
  std::mutex buffer_mutex_;
  std::condition_variable buffer_condition_;
  std::vector<std::unique_ptr<Buffer>> buffers_;
  // Buffers to destroy on the raster thread.
  std::vector<std::unique_ptr<Buffer>> stale_buffers_;
  Buffer* last_presented_ = nullptr;
  uint32_t present_serial_ = 0;
  bool stop_waiting_ = false;
  // Set if sharing buffers with DRI3 failed, so shared memory is used.
  bool dri3_failed_ = false;
  xcb_gcontext_t gc_ = XCB_NONE;
  uint32_t present_event_id_ = 0;
  std::unique_ptr<TextureBlitter> blitter_;
  std::unique_ptr<TextureBuffer> staging_;

  // Frames presented and completed, for frames_in_flight.
  std::atomic<uint64_t> frames_presented_ = 0;
  std::atomic<uint64_t> frames_completed_ = 0;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(X11Window);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_X11_WINDOW_H_
