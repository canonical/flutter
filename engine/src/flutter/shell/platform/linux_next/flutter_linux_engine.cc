// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/flutter_linux_engine.h"

#include <chrono>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/engine_switches.h"
#include "flutter/shell/platform/linux_next/flutter_linux_view.h"
#include "flutter/shell/platform/linux_next/software_compositor.h"

namespace flutter {

namespace {

// Converts a Flutter task target time to an event loop time.
EventLoop::TimePoint ToEventLoopTime(uint64_t target_time_nanos,
                                     uint64_t engine_now_nanos) {
  auto delay = std::chrono::nanoseconds(0);
  if (target_time_nanos > engine_now_nanos) {
    delay = std::chrono::nanoseconds(target_time_nanos - engine_now_nanos);
  }
  return EventLoop::Clock::now() +
         std::chrono::duration_cast<EventLoop::Clock::duration>(delay);
}

}  // namespace

FlutterLinuxEngine::FlutterLinuxEngine(FlutterLinuxApplication* application,
                                       Properties properties)
    : application_(application),
      properties_(std::move(properties)),
      compositor_(std::make_unique<SoftwareCompositor>()) {
  embedder_api_.struct_size = sizeof(FlutterEngineProcTable);
  FlutterEngineGetProcAddresses(&embedder_api_);
}

FlutterLinuxEngine::~FlutterLinuxEngine() {
  if (engine_ != nullptr) {
    embedder_api_.Shutdown(engine_);
    engine_ = nullptr;
  }
  if (aot_data_ != nullptr) {
    embedder_api_.CollectAOTData(aot_data_);
  }
}

FlutterLinuxResult FlutterLinuxEngine::Run() {
  if (running()) {
    FML_LOG(ERROR) << "Engine is already running";
    return kFlutterLinuxResultInvalidState;
  }

  if (embedder_api_.RunsAOTCompiledDartCode() && aot_data_ == nullptr) {
    std::string elf_path = properties_.aot_library_path.string();
    FlutterEngineAOTDataSource source = {};
    source.type = kFlutterEngineAOTDataSourceTypeElfPath;
    source.elf_path = elf_path.c_str();
    if (embedder_api_.CreateAOTData(&source, &aot_data_) != kSuccess) {
      FML_LOG(ERROR) << "Unable to load AOT data from " << elf_path;
      return kFlutterLinuxResultFailed;
    }
  }

  std::string assets_path = properties_.assets_path.string();
  std::string icu_data_path = properties_.icu_data_path.string();

  // The flutter tool passes engine switches (e.g. the VM service port) in the
  // environment. The first argument is treated as the executable name and
  // ignored.
  std::vector<std::string> switches = GetSwitchesFromEnvironment();
  std::vector<const char*> argv = {"flutter"};
  for (const std::string& s : switches) {
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
        ->application_->event_loop()
        ->RunsTasksOnCurrentThread();
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

  FlutterProjectArgs args = {};
  args.struct_size = sizeof(FlutterProjectArgs);
  args.shutdown_dart_vm_when_done = true;
  args.assets_path = assets_path.c_str();
  args.icu_data_path = icu_data_path.c_str();
  args.command_line_argc = static_cast<int>(argv.size());
  args.command_line_argv = argv.data();
  args.engine_id = reinterpret_cast<int64_t>(this);
  if (!properties_.dart_entrypoint.empty()) {
    args.custom_dart_entrypoint = properties_.dart_entrypoint.c_str();
  }
  args.dart_entrypoint_argc = static_cast<int>(entrypoint_argv.size());
  args.dart_entrypoint_argv =
      entrypoint_argv.empty() ? nullptr : entrypoint_argv.data();
  args.platform_message_callback = [](const FlutterPlatformMessage* message,
                                      void* user_data) {
    static_cast<FlutterLinuxEngine*>(user_data)->HandlePlatformMessage(message);
  };
  args.custom_task_runners = &custom_task_runners;
  args.aot_data = aot_data_;

  FlutterRendererConfig renderer_config = compositor_->GetRendererConfig();

  FlutterCompositor compositor = {};
  compositor_->GetFlutterCompositor(&compositor);
  args.compositor = &compositor;

  FlutterEngineResult result = embedder_api_.Run(
      FLUTTER_ENGINE_VERSION, &renderer_config, &args, this, &engine_);
  if (result != kSuccess || engine_ == nullptr) {
    FML_LOG(ERROR) << "Failed to start Flutter engine: error " << result;
    engine_ = nullptr;
    return kFlutterLinuxResultFailed;
  }

  for (const auto& [view_id, view] : views_) {
    if (view_id == kImplicitViewId) {
      view->SendWindowMetrics();
    } else {
      AddViewToEngine(view);
    }
  }

  return kFlutterLinuxResultSuccess;
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateImplicitView(
    int width,
    int height,
    FlutterLinuxResult* result) {
  if (running()) {
    FML_LOG(ERROR)
        << "The implicit view must be created before the engine runs";
    *result = kFlutterLinuxResultInvalidState;
    return nullptr;
  }
  if (implicit_view_created_) {
    FML_LOG(ERROR) << "The implicit view has already been created";
    *result = kFlutterLinuxResultInvalidState;
    return nullptr;
  }
  implicit_view_created_ = true;
  return CreateView(kImplicitViewId, width, height, result);
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateView(
    int width,
    int height,
    FlutterLinuxResult* result) {
  return CreateView(next_view_id_++, width, height, result);
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateView(
    FlutterViewId view_id,
    int width,
    int height,
    FlutterLinuxResult* result) {
  Window::Params params;
  params.width = width;
  params.height = height;
  params.application_id = application_->application_id();
  std::unique_ptr<FlutterLinuxView> view =
      FlutterLinuxView::Create(this, view_id, params);
  if (!view) {
    FML_LOG(ERROR) << "Failed to create window for view " << view_id;
    *result = kFlutterLinuxResultFailed;
    return nullptr;
  }
  *result = kFlutterLinuxResultSuccess;
  return AddView(std::move(view));
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::AddView(
    std::unique_ptr<FlutterLinuxView> view) {
  views_[view->view_id()] = view.get();
  compositor_->AddView(view->view_id(), view.get());

  if (running()) {
    if (view->view_id() == kImplicitViewId) {
      view->SendWindowMetrics();
    } else {
      AddViewToEngine(view.get());
    }
  }
  return view;
}

void FlutterLinuxEngine::AddViewToEngine(FlutterLinuxView* view) {
  FlutterWindowMetricsEvent metrics = view->GetWindowMetrics();
  FlutterAddViewInfo info = {};
  info.struct_size = sizeof(FlutterAddViewInfo);
  info.view_id = view->view_id();
  info.view_metrics = &metrics;
  info.add_view_callback = [](const FlutterAddViewResult* result) {
    if (!result->added) {
      FML_LOG(ERROR) << "Failed to add view to engine";
    }
  };
  if (embedder_api_.AddView(engine_, &info) != kSuccess) {
    FML_LOG(ERROR) << "Failed to add view " << view->view_id();
  }
}

void FlutterLinuxEngine::RemoveView(FlutterLinuxView* view) {
  // Once removed from the compositor no frames are shown in the view, so
  // there is no need to wait for the engine to finish with it.
  compositor_->RemoveView(view->view_id());
  if (running() && view->view_id() != kImplicitViewId) {
    FlutterRemoveViewInfo info = {};
    info.struct_size = sizeof(FlutterRemoveViewInfo);
    info.view_id = view->view_id();
    info.remove_view_callback = [](const FlutterRemoveViewResult* result) {};
    if (embedder_api_.RemoveView(engine_, &info) != kSuccess) {
      FML_LOG(ERROR) << "Failed to remove view " << view->view_id();
    }
  }
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
    application_->Quit();
  }
}

void FlutterLinuxEngine::PostFlutterTask(FlutterTask task,
                                         uint64_t target_time_nanos) {
  EventLoop::TimePoint target_time =
      ToEventLoopTime(target_time_nanos, embedder_api_.GetCurrentTime());
  application_->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), task]() {
        if (!alive.expired() && engine_ != nullptr) {
          embedder_api_.RunTask(engine_, &task);
        }
      },
      target_time);
}

void FlutterLinuxEngine::HandlePlatformMessage(
    const FlutterPlatformMessage* message) {
  // No channels are implemented yet. An empty response tells the framework
  // the message wasn't handled.
  if (message->response_handle == nullptr) {
    return;
  }
  embedder_api_.SendPlatformMessageResponse(engine_, message->response_handle,
                                            nullptr, 0);
}

}  // namespace flutter
