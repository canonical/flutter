// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/flutter_linux_engine.h"

#include <chrono>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/engine_switches.h"

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
    : application_(application), properties_(std::move(properties)) {
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

bool FlutterLinuxEngine::Run() {
  if (running()) {
    FML_LOG(ERROR) << "Engine is already running";
    return false;
  }

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

  // There are no views to draw to yet, so frames are discarded.
  FlutterRendererConfig renderer_config = {};
  renderer_config.type = kSoftware;
  renderer_config.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
  renderer_config.software.surface_present_callback =
      [](void* user_data, const void* allocation, size_t row_bytes,
         size_t height) { return true; };

  FlutterEngineResult result = embedder_api_.Run(
      FLUTTER_ENGINE_VERSION, &renderer_config, &args, this, &engine_);
  if (result != kSuccess || engine_ == nullptr) {
    FML_LOG(ERROR) << "Failed to start Flutter engine: error " << result;
    engine_ = nullptr;
    return false;
  }

  return true;
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
