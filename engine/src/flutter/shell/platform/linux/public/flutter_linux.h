// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_

// The C++ API for embedding Flutter in a Linux application.
//
// This is header-only so it is compiled with the application's C++ standard
// library. It is a thin layer over flutter_linux_abi.h.
//
// All objects must be created, used and destroyed on the same thread, which
// becomes the platform thread. Exceptions are noted.
//
// An application can contain multiple engines, which share the platform
// thread, the connection to the compositor and the GPU:
//
//   auto application = flutter::FlutterDesktopApplication::create();
//   auto engine = flutter::FlutterDesktopEngine::create(application, props);
//   auto view = flutter::FlutterDesktopView::create(engine, 1280, 720);
//   RegisterPlugins(*engine);
//   engine->run();
//   application->run();
//
// Plugins are added separately to each engine, which owns them:
//
//   void MyPluginRegisterWithEngine(flutter::FlutterDesktopEngine& engine) {
//     engine.add_plugin(std::make_unique<MyPlugin>(engine));
//   }
//
// Plugins communicate with the framework using the channels in
// cpp_client_wrapper (e.g. flutter::MethodChannel) with the engine's
// messenger. Plugins are destroyed before their engine, so they can keep a
// reference to it, but must not keep a std::shared_ptr to it (or its views)
// as that would stop it from being destroyed.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <flutter/binary_messenger.h>

#include "flutter_linux_abi.h"

namespace flutter {

// A Wayland protocol the embedder is using.
struct FlutterDesktopWaylandProtocol {
  // The name of the global interface, e.g. "wp_presentation".
  std::string name;

  // The version in use. This is the lower of the version the compositor
  // supports and the version the embedder supports.
  uint32_t version;
};

// The renderer Flutter draws with.
enum class FlutterDesktopRenderer {
  kSkia,
  kImpeller,
};

// The graphics API used to access the GPU.
enum class FlutterDesktopGraphicsApi {
  kOpenGLES,
  kVulkan,
};

// The display server the application is connected to.
enum class FlutterDesktopDisplayBackend {
  kWayland,
  kX11,
};

// How rendered frames are shared with the compositor.
enum class FlutterDesktopBufferSharing {
  // GPU buffers are shared directly with the compositor without copying
  // (zwp_linux_dmabuf_v1).
  kLinuxDmabuf,

  // Frames are copied from the GPU into shared memory (wl_shm). This is used
  // if the compositor or GPU don't support sharing dmabufs.
  kWlShm,

  // X11: frames are copied on the GPU into buffers shared with the X server
  // (DRI3).
  kDri3,

  // X11: frames are copied from the GPU into memory shared with the X server
  // (MIT-SHM). This is used if the X server or GPU don't support DRI3.
  kMitShm,
};

// How access to shared buffers is coordinated with the compositor.
enum class FlutterDesktopBufferSynchronization {
  // The kernel driver tracks GPU access to buffers.
  kImplicit,

  // Fences are passed to and from the compositor
  // (wp_linux_drm_syncobj_manager_v1).
  kExplicit,
};

// Information about how an engine renders and presents frames.
struct FlutterDesktopRenderingInfo {
  FlutterDesktopRenderer renderer = FlutterDesktopRenderer::kSkia;

  FlutterDesktopGraphicsApi graphics_api = FlutterDesktopGraphicsApi::kOpenGLES;

  // The path of the GPU device rendered with, e.g. "/dev/dri/renderD128".
  std::string gpu_device;

  // The OpenGL implementation, if graphics_api is kOpenGLES.
  std::string gl_vendor;
  std::string gl_renderer;
  std::string gl_version;

  FlutterDesktopBufferSharing buffer_sharing =
      FlutterDesktopBufferSharing::kLinuxDmabuf;

  // The pixel format of the buffers, as a DRM fourcc code (drm_fourcc.h).
  uint32_t buffer_format = 0;

  // The DRM format modifier describing the memory layout of the most
  // recently allocated buffer (e.g. tiling or compression). This is
  // DRM_FORMAT_MOD_INVALID (0x00ffffffffffffff) if the driver chose the
  // layout without reporting it, or no frame has been drawn yet. Shared
  // memory buffers are DRM_FORMAT_MOD_LINEAR (0).
  uint64_t buffer_modifier = 0;

  FlutterDesktopBufferSynchronization buffer_synchronization =
      FlutterDesktopBufferSynchronization::kImplicit;

  // True if frames are scheduled using the times the compositor reports they
  // were shown (wp_presentation).
  bool presentation_feedback = false;

  // True if every frame is shown for at least one display refresh
  // (wp_fifo_v1).
  bool fifo = false;

  // True if frames are rendered at fractional scales rather than the next
  // integer scale (wp_fractional_scale_v1 and wp_viewporter).
  bool fractional_scale = false;
};

class FlutterDesktopView;

// A plugin added to an engine. Plugins are destroyed before their engine, in
// the reverse order they were added.
class FlutterDesktopPlugin {
 public:
  virtual ~FlutterDesktopPlugin() = default;
};

// Receives notifications when views are added to or removed from an engine.
class FlutterDesktopViewObserver {
 public:
  virtual ~FlutterDesktopViewObserver() = default;

  // Called after |view| is added.
  virtual void on_view_added(FlutterDesktopView& view) {}

  // Called before |view| is removed. The view is destroyed after this
  // returns.
  virtual void on_view_removed(FlutterDesktopView& view) {}
};

// Events on a file descriptor being watched with
// FlutterDesktopEngine::add_fd_watch.
struct FlutterDesktopFdEvents {
  static constexpr uint32_t kReadable = FLUTTER_LINUX_FD_EVENT_READABLE;
  static constexpr uint32_t kWritable = FLUTTER_LINUX_FD_EVENT_WRITABLE;
  // Always reported, even if not requested.
  static constexpr uint32_t kError = FLUTTER_LINUX_FD_EVENT_ERROR;
  static constexpr uint32_t kHangUp = FLUTTER_LINUX_FD_EVENT_HANG_UP;
};

// A frame for a pixel buffer texture. See FlutterLinuxPixelBuffer.
using FlutterDesktopPixelBuffer = FlutterLinuxPixelBuffer;

// A frame for a dmabuf texture. See FlutterLinuxDmabuf.
using FlutterDesktopDmabuf = FlutterLinuxDmabuf;

// A pointer event in a platform view. See
// FlutterLinuxPlatformViewPointerEvent.
using FlutterDesktopPlatformViewPointerEvent =
    FlutterLinuxPlatformViewPointerEvent;

// A buffer that can be presented in a platform view. It may outlive the
// view.
class FlutterDesktopPlatformViewBuffer {
 public:
  ~FlutterDesktopPlatformViewBuffer() {
    flutter_linux_platform_view_buffer_free(buffer_);
  }

  FlutterDesktopPlatformViewBuffer(const FlutterDesktopPlatformViewBuffer&) =
      delete;
  FlutterDesktopPlatformViewBuffer& operator=(
      const FlutterDesktopPlatformViewBuffer&) = delete;

 private:
  friend class FlutterDesktopPlatformViewSurface;

  explicit FlutterDesktopPlatformViewBuffer(std::function<void()> release)
      : release_(std::move(release)) {}

  FlutterLinuxPlatformViewBuffer* buffer_ = nullptr;
  std::function<void()> release_;
};

// Shows the content of a platform view. Valid until the view's
// FlutterDesktopPlatformView is destroyed.
class FlutterDesktopPlatformViewSurface {
 public:
  explicit FlutterDesktopPlatformViewSurface(FlutterLinuxPlatformView* view)
      : view_(view) {}

  // The ID the framework uses for the view.
  int64_t id() const { return flutter_linux_platform_view_get_id(view_); }

  // Creates a buffer from |dmabuf|, ignoring its release callback.
  // |release| is called each time the buffer can be reused after being
  // presented. Returns nullptr if the compositor doesn't support the
  // buffer. See flutter_linux_platform_view_create_buffer.
  std::unique_ptr<FlutterDesktopPlatformViewBuffer> create_buffer(
      const FlutterDesktopDmabuf& dmabuf,
      std::function<void()> release = nullptr) {
    std::unique_ptr<FlutterDesktopPlatformViewBuffer> buffer(
        new FlutterDesktopPlatformViewBuffer(std::move(release)));
    FlutterDesktopDmabuf d = dmabuf;
    d.release_callback = [](void* user_data) {
      auto* b = static_cast<FlutterDesktopPlatformViewBuffer*>(user_data);
      if (b->release_) {
        b->release_();
      }
    };
    d.release_user_data = buffer.get();
    buffer->buffer_ = flutter_linux_platform_view_create_buffer(view_, &d);
    if (buffer->buffer_ == nullptr) {
      return nullptr;
    }
    return buffer;
  }

  // Shows |buffer|, replacing the previous buffer. Rendering into it must be
  // complete. Pass nullptr to show nothing.
  void present(FlutterDesktopPlatformViewBuffer* buffer) {
    flutter_linux_platform_view_present(
        view_, buffer != nullptr ? buffer->buffer_ : nullptr);
  }

 private:
  FlutterLinuxPlatformView* view_;
};

// Native content shown in a platform view widget, such as LinuxView. Created
// by the factory registered with
// FlutterDesktopEngine::register_platform_view_type and destroyed when the
// framework disposes the view.
class FlutterDesktopPlatformView {
 public:
  virtual ~FlutterDesktopPlatformView() = default;

  // Called when the size the view is shown at changes. Buffers of
  // |width| x |height| physical pixels are shown without scaling.
  virtual void on_resize(int width, int height, double scale) {}

  // Called with pointer events in the view.
  virtual void on_pointer_event(
      const FlutterDesktopPlatformViewPointerEvent& event) {}
};

// Creates the content for a platform view shown in |surface|, with |params|
// from the widget's creationParams encoded with its codec. Returns nullptr if
// the view can't be created.
using FlutterDesktopPlatformViewFactory =
    std::function<std::unique_ptr<FlutterDesktopPlatformView>(
        FlutterDesktopPlatformViewSurface surface,
        const std::vector<uint8_t>& params)>;

// Sends and receives messages on platform channels with the framework
// running in an engine.
//
// Send may be called from any thread while the engine exists; replies are
// called on the platform thread. Handlers are called on the platform thread,
// and may reply from any thread. A reply that is destroyed without being
// called sends an empty response, meaning the message was not handled.
class FlutterDesktopMessenger : public BinaryMessenger {
 public:
  explicit FlutterDesktopMessenger(FlutterLinuxMessenger* messenger)
      : messenger_(messenger) {}

  FlutterDesktopMessenger(const FlutterDesktopMessenger&) = delete;
  FlutterDesktopMessenger& operator=(const FlutterDesktopMessenger&) = delete;

  // |BinaryMessenger|
  // |reply| is called exactly once, with nullptr if there was no reply
  // (including if the engine is destroyed first).
  void Send(const std::string& channel,
            const uint8_t* message,
            size_t message_size,
            BinaryReply reply = nullptr) const override {
    if (!reply) {
      flutter_linux_messenger_send(messenger_, channel.c_str(), message,
                                   message_size, nullptr, nullptr);
      return;
    }
    flutter_linux_messenger_send(messenger_, channel.c_str(), message,
                                 message_size, OnReply,
                                 new BinaryReply(std::move(reply)));
  }

  // |BinaryMessenger|
  // The handler is destroyed when replaced, removed or the engine is
  // destroyed.
  void SetMessageHandler(const std::string& channel,
                         BinaryMessageHandler handler) override {
    if (!handler) {
      flutter_linux_messenger_set_handler(messenger_, channel.c_str(), nullptr,
                                          nullptr, nullptr);
      return;
    }
    flutter_linux_messenger_set_handler(
        messenger_, channel.c_str(), OnMessage,
        new BinaryMessageHandler(std::move(handler)), [](void* user_data) {
          delete static_cast<BinaryMessageHandler*>(user_data);
        });
  }

 private:
  // Sends a response exactly once, with an empty response if destroyed
  // without being sent.
  class Responder {
   public:
    explicit Responder(FlutterLinuxMessageResponse* response)
        : response_(response) {}
    ~Responder() { send(nullptr, 0); }

    Responder(const Responder&) = delete;
    Responder& operator=(const Responder&) = delete;

    void send(const uint8_t* data, size_t size) {
      FlutterLinuxMessageResponse* response = response_;
      response_ = nullptr;
      if (response != nullptr) {
        flutter_linux_message_response_send(response, data, size);
      }
    }

   private:
    FlutterLinuxMessageResponse* response_;
  };

  static void OnReply(const uint8_t* data, size_t size, void* user_data) {
    std::unique_ptr<BinaryReply> reply(static_cast<BinaryReply*>(user_data));
    (*reply)(data, size);
  }

  static void OnMessage(const uint8_t* data,
                        size_t size,
                        FlutterLinuxMessageResponse* response,
                        void* user_data) {
    auto responder = std::make_shared<Responder>(response);
    // Copy the handler as it may replace itself.
    BinaryMessageHandler handler =
        *static_cast<BinaryMessageHandler*>(user_data);
    handler(data, size,
            [responder = std::move(responder)](const uint8_t* reply,
                                               size_t reply_size) {
              responder->send(reply, reply_size);
            });
  }

  FlutterLinuxMessenger* messenger_;
};

// The platform resources shared by all engines in a process.
class FlutterDesktopApplication {
 public:
  // Connects to the display server: the Wayland compositor if available,
  // otherwise the X server. The FLUTTER_LINUX_BACKEND environment variable
  // may be set to "wayland" or "x11" to choose one. Returns nullptr if no
  // display server or GPU is available.
  static std::shared_ptr<FlutterDesktopApplication> create() {
    FlutterLinuxApplication* application = flutter_linux_application_new();
    if (application == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopApplication>(
        new FlutterDesktopApplication(application));
  }

  ~FlutterDesktopApplication() { flutter_linux_application_free(application_); }

  FlutterDesktopApplication(const FlutterDesktopApplication&) = delete;
  FlutterDesktopApplication& operator=(const FlutterDesktopApplication&) =
      delete;

  // Processes events for all engines until quit is called, the connection to
  // the compositor is lost, or the user closes a view that doesn't have a
  // close callback.
  void run() { flutter_linux_application_run(application_); }

  // Causes run to return. May be called from any thread.
  void quit() { flutter_linux_application_quit(application_); }

  // Sets the ID used to match windows to the application's desktop file.
  // Applies to windows created after this call, including windows created by
  // the framework.
  void set_application_id(const std::string& application_id) {
    flutter_linux_application_set_application_id(application_,
                                                 application_id.c_str());
  }

  // The display server the application is connected to.
  FlutterDesktopDisplayBackend display_backend() const {
    return flutter_linux_application_get_display_backend(application_) ==
                   FLUTTER_LINUX_DISPLAY_BACKEND_X11
               ? FlutterDesktopDisplayBackend::kX11
               : FlutterDesktopDisplayBackend::kWayland;
  }

  // The connection to the Wayland compositor, or nullptr if using X11.
  // Plugins may use this to create their own Wayland objects, with their own
  // event queue if used from other threads.
  struct wl_display* wayland_display() const {
    return flutter_linux_application_get_wl_display(application_);
  }

  // The connection to the X server, or nullptr if using Wayland. Events on
  // the connection are processed by the embedder.
  struct xcb_connection_t* xcb_connection() const {
    return flutter_linux_application_get_xcb_connection(application_);
  }

  // The Wayland protocols in use, which is empty if using X11.
  std::vector<FlutterDesktopWaylandProtocol> wayland_protocols() const {
    size_t count =
        flutter_linux_application_get_wayland_protocol_count(application_);
    std::vector<FlutterDesktopWaylandProtocol> protocols;
    protocols.reserve(count);
    for (size_t i = 0; i < count; i++) {
      const char* name = nullptr;
      uint32_t version = 0;
      flutter_linux_application_get_wayland_protocol(application_, i, &name,
                                                     &version);
      protocols.push_back({name, version});
    }
    return protocols;
  }

 private:
  friend class FlutterDesktopEngine;

  explicit FlutterDesktopApplication(FlutterLinuxApplication* application)
      : application_(application) {}

  FlutterLinuxApplication* application_;
};

// Properties for configuring a Flutter engine.
struct FlutterDesktopEngineProperties {
  // The path to the flutter_assets folder for the application to be run.
  // Relative paths are relative to the directory containing the executable.
  std::string assets_path;

  // The path to the icudtl.dat file for the version of Flutter being used.
  // Relative paths are relative to the directory containing the executable.
  std::string icu_data_path;

  // The path to the AOT library for the application. This is ignored for
  // non-AOT (debug) builds. Relative paths are relative to the directory
  // containing the executable.
  std::string aot_library_path;

  // The name of the top-level Dart entrypoint function. If empty, 'main' is
  // used. Custom entrypoints must be in the same library as main() and be
  // decorated with `@pragma('vm:entry-point')`.
  std::string dart_entrypoint;

  // Arguments passed to the Dart entrypoint.
  std::vector<std::string> dart_entrypoint_arguments;
};

// An instance of the Flutter engine.
class FlutterDesktopEngine {
 public:
  // Creates an engine in |application|. Returns nullptr on failure.
  static std::shared_ptr<FlutterDesktopEngine> create(
      std::shared_ptr<FlutterDesktopApplication> application,
      const FlutterDesktopEngineProperties& properties) {
    std::vector<const char*> argv;
    argv.reserve(properties.dart_entrypoint_arguments.size());
    for (const std::string& argument : properties.dart_entrypoint_arguments) {
      argv.push_back(argument.c_str());
    }

    FlutterLinuxEngineProperties abi_properties = {};
    abi_properties.assets_path = properties.assets_path.c_str();
    abi_properties.icu_data_path = properties.icu_data_path.c_str();
    abi_properties.aot_library_path = properties.aot_library_path.c_str();
    abi_properties.dart_entrypoint = properties.dart_entrypoint.c_str();
    abi_properties.dart_entrypoint_argc = static_cast<int>(argv.size());
    abi_properties.dart_entrypoint_argv = argv.data();

    FlutterLinuxEngine* engine =
        flutter_linux_engine_new(application->application_, &abi_properties);
    if (engine == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<FlutterDesktopEngine>(
        new FlutterDesktopEngine(std::move(application), engine));
  }

  ~FlutterDesktopEngine() {
    // Destroy the plugins while the engine is still usable, as they may use
    // it as they are destroyed.
    while (!plugins_.empty()) {
      plugins_.pop_back();
    }
    flutter_linux_engine_free(engine_);
  }

  FlutterDesktopEngine(const FlutterDesktopEngine&) = delete;
  FlutterDesktopEngine& operator=(const FlutterDesktopEngine&) = delete;

  // Starts running Dart code. Views can be created before or after this is
  // called. Returns false on failure.
  bool run() { return flutter_linux_engine_run(engine_); }

  // Returns true if the engine is running.
  bool is_running() const { return flutter_linux_engine_is_running(engine_); }

  // The messenger for communicating with the framework on platform
  // channels.
  BinaryMessenger* messenger() { return &messenger_; }

  // Adds |plugin| to this engine, which destroys it before the engine is
  // destroyed.
  void add_plugin(std::unique_ptr<FlutterDesktopPlugin> plugin) {
    plugins_.push_back(std::move(plugin));
  }

  // Adds |observer| to be notified when views are added and removed.
  // on_view_added is called for existing views before this returns. The
  // observer must be removed before it is destroyed.
  void add_view_observer(FlutterDesktopViewObserver* observer) {
    view_observers_.push_back(observer);
    for (FlutterDesktopView* view : views()) {
      if (!has_view_observer(observer)) {
        break;
      }
      observer->on_view_added(*view);
    }
  }

  void remove_view_observer(FlutterDesktopViewObserver* observer) {
    view_observers_.erase(
        std::remove(view_observers_.begin(), view_observers_.end(), observer),
        view_observers_.end());
  }

  // The view with |view_id|, or nullptr if there is no such view.
  FlutterDesktopView* view(int64_t view_id) const {
    auto it = views_.find(view_id);
    return it != views_.end() ? it->second : nullptr;
  }

  // The views of this engine.
  std::vector<FlutterDesktopView*> views() const {
    std::vector<FlutterDesktopView*> views;
    views.reserve(views_.size());
    for (const auto& [view_id, view] : views_) {
      views.push_back(view);
    }
    return views;
  }

  // Runs |task| on the platform thread after |delay|. Tasks that haven't run
  // when the engine is destroyed are destroyed without running. May be
  // called from any thread while the engine exists.
  void post_task(std::function<void()> task,
                 std::chrono::nanoseconds delay = {}) {
    flutter_linux_engine_post_task(
        engine_, static_cast<uint64_t>(std::max<int64_t>(delay.count(), 0)),
        [](void* user_data) {
          (*static_cast<std::function<void()>*>(user_data))();
        },
        [](void* user_data) {
          delete static_cast<std::function<void()>*>(user_data);
        },
        new std::function<void()>(std::move(task)));
  }

  // Calls |callback| on the platform thread when any of |events|
  // (FlutterDesktopFdEvents flags) occur on |fd|. Returns an ID for the
  // watch, or zero on failure. Watches are removed when the engine is
  // destroyed.
  uint64_t add_fd_watch(int fd,
                        uint32_t events,
                        std::function<void(uint32_t events)> callback) {
    using Callback = std::function<void(uint32_t)>;
    return flutter_linux_engine_add_fd_watch(
        engine_, fd, events,
        [](int fd, uint32_t events, void* user_data) {
          // Copy the callback as it may remove the watch.
          Callback callback = *static_cast<Callback*>(user_data);
          callback(events);
        },
        [](void* user_data) { delete static_cast<Callback*>(user_data); },
        new Callback(std::move(callback)));
  }

  // Changes the events the watch with |watch_id| waits for.
  bool set_fd_watch_events(uint64_t watch_id, uint32_t events) {
    return flutter_linux_engine_set_fd_watch_events(engine_, watch_id, events);
  }

  // Removes the watch with |watch_id|. May be called from its callback.
  void remove_fd_watch(uint64_t watch_id) {
    flutter_linux_engine_remove_fd_watch(engine_, watch_id);
  }

  // Registers a texture that copies frames from CPU memory, and returns its
  // ID for use in a Texture widget, or -1 on failure. |callback| is called on
  // the raster thread with the size the texture is drawn at, and returns the
  // latest frame or nullptr. The returned frame only needs to remain valid
  // until the callback returns.
  int64_t register_pixel_buffer_texture(
      std::function<const FlutterDesktopPixelBuffer*(size_t width,
                                                     size_t height)> callback) {
    return RegisterTexture(flutter_linux_engine_register_pixel_buffer_texture,
                           std::move(callback));
  }

  // Registers a texture that shows frames from dmabufs without copying them,
  // and returns its ID for use in a Texture widget, or -1 on failure.
  // |callback| is called on the raster thread with the size the texture is
  // drawn at, and returns the latest frame or nullptr. The returned frame
  // only needs to remain valid until the callback returns.
  int64_t register_dmabuf_texture(
      std::function<const FlutterDesktopDmabuf*(size_t width, size_t height)>
          callback) {
    return RegisterTexture(flutter_linux_engine_register_dmabuf_texture,
                           std::move(callback));
  }

  // Tells the engine the texture with |texture_id| has a new frame. May be
  // called from any thread while the engine exists. Returns false if there
  // is no such texture.
  bool mark_texture_frame_available(int64_t texture_id) {
    return flutter_linux_engine_mark_texture_frame_available(engine_,
                                                             texture_id);
  }

  // Unregisters the texture with |texture_id|. Its callback may be called
  // until |done| is called on the platform thread, which may be after the
  // engine is destroyed.
  void unregister_texture(int64_t texture_id,
                          std::function<void()> done = nullptr) {
    if (!done) {
      flutter_linux_engine_unregister_texture(engine_, texture_id, nullptr,
                                              nullptr);
      return;
    }
    flutter_linux_engine_unregister_texture(
        engine_, texture_id,
        [](void* user_data) {
          std::unique_ptr<std::function<void()>> done(
              static_cast<std::function<void()>*>(user_data));
          (*done)();
        },
        new std::function<void()>(std::move(done)));
  }

  // The application this engine is in.
  const std::shared_ptr<FlutterDesktopApplication>& application() const {
    return application_;
  }

  // Registers |factory| to create platform views of |view_type|, replacing
  // any existing factory. Platform views are only supported on Wayland; on
  // X11 requests to create them fail.
  void register_platform_view_type(const std::string& view_type,
                                   FlutterDesktopPlatformViewFactory factory) {
    flutter_linux_engine_register_platform_view_type(
        engine_, view_type.c_str(),
        [](FlutterLinuxPlatformView* view, int64_t view_id,
           const uint8_t* params, size_t params_size, void* user_data) -> bool {
          auto* factory =
              static_cast<FlutterDesktopPlatformViewFactory*>(user_data);
          std::vector<uint8_t> p;
          if (params != nullptr) {
            p.assign(params, params + params_size);
          }
          std::unique_ptr<FlutterDesktopPlatformView> platform_view =
              (*factory)(FlutterDesktopPlatformViewSurface(view), p);
          if (!platform_view) {
            return false;
          }
          static const FlutterLinuxPlatformViewCallbacks callbacks = {
              .struct_size = sizeof(FlutterLinuxPlatformViewCallbacks),
              .resize =
                  [](int width, int height, double scale, void* user_data) {
                    static_cast<FlutterDesktopPlatformView*>(user_data)
                        ->on_resize(width, height, scale);
                  },
              .pointer_event =
                  [](const FlutterLinuxPlatformViewPointerEvent* event,
                     void* user_data) {
                    static_cast<FlutterDesktopPlatformView*>(user_data)
                        ->on_pointer_event(*event);
                  },
          };
          flutter_linux_platform_view_set_callbacks(
              view, &callbacks, platform_view.release(), [](void* user_data) {
                delete static_cast<FlutterDesktopPlatformView*>(user_data);
              });
          return true;
        },
        [](void* user_data) {
          delete static_cast<FlutterDesktopPlatformViewFactory*>(user_data);
        },
        new FlutterDesktopPlatformViewFactory(std::move(factory)));
  }

  // Information about how this engine renders and presents frames. Some
  // values are only known once the first frame has been drawn.
  FlutterDesktopRenderingInfo rendering_info() const {
    FlutterLinuxRenderingInfo abi_info = {};
    abi_info.struct_size = sizeof(abi_info);
    flutter_linux_engine_get_rendering_info(engine_, &abi_info);

    FlutterDesktopRenderingInfo info;
    info.renderer = abi_info.renderer == FLUTTER_LINUX_RENDERER_IMPELLER
                        ? FlutterDesktopRenderer::kImpeller
                        : FlutterDesktopRenderer::kSkia;
    info.graphics_api =
        abi_info.graphics_api == FLUTTER_LINUX_GRAPHICS_API_VULKAN
            ? FlutterDesktopGraphicsApi::kVulkan
            : FlutterDesktopGraphicsApi::kOpenGLES;
    info.gpu_device = abi_info.gpu_device;
    info.gl_vendor = abi_info.gl_vendor;
    info.gl_renderer = abi_info.gl_renderer;
    info.gl_version = abi_info.gl_version;
    switch (abi_info.buffer_sharing) {
      case FLUTTER_LINUX_BUFFER_SHARING_WL_SHM:
        info.buffer_sharing = FlutterDesktopBufferSharing::kWlShm;
        break;
      case FLUTTER_LINUX_BUFFER_SHARING_DRI3:
        info.buffer_sharing = FlutterDesktopBufferSharing::kDri3;
        break;
      case FLUTTER_LINUX_BUFFER_SHARING_MIT_SHM:
        info.buffer_sharing = FlutterDesktopBufferSharing::kMitShm;
        break;
      case FLUTTER_LINUX_BUFFER_SHARING_LINUX_DMABUF:
      default:
        info.buffer_sharing = FlutterDesktopBufferSharing::kLinuxDmabuf;
        break;
    }
    info.buffer_format = abi_info.buffer_format;
    info.buffer_modifier = abi_info.buffer_modifier;
    info.buffer_synchronization =
        abi_info.buffer_synchronization ==
                FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_EXPLICIT
            ? FlutterDesktopBufferSynchronization::kExplicit
            : FlutterDesktopBufferSynchronization::kImplicit;
    info.presentation_feedback = abi_info.presentation_feedback;
    info.fifo = abi_info.fifo;
    info.fractional_scale = abi_info.fractional_scale;
    return info;
  }

 private:
  friend class FlutterDesktopView;

  FlutterDesktopEngine(std::shared_ptr<FlutterDesktopApplication> application,
                       FlutterLinuxEngine* engine)
      : application_(std::move(application)),
        engine_(engine),
        messenger_(flutter_linux_engine_get_messenger(engine)) {}

  bool has_view_observer(FlutterDesktopViewObserver* observer) const {
    return std::find(view_observers_.begin(), view_observers_.end(),
                     observer) != view_observers_.end();
  }

  // Called by FlutterDesktopView when it is created and destroyed.
  void add_view(int64_t view_id, FlutterDesktopView* view) {
    views_[view_id] = view;
    // Copied as observers may add or remove observers.
    std::vector<FlutterDesktopViewObserver*> observers = view_observers_;
    for (FlutterDesktopViewObserver* observer : observers) {
      if (has_view_observer(observer)) {
        observer->on_view_added(*view);
      }
    }
  }
  void remove_view(int64_t view_id, FlutterDesktopView* view) {
    std::vector<FlutterDesktopViewObserver*> observers = view_observers_;
    for (FlutterDesktopViewObserver* observer : observers) {
      if (has_view_observer(observer)) {
        observer->on_view_removed(*view);
      }
    }
    views_.erase(view_id);
  }

  // Registers a texture with |register_function|, which takes a C callback
  // that returns a |Frame|.
  template <typename Frame, typename RegisterFunction>
  int64_t RegisterTexture(
      RegisterFunction register_function,
      std::function<const Frame*(size_t width, size_t height)> callback) {
    using Callback = std::function<const Frame*(size_t, size_t)>;
    return register_function(
        engine_,
        [](size_t width, size_t height, void* user_data) -> const Frame* {
          return (*static_cast<Callback*>(user_data))(width, height);
        },
        [](void* user_data) { delete static_cast<Callback*>(user_data); },
        new Callback(std::move(callback)));
  }

  // Keeps the application alive while the engine exists.
  std::shared_ptr<FlutterDesktopApplication> application_;
  FlutterLinuxEngine* engine_;
  FlutterDesktopMessenger messenger_;
  std::vector<std::unique_ptr<FlutterDesktopPlugin>> plugins_;
  std::vector<FlutterDesktopViewObserver*> view_observers_;

  // The views of this engine, which keep the engine alive.
  std::map<int64_t, FlutterDesktopView*> views_;
};

// A view showing Flutter content in a top-level window.
class FlutterDesktopView {
 public:
  // Creates a view of |engine| in a new window of the given logical size. The
  // compositor may choose a different size. Returns nullptr on failure.
  //
  // Only a single view per engine is currently supported.
  static std::shared_ptr<FlutterDesktopView>
  create(std::shared_ptr<FlutterDesktopEngine> engine, int width, int height) {
    FlutterLinuxView* view =
        flutter_linux_view_new(engine->engine_, width, height);
    if (view == nullptr) {
      return nullptr;
    }
    std::shared_ptr<FlutterDesktopView> result(
        new FlutterDesktopView(std::move(engine), view));
    result->engine_->add_view(result->id(), result.get());
    return result;
  }

  ~FlutterDesktopView() {
    engine_->remove_view(id(), this);
    flutter_linux_view_free(view_);
  }

  FlutterDesktopView(const FlutterDesktopView&) = delete;
  FlutterDesktopView& operator=(const FlutterDesktopView&) = delete;

  // The ID Flutter uses for this view.
  int64_t id() const { return flutter_linux_view_get_id(view_); }

  // The engine this view shows.
  const std::shared_ptr<FlutterDesktopEngine>& engine() const {
    return engine_;
  }

  // The surface the view's content is shown in. Its origin is the origin of
  // the content, and it is the root of the window's surface tree, so it can
  // be used to identify the window (e.g. with xdg-foreign). nullptr if using
  // X11.
  struct wl_surface* wayland_surface() const {
    return flutter_linux_view_get_wl_surface(view_);
  }

  // The X11 window the view's content is shown in, or zero if using Wayland.
  uint32_t x11_window() const {
    return flutter_linux_view_get_x11_window(view_);
  }

  // The logical width of the view's content.
  int width() const {
    int width = 0;
    flutter_linux_view_get_size(view_, &width, nullptr);
    return width;
  }

  // The logical height of the view's content.
  int height() const {
    int height = 0;
    flutter_linux_view_get_size(view_, nullptr, &height);
    return height;
  }

  // The number of physical pixels per logical pixel.
  double scale() const { return flutter_linux_view_get_scale(view_); }

  // Sets the window title.
  void set_title(const std::string& title) {
    flutter_linux_view_set_title(view_, title.c_str());
  }

  // Sets the application ID of the window. The compositor uses this to group
  // windows and find the application's .desktop file, so it should match the
  // name of that file.
  void set_application_id(const std::string& application_id) {
    flutter_linux_view_set_application_id(view_, application_id.c_str());
  }

  // Sets a function to call when the user requests the window be closed. By
  // default, the application quits. The view may be destroyed from
  // |callback|. Pass an empty function to restore the default behavior.
  void set_close_callback(std::function<void()> callback) {
    close_callback_ = std::move(callback);
    if (close_callback_) {
      flutter_linux_view_set_close_callback(view_, OnClose, this);
    } else {
      flutter_linux_view_set_close_callback(view_, nullptr, nullptr);
    }
  }

 private:
  FlutterDesktopView(std::shared_ptr<FlutterDesktopEngine> engine,
                     FlutterLinuxView* view)
      : engine_(std::move(engine)), view_(view) {}

  static void OnClose(void* user_data) {
    // Copy the callback as it may destroy the view.
    std::function<void()> callback =
        static_cast<FlutterDesktopView*>(user_data)->close_callback_;
    callback();
  }

  // Keeps the engine alive while the view exists.
  std::shared_ptr<FlutterDesktopEngine> engine_;
  FlutterLinuxView* view_;
  std::function<void()> close_callback_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
