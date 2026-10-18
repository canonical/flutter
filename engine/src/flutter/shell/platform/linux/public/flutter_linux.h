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
// becomes the platform thread. The exception is
// FlutterDesktopApplication::quit, which may be called from any thread.
//
// An application can contain multiple engines, which share the platform
// thread, the connection to the compositor and the GPU:
//
//   auto application = flutter::FlutterDesktopApplication::create();
//   auto engine = flutter::FlutterDesktopEngine::create(application, props);
//   auto view = flutter::FlutterDesktopView::create(engine, 1280, 720);
//   RegisterPlugins(engine.get());
//   engine->run();
//   application->run();
//
// Plugins are registered separately with each engine (see
// flutter/plugin_registrar_linux.h).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <flutter/plugin_registry.h>

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

// How rendered frames are shared with the compositor.
enum class FlutterDesktopBufferSharing {
  // GPU buffers are shared directly with the compositor without copying
  // (zwp_linux_dmabuf_v1).
  kLinuxDmabuf,

  // Frames are copied from the GPU into shared memory (wl_shm). This is used
  // if the compositor or GPU don't support sharing dmabufs.
  kWlShm,
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

// The platform resources shared by all engines in a process.
class FlutterDesktopApplication {
 public:
  // Connects to the Wayland compositor. Returns nullptr if no compositor or
  // GPU is available.
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

  // The Wayland protocols in use.
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
class FlutterDesktopEngine : public PluginRegistry {
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

  ~FlutterDesktopEngine() override { flutter_linux_engine_free(engine_); }

  FlutterDesktopEngine(const FlutterDesktopEngine&) = delete;
  FlutterDesktopEngine& operator=(const FlutterDesktopEngine&) = delete;

  // Starts running Dart code. Views can be created before or after this is
  // called. Returns false on failure.
  bool run() { return flutter_linux_engine_run(engine_); }

  // Returns true if the engine is running.
  bool is_running() const { return flutter_linux_engine_is_running(engine_); }

  // |PluginRegistry|
  // Returns the registrar for the plugin called |plugin_name| in this
  // engine. The plugin is destroyed with the engine.
  FlutterDesktopPluginRegistrarRef GetRegistrarForPlugin(
      const std::string& plugin_name) override {
    return flutter_linux_engine_get_plugin_registrar(engine_,
                                                     plugin_name.c_str());
  }

  // The application this engine is in.
  const std::shared_ptr<FlutterDesktopApplication>& application() const {
    return application_;
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
    info.buffer_sharing =
        abi_info.buffer_sharing == FLUTTER_LINUX_BUFFER_SHARING_WL_SHM
            ? FlutterDesktopBufferSharing::kWlShm
            : FlutterDesktopBufferSharing::kLinuxDmabuf;
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
      : application_(std::move(application)), engine_(engine) {}

  // Keeps the application alive while the engine exists.
  std::shared_ptr<FlutterDesktopApplication> application_;
  FlutterLinuxEngine* engine_;
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
    return std::shared_ptr<FlutterDesktopView>(
        new FlutterDesktopView(std::move(engine), view));
  }

  ~FlutterDesktopView() { flutter_linux_view_free(view_); }

  FlutterDesktopView(const FlutterDesktopView&) = delete;
  FlutterDesktopView& operator=(const FlutterDesktopView&) = delete;

  // The ID Flutter uses for this view.
  int64_t id() const { return flutter_linux_view_get_id(view_); }

  // The engine this view shows.
  const std::shared_ptr<FlutterDesktopEngine>& engine() const {
    return engine_;
  }

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
