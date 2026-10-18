// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_engine.h"

#include <algorithm>
#include <chrono>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/engine_switches.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"

namespace flutter {

namespace {

// Converts a Flutter task target time to an event loop time.
EventLoop::TimePoint ToEventLoopTime(uint64_t target_time_nanos,
                                     uint64_t engine_now_nanos) {
  auto delay = std::chrono::nanoseconds(
      static_cast<int64_t>(target_time_nanos - engine_now_nanos));
  if (target_time_nanos <= engine_now_nanos) {
    delay = std::chrono::nanoseconds(0);
  }
  return EventLoop::Clock::now() +
         std::chrono::duration_cast<EventLoop::Clock::duration>(delay);
}

}  // namespace

std::unique_ptr<FlutterLinuxEngine> FlutterLinuxEngine::Create(
    Properties properties) {
  std::unique_ptr<FlutterLinuxEngine> engine(
      new FlutterLinuxEngine(std::move(properties)));
  if (!engine->Initialize()) {
    return nullptr;
  }
  return engine;
}

FlutterLinuxEngine::FlutterLinuxEngine(Properties properties)
    : properties_(std::move(properties)),
      switches_(GetSwitchesFromEnvironment()) {
  embedder_api_.struct_size = sizeof(FlutterEngineProcTable);
  FlutterEngineGetProcAddresses(&embedder_api_);
}

FlutterLinuxEngine::~FlutterLinuxEngine() {
  Stop();
  FML_DCHECK(views_.empty());

  compositor_.reset();
  egl_.reset();
  if (display_) {
    event_loop_->RemoveSource(display_.get());
    display_.reset();
  }
  event_loop_.reset();

  if (aot_data_ != nullptr) {
    embedder_api_.CollectAOTData(aot_data_);
  }
}

bool FlutterLinuxEngine::Initialize() {
  event_loop_ = std::make_unique<EventLoop>();

  display_ = WaylandDisplay::Create();
  if (!display_) {
    return false;
  }
  display_->SetErrorCallback([this]() { QuitEventLoop(); });
  event_loop_->AddSource(display_.get());

  egl_ = EGLManager::Create(display_->main_device());
  if (!egl_) {
    return false;
  }

  compositor_ = std::make_unique<CompositorOpenGL>(egl_.get(), display_.get(),
                                                   IsImpellerEnabled());
  return true;
}

bool FlutterLinuxEngine::IsImpellerEnabled() const {
  return std::any_of(
      switches_.begin(), switches_.end(), [](const std::string& s) {
        return s == "--enable-impeller" || s == "--enable-impeller=true";
      });
}

bool FlutterLinuxEngine::Run(std::string_view entrypoint) {
  if (running()) {
    FML_LOG(ERROR) << "Engine is already running";
    return false;
  }

  if (!properties_.dart_entrypoint.empty() && !entrypoint.empty() &&
      properties_.dart_entrypoint != entrypoint) {
    FML_LOG(ERROR) << "Conflicting entrypoints were specified in "
                      "FlutterDesktopEngineProperties.dart_entrypoint and "
                      "FlutterDesktopEngineRun(engine, entry_point).";
    return false;
  }
  std::string custom_entrypoint(entrypoint.empty() ? properties_.dart_entrypoint
                                                   : entrypoint);

  if (embedder_api_.RunsAOTCompiledDartCode() && aot_data_ == nullptr) {
    std::string elf_path = properties_.aot_library_path.string();
    FlutterEngineAOTDataSource source = {};
    source.type = kFlutterEngineAOTDataSourceTypeElfPath;
    source.elf_path = elf_path.c_str();
    if (embedder_api_.CreateAOTData(&source, &aot_data_) != kSuccess) {
      FML_LOG(ERROR) << "Unable to load AOT data from " << elf_path;
      return false;
    }
  }

  std::string assets_path = properties_.assets_path.string();
  std::string icu_data_path = properties_.icu_data_path.string();

  // The first argument is treated as the executable name and ignored.
  std::vector<const char*> argv = {"flutter"};
  for (const std::string& s : switches_) {
    argv.push_back(s.c_str());
  }
  std::vector<const char*> entrypoint_argv;
  for (const std::string& s : properties_.dart_entrypoint_arguments) {
    entrypoint_argv.push_back(s.c_str());
  }

  // The UI and platform task runners are merged, so Dart code runs on the
  // platform thread.
  FlutterTaskRunnerDescription platform_task_runner = {};
  platform_task_runner.struct_size = sizeof(FlutterTaskRunnerDescription);
  platform_task_runner.user_data = this;
  platform_task_runner.runs_task_on_current_thread_callback =
      [](void* user_data) -> bool {
    return static_cast<FlutterLinuxEngine*>(user_data)
        ->event_loop_->RunsTasksOnCurrentThread();
  };
  platform_task_runner.post_task_callback =
      [](FlutterTask task, uint64_t target_time_nanos, void* user_data) {
        static_cast<FlutterLinuxEngine*>(user_data)->PostFlutterTask(
            task, target_time_nanos);
      };
  FlutterCustomTaskRunners custom_task_runners = {};
  custom_task_runners.struct_size = sizeof(FlutterCustomTaskRunners);
  custom_task_runners.platform_task_runner = &platform_task_runner;
  custom_task_runners.ui_task_runner = &platform_task_runner;

  FlutterCompositor compositor = {};
  compositor.struct_size = sizeof(FlutterCompositor);
  compositor.user_data = compositor_.get();
  compositor.create_backing_store_callback =
      [](const FlutterBackingStoreConfig* config,
         FlutterBackingStore* backing_store_out, void* user_data) -> bool {
    return static_cast<CompositorOpenGL*>(user_data)->CreateBackingStore(
        *config, backing_store_out);
  };
  compositor.collect_backing_store_callback =
      [](const FlutterBackingStore* backing_store, void* user_data) -> bool {
    return static_cast<CompositorOpenGL*>(user_data)->CollectBackingStore(
        backing_store);
  };
  compositor.present_view_callback =
      [](const FlutterPresentViewInfo* info) -> bool {
    return static_cast<CompositorOpenGL*>(info->user_data)
        ->Present(info->view_id, info->layers, info->layers_count);
  };
  // Buffers are pooled by the compositor, which knows when the display
  // server has finished with them.
  compositor.avoid_backing_store_cache = true;

  FlutterProjectArgs args = {};
  args.struct_size = sizeof(FlutterProjectArgs);
  args.shutdown_dart_vm_when_done = true;
  args.assets_path = assets_path.c_str();
  args.icu_data_path = icu_data_path.c_str();
  args.command_line_argc = static_cast<int>(argv.size());
  args.command_line_argv = argv.data();
  args.engine_id = reinterpret_cast<int64_t>(this);
  if (!custom_entrypoint.empty()) {
    args.custom_dart_entrypoint = custom_entrypoint.c_str();
  }
  args.dart_entrypoint_argc = static_cast<int>(entrypoint_argv.size());
  args.dart_entrypoint_argv =
      entrypoint_argv.empty() ? nullptr : entrypoint_argv.data();
  args.platform_message_callback = [](const FlutterPlatformMessage* message,
                                      void* user_data) {
    static_cast<FlutterLinuxEngine*>(user_data)->HandlePlatformMessage(message);
  };
  args.custom_task_runners = &custom_task_runners;
  args.compositor = &compositor;
  args.aot_data = aot_data_;

  FlutterRendererConfig renderer_config = GetRendererConfig();
  FlutterEngineResult result = embedder_api_.Run(
      FLUTTER_ENGINE_VERSION, &renderer_config, &args, this, &engine_);
  if (result != kSuccess || engine_ == nullptr) {
    FML_LOG(ERROR) << "Failed to start Flutter engine: error " << result;
    engine_ = nullptr;
    return false;
  }

  for (const auto& [view_id, view] : views_) {
    view->SendWindowMetrics();
  }

  return true;
}

bool FlutterLinuxEngine::Stop() {
  if (engine_ == nullptr) {
    return true;
  }
  FlutterEngineResult result = embedder_api_.Shutdown(engine_);
  engine_ = nullptr;
  return result == kSuccess;
}

void FlutterLinuxEngine::RunEventLoop() {
  event_loop_->Run();
}

void FlutterLinuxEngine::QuitEventLoop() {
  event_loop_->Quit();
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateView(int width,
                                                                 int height) {
  if (views_.count(kImplicitViewId) != 0) {
    FML_LOG(ERROR) << "Only a single view is currently supported";
    return nullptr;
  }

  std::unique_ptr<FlutterLinuxView> view =
      FlutterLinuxView::Create(this, kImplicitViewId, width, height);
  if (!view) {
    return nullptr;
  }
  views_[view->view_id()] = view.get();
  compositor_->AddView(view->view_id(), view.get());

  if (running()) {
    view->SendWindowMetrics();
  }
  return view;
}

void FlutterLinuxEngine::RemoveView(FlutterLinuxView* view) {
  compositor_->RemoveView(view->view_id());
  views_.erase(view->view_id());
}

void FlutterLinuxEngine::SendWindowMetrics(
    const FlutterWindowMetricsEvent& event) {
  if (engine_ == nullptr) {
    return;
  }
  embedder_api_.SendWindowMetricsEvent(engine_, &event);
}

void FlutterLinuxEngine::OnViewClose(FlutterLinuxView* view) {
  // Closing the implicit view ends the application.
  if (view->view_id() == kImplicitViewId) {
    QuitEventLoop();
  }
}

void FlutterLinuxEngine::PostFlutterTask(FlutterTask task,
                                         uint64_t target_time_nanos) {
  EventLoop::TimePoint target_time =
      ToEventLoopTime(target_time_nanos, embedder_api_.GetCurrentTime());
  event_loop_->PostTask(
      [this, task]() {
        if (engine_ != nullptr) {
          embedder_api_.RunTask(engine_, &task);
        }
      },
      target_time);
}

void FlutterLinuxEngine::HandlePlatformMessage(
    const FlutterPlatformMessage* message) {
  // No platform channels are implemented yet. Send an empty response so the
  // framework knows the message was not handled.
  if (message->response_handle != nullptr) {
    embedder_api_.SendPlatformMessageResponse(engine_, message->response_handle,
                                              nullptr, 0);
  }
}

FlutterRendererConfig FlutterLinuxEngine::GetRendererConfig() {
  FlutterRendererConfig config = {};
  config.type = kOpenGL;
  config.open_gl.struct_size = sizeof(FlutterOpenGLRendererConfig);
  config.open_gl.make_current = [](void* user_data) -> bool {
    return static_cast<FlutterLinuxEngine*>(user_data)
        ->egl_->MakeRenderContextCurrent();
  };
  config.open_gl.clear_current = [](void* user_data) -> bool {
    return static_cast<FlutterLinuxEngine*>(user_data)->egl_->ClearCurrent();
  };
  // Frames are presented by the compositor, so these are never used.
  config.open_gl.present = [](void* user_data) -> bool { return true; };
  config.open_gl.fbo_callback = [](void* user_data) -> uint32_t { return 0; };
  config.open_gl.make_resource_current = [](void* user_data) -> bool {
    return static_cast<FlutterLinuxEngine*>(user_data)
        ->egl_->MakeResourceContextCurrent();
  };
  config.open_gl.gl_proc_resolver = [](void* user_data,
                                       const char* name) -> void* {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
  };
  return config;
}

}  // namespace flutter
