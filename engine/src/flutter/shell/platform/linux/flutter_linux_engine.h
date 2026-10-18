// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/compositor_opengl.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/flutter_linux_application.h"
#include "flutter/shell/platform/linux/keyboard_manager.h"
#include "flutter/shell/platform/linux/platform_messenger.h"
#include "flutter/shell/platform/linux/vsync_predictor.h"

namespace flutter {

class FlutterLinuxView;

// The view ID of the view that exists for the lifetime of the engine.
constexpr FlutterViewId kImplicitViewId = 0;

// An instance of a Flutter engine, and the platform resources it uses.
//
// The engine must be created, run and destroyed on the application's platform
// thread. Multiple engines may share an application.
class FlutterLinuxEngine : public PlatformMessenger {
 public:
  struct Properties {
    std::filesystem::path assets_path;
    std::filesystem::path icu_data_path;
    std::filesystem::path aot_library_path;
    std::string dart_entrypoint;
    std::vector<std::string> dart_entrypoint_arguments;
  };

  // Creates an engine in |application|, which must outlive the engine.
  // Returns nullptr on failure.
  static std::unique_ptr<FlutterLinuxEngine> Create(
      FlutterLinuxApplication* application,
      Properties properties);

  ~FlutterLinuxEngine() override;

  // Starts running the engine.
  bool Run();

  // Returns true if the engine is running.
  bool running() const { return engine_ != nullptr; }

  // Stops the engine. Returns true if the engine was stopped or was not
  // running.
  bool Stop();

  FlutterLinuxApplication* application() const { return application_; }

  // Returns true if Impeller has been enabled with a command line switch.
  bool IsImpellerEnabled() const;

  // The EGL contexts the engine renders with.
  EGLManager* egl() const { return egl_.get(); }

  // Presents the frames the engine renders.
  CompositorOpenGL* compositor() const { return compositor_.get(); }

  // Creates the view shown in a new window of the given logical size.
  //
  // Only the implicit view is currently supported, so this can only be
  // called once.
  std::unique_ptr<FlutterLinuxView> CreateView(int width, int height);

  // Detaches |view| from the engine before it is destroyed.
  void RemoveView(FlutterLinuxView* view);

  // Informs the engine of the size of a view.
  void SendWindowMetrics(const FlutterWindowMetricsEvent& event);

  // Sends pointer events to the engine.
  void SendPointerEvents(const FlutterPointerEvent* events, size_t count);

  // Sends a key event to the engine. |callback| is called with whether the
  // framework handled it, and may be empty.
  void SendKeyEvent(const FlutterKeyEvent& event,
                    std::function<void(bool handled)> callback);

  // Handles keyboard input for the engine's views.
  KeyboardManager* keyboard_manager() const { return keyboard_manager_.get(); }

  // |PlatformMessenger|
  void SendMessage(const std::string& channel,
                   const std::vector<uint8_t>& message,
                   ReplyCallback reply) override;

  // |PlatformMessenger|
  void SetMessageHandler(const std::string& channel,
                         MessageHandler handler) override;

  // Called when the user requests a view be closed.
  void OnViewClose(FlutterLinuxView* view);

  // Called when a frame from any view has been shown on the screen. Times
  // are in nanoseconds on CLOCK_MONOTONIC. |refresh_ns| is zero if unknown.
  void OnFramePresented(uint64_t presentation_time_ns, uint64_t refresh_ns);

  // Called when the compositor is ready for another frame from any view.
  void OnFrameDone();

 private:
  FlutterLinuxEngine(FlutterLinuxApplication* application,
                     Properties properties);

  bool Initialize();

  // Posts a Flutter task to be run on the platform thread.
  void PostFlutterTask(FlutterTask task, uint64_t target_time_nanos);

  // Responds to a request from the engine for the next vsync.
  void OnVsyncRequest(intptr_t baton);

  // True if no view can show a new frame until the compositor has handled
  // the frames already presented.
  bool IsThrottled() const;

  // Responds to a message from the framework.
  void HandlePlatformMessage(const FlutterPlatformMessage* message);

  FlutterRendererConfig GetRendererConfig();

  FlutterLinuxApplication* application_;
  Properties properties_;
  std::vector<std::string> switches_;

  FlutterEngineProcTable embedder_api_ = {};
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;
  FlutterEngineAOTData aot_data_ = nullptr;

  std::unique_ptr<EGLManager> egl_;
  std::unique_ptr<CompositorOpenGL> compositor_;

  // Predicts when to draw frames, from when previous frames were shown.
  VsyncPredictor vsync_predictor_;

  // A vsync request waiting for the compositor to be ready for a new frame.
  std::optional<intptr_t> pending_vsync_baton_;

  // Handlers for messages from the framework, by channel.
  std::map<std::string, MessageHandler> message_handlers_;

  std::unique_ptr<KeyboardManager> keyboard_manager_;

  // The views shown by this engine. Owned by their view controllers.
  std::map<FlutterViewId, FlutterLinuxView*> views_;

  // Tasks posted to the shared event loop hold a weak reference to this, so
  // they don't run after the engine is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_ENGINE_H_
