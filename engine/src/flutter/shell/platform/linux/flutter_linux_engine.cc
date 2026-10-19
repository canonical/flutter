// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>

#include "flutter/fml/icu_util.h"
#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "flutter/shell/platform/common/engine_switches.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"
#include "flutter/shell/platform/linux/window_manager.h"

namespace flutter {

namespace {

// The number of frames each view may have waiting for the compositor before
// drawing stops. Allowing more than one causes frames to be discarded on
// compositors that refresh slower than they report.
constexpr size_t kMaxFramesInFlight = 1;

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

// The engines that exist, so IDs from the framework can be checked.
std::mutex& EnginesMutex() {
  static std::mutex mutex;
  return mutex;
}

std::set<FlutterLinuxEngine*>& Engines() {
  static std::set<FlutterLinuxEngine*> engines;
  return engines;
}

}  // namespace

FlutterLinuxEngine* FlutterLinuxEngine::FromEngineId(int64_t engine_id) {
  auto* engine = reinterpret_cast<FlutterLinuxEngine*>(engine_id);
  std::scoped_lock lock(EnginesMutex());
  return Engines().count(engine) != 0 ? engine : nullptr;
}

std::unique_ptr<FlutterLinuxEngine> FlutterLinuxEngine::Create(
    FlutterLinuxApplication* application,
    Properties properties) {
  std::unique_ptr<FlutterLinuxEngine> engine(
      new FlutterLinuxEngine(application, std::move(properties)));
  if (!engine->Initialize()) {
    return nullptr;
  }
  return engine;
}

FlutterLinuxEngine::FlutterLinuxEngine(FlutterLinuxApplication* application,
                                       Properties properties)
    : application_(application),
      properties_(std::move(properties)),
      switches_(GetSwitchesFromEnvironment()) {
  embedder_api_.struct_size = sizeof(FlutterEngineProcTable);
  FlutterEngineGetProcAddresses(&embedder_api_);

  keyboard_manager_ = std::make_unique<KeyboardManager>(
      this, [this](const FlutterKeyEvent& event,
                   std::function<void(bool handled)> callback) {
        SendKeyEvent(event, std::move(callback));
      });
  mouse_cursor_plugin_ = std::make_unique<MouseCursorPlugin>(
      this, [this](uint32_t shape) { OnCursorChanged(shape); });
  plugin_messenger_ = new PluginMessenger(this, application_->event_loop());
  plugin_event_loop_ =
      std::make_unique<ScopedEventLoop>(application_->event_loop());
  window_manager_ = std::make_unique<WindowManager>(this);

  application_->atspi()->AddObserver(this);
  application_->display()->AddOutputObserver(this);
  SetMessageHandler(
      "flutter/accessibility",
      [this](const uint8_t* data, size_t size, ResponseCallback respond) {
        HandleAccessibilityMessage(data, size);
        // The channel has no responses, so send null.
        respond(*StandardMessageCodec::GetInstance().EncodeMessage(
            EncodableValue()));
      });

  std::scoped_lock lock(EnginesMutex());
  Engines().insert(this);
}

FlutterLinuxEngine::~FlutterLinuxEngine() {
  {
    std::scoped_lock lock(EnginesMutex());
    Engines().erase(this);
  }
  application_->atspi()->RemoveObserver(this);
  application_->display()->RemoveOutputObserver(this);

  window_manager_.reset();
  FML_DCHECK(views_.empty());

  // Plugins (which are destroyed before the engine) may have left tasks,
  // watches and message handlers.
  plugin_event_loop_.reset();
  plugin_messenger_->Detach();
  plugin_messenger_->Release();

  Stop();

  mouse_cursor_plugin_.reset();
  keyboard_manager_.reset();

  compositor_.reset();
  egl_.reset();

  if (aot_data_ != nullptr) {
    embedder_api_.CollectAOTData(aot_data_);
  }
}

bool FlutterLinuxEngine::Initialize() {
  // Window titles are laid out with ICU, and may be drawn before the engine
  // runs. The engine skips its own initialization if this has been done.
  std::error_code error;
  if (std::filesystem::is_regular_file(properties_.icu_data_path, error)) {
    fml::icu::InitializeICU(properties_.icu_data_path.string());
  }

  egl_ = EGLManager::Create(application_->gpu());
  if (!egl_) {
    return false;
  }

  compositor_ = std::make_unique<CompositorOpenGL>(
      egl_.get(), application_->display(), IsImpellerEnabled());
  return true;
}

bool FlutterLinuxEngine::IsImpellerEnabled() const {
  // Impeller is the default, but can be disabled while Skia remains.
  return std::none_of(
      switches_.begin(), switches_.end(),
      [](const std::string& s) { return s == "--enable-impeller=false"; });
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

  // The first argument is treated as the executable name and ignored.
  std::vector<const char*> argv = {"flutter"};
  for (const std::string& s : switches_) {
    argv.push_back(s.c_str());
  }
  if (IsImpellerEnabled()) {
    argv.push_back("--enable-impeller");
    // Desktop platforms draw shapes with signed distance fields.
    argv.push_back("--impeller-use-sdfs");
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
  // Frames are scheduled using the presentation times reported by the
  // compositor, and throttled by its frame callbacks.
  args.vsync_callback = [](void* user_data, intptr_t baton) {
    static_cast<FlutterLinuxEngine*>(user_data)->OnVsyncRequest(baton);
  };
  args.update_semantics_callback2 = [](const FlutterSemanticsUpdate2* update,
                                       void* user_data) {
    static_cast<FlutterLinuxEngine*>(user_data)->OnSemanticsUpdate(update);
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

  // Views refer to the displays, so they must be known first.
  SendDisplays();
  for (const auto& [view_id, view] : views_) {
    if (view_id == kImplicitViewId) {
      view->SendWindowMetrics();
    } else {
      AddViewToEngine(view);
    }
  }

  if (application_->atspi()->enabled()) {
    embedder_api_.UpdateSemanticsEnabled(engine_, true);
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

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateView(int width,
                                                                 int height) {
  // The framework expects the implicit view to exist from when it starts, so
  // it can only be created before the engine runs.
  FlutterViewId view_id = kImplicitViewId;
  if (running() || implicit_view_created_) {
    view_id = next_view_id_++;
  }
  implicit_view_created_ = true;

  WaylandWindow::Params params;
  params.width = width;
  params.height = height;
  params.application_id = application_->application_id();
  return AddView(FlutterLinuxView::Create(
      this, view_id, params, FlutterLinuxView::Sizing::kWindow, {}));
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::CreateView(
    const WaylandWindow::Params& params,
    FlutterLinuxView::Sizing sizing,
    const FlutterLinuxView::Constraints& constraints) {
  WaylandWindow::Params window_params = params;
  if (window_params.application_id.empty()) {
    window_params.application_id = application_->application_id();
  }
  return AddView(FlutterLinuxView::Create(this, next_view_id_++, window_params,
                                          sizing, constraints));
}

std::unique_ptr<FlutterLinuxView> FlutterLinuxEngine::AddView(
    std::unique_ptr<FlutterLinuxView> view) {
  if (!view) {
    return nullptr;
  }
  views_[view->view_id()] = view.get();
  compositor_->AddView(view->view_id(), view.get());
  application_->OnViewAdded();

  if (running()) {
    if (view->view_id() == kImplicitViewId) {
      view->SendWindowMetrics();
    } else {
      AddViewToEngine(view.get());
    }
  }
  return view;
}

void FlutterLinuxEngine::OnOutputsChanged() {
  SendDisplays();
}

void FlutterLinuxEngine::SendDisplays() {
  if (!running()) {
    return;
  }

  std::vector<FlutterEngineDisplay> displays;
  for (const auto& [id, output] : application_->display()->outputs()) {
    if (!output->ready()) {
      continue;
    }
    FlutterEngineDisplay display = {};
    display.struct_size = sizeof(FlutterEngineDisplay);
    display.display_id = id;
    display.refresh_rate = output->refresh_rate();
    display.width = output->width();
    display.height = output->height();
    display.device_pixel_ratio = output->pixel_ratio();
    displays.push_back(display);
  }
  if (displays.size() == 1) {
    displays[0].single_display = true;
  }
  // There is only one update type, which replaces all the displays.
  embedder_api_.NotifyDisplayUpdate(engine_,
                                    kFlutterEngineDisplaysUpdateTypeStartup,
                                    displays.data(), displays.size());
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

FlutterLinuxView* FlutterLinuxEngine::FindViewForWindow(
    const WaylandWindow* window) const {
  if (window == nullptr) {
    return nullptr;
  }
  for (const auto& [id, view] : views_) {
    if (view->window() == window) {
      return view;
    }
  }
  return nullptr;
}

void FlutterLinuxEngine::RemoveView(FlutterLinuxView* view) {
  // Stop presenting to the view before the engine is told it is gone. Once
  // removed from the compositor no frames are shown in the view, so there is
  // no need to wait for the engine to finish with it. The compositor waits
  // for any frame being presented, which mustn't wait for this thread.
  view->window()->StopWaiting();
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
  application_->OnViewRemoved();

  // The removed view may have been the reason drawing was throttled.
  OnFrameDone();
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

void FlutterLinuxEngine::OnFramePresented(uint64_t presentation_time_ns,
                                          uint64_t refresh_ns) {
  vsync_predictor_.OnFramePresented(presentation_time_ns, refresh_ns);
}

void FlutterLinuxEngine::OnFrameDone() {
  if (engine_ == nullptr || !pending_vsync_baton_.has_value() ||
      IsThrottled()) {
    return;
  }
  intptr_t baton = *pending_vsync_baton_;
  pending_vsync_baton_.reset();

  // The compositor is ready for a new frame now, so start it immediately
  // rather than waiting for the next predicted vsync.
  uint64_t now = embedder_api_.GetCurrentTime();
  embedder_api_.OnVsync(engine_, baton, now,
                        now + vsync_predictor_.refresh_interval());
}

void FlutterLinuxEngine::OnVsyncRequest(intptr_t baton) {
  // The UI and platform threads are merged, so this is on the platform
  // thread and can access the views and predictor.
  FML_DCHECK(application_->event_loop()->RunsTasksOnCurrentThread());

  uint64_t now = embedder_api_.GetCurrentTime();
  VsyncPredictor::Vsync vsync = vsync_predictor_.NextVsync(now);
  application_->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), baton, vsync]() {
        if (alive.expired() || engine_ == nullptr) {
          return;
        }
        // Wait until the compositor is ready for another frame. This stops
        // drawing while windows are hidden, and when the compositor can't
        // keep up. This is checked when the vsync occurs, as the engine
        // requests it before the previous frame has been presented.
        if (IsThrottled()) {
          FML_DCHECK(!pending_vsync_baton_.has_value());
          pending_vsync_baton_ = baton;
          return;
        }
        embedder_api_.OnVsync(engine_, baton, vsync.start_time,
                              vsync.target_time);
      },
      ToEventLoopTime(vsync.start_time, now));
}

bool FlutterLinuxEngine::IsThrottled() const {
  if (views_.empty()) {
    return false;
  }
  for (const auto& [view_id, view] : views_) {
    if (view->frames_in_flight() < kMaxFramesInFlight) {
      return false;
    }
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

void FlutterLinuxEngine::SendPointerEvents(const FlutterPointerEvent* events,
                                           size_t count) {
  if (engine_ == nullptr || count == 0) {
    return;
  }
  embedder_api_.SendPointerEvent(engine_, events, count);
}

void FlutterLinuxEngine::SendKeyEvent(
    const FlutterKeyEvent& event,
    std::function<void(bool handled)> callback) {
  if (engine_ == nullptr) {
    if (callback) {
      callback(false);
    }
    return;
  }

  // The callback is called by the engine on the platform thread.
  auto* user_data = new std::function<void(bool)>(std::move(callback));
  FlutterEngineResult result = embedder_api_.SendKeyEvent(
      engine_, &event,
      [](bool handled, void* user_data) {
        std::unique_ptr<std::function<void(bool)>> callback(
            static_cast<std::function<void(bool)>*>(user_data));
        if (*callback) {
          (*callback)(handled);
        }
      },
      user_data);
  if (result != kSuccess) {
    std::unique_ptr<std::function<void(bool)>> failed_callback(user_data);
    if (*failed_callback) {
      (*failed_callback)(false);
    }
  }
}

void FlutterLinuxEngine::SendMessage(const std::string& channel,
                                     const std::vector<uint8_t>& message,
                                     ReplyCallback reply) {
  if (engine_ == nullptr) {
    if (reply) {
      reply(nullptr, 0);
    }
    return;
  }

  FlutterPlatformMessageResponseHandle* response_handle = nullptr;
  ReplyCallback* reply_data = nullptr;
  if (reply) {
    reply_data = new ReplyCallback(std::move(reply));
    FlutterEngineResult result =
        embedder_api_.PlatformMessageCreateResponseHandle(
            engine_,
            [](const uint8_t* data, size_t size, void* user_data) {
              std::unique_ptr<ReplyCallback> reply(
                  static_cast<ReplyCallback*>(user_data));
              (*reply)(data, size);
            },
            reply_data, &response_handle);
    if (result != kSuccess) {
      std::unique_ptr<ReplyCallback> failed_reply(reply_data);
      (*failed_reply)(nullptr, 0);
      return;
    }
  }

  FlutterPlatformMessage platform_message = {};
  platform_message.struct_size = sizeof(FlutterPlatformMessage);
  platform_message.channel = channel.c_str();
  platform_message.message = message.data();
  platform_message.message_size = message.size();
  platform_message.response_handle = response_handle;
  FlutterEngineResult result =
      embedder_api_.SendPlatformMessage(engine_, &platform_message);

  if (response_handle != nullptr) {
    embedder_api_.PlatformMessageReleaseResponseHandle(engine_,
                                                       response_handle);
    if (result != kSuccess) {
      std::unique_ptr<ReplyCallback> failed_reply(reply_data);
      (*failed_reply)(nullptr, 0);
    }
  }
}

void FlutterLinuxEngine::SetMessageHandler(const std::string& channel,
                                           MessageHandler handler) {
  if (handler) {
    message_handlers_[channel] = std::move(handler);
  } else {
    message_handlers_.erase(channel);
  }
}

void FlutterLinuxEngine::OnCursorChanged(uint32_t shape) {
  WaylandSeat* seat = application_->display()->seat();
  if (seat == nullptr) {
    return;
  }
  for (const auto& [view_id, view] : views_) {
    if (seat->pointer_focus() == view->window()->surface()) {
      seat->SetCursor(shape);
      return;
    }
  }
}

void FlutterLinuxEngine::HandlePlatformMessage(
    const FlutterPlatformMessage* message) {
  // Sends the response to the message exactly once. If the handler drops
  // the response without sending it, an empty response is sent so the
  // framework knows the message was not handled.
  class Responder {
   public:
    Responder(FlutterLinuxEngine* engine,
              const FlutterPlatformMessageResponseHandle* handle)
        : engine_(engine), alive_(engine->alive_), handle_(handle) {}
    ~Responder() { Respond({}); }

    void Respond(const std::vector<uint8_t>& response) {
      if (handle_ == nullptr) {
        return;
      }
      const FlutterPlatformMessageResponseHandle* handle = handle_;
      handle_ = nullptr;
      if (alive_.expired() || engine_->engine_ == nullptr) {
        return;
      }
      engine_->embedder_api_.SendPlatformMessageResponse(
          engine_->engine_, handle, response.data(), response.size());
    }

   private:
    FlutterLinuxEngine* engine_;
    std::weak_ptr<bool> alive_;
    const FlutterPlatformMessageResponseHandle* handle_;
  };
  auto responder = std::make_shared<Responder>(this, message->response_handle);

  auto it = message_handlers_.find(message->channel);
  if (it == message_handlers_.end()) {
    return;
  }
  // Copied so the handler can replace itself.
  MessageHandler handler = it->second;
  handler(message->message, message->message_size,
          [responder](const std::vector<uint8_t>& response) {
            responder->Respond(response);
          });
}

void FlutterLinuxEngine::OnSemanticsUpdate(
    const FlutterSemanticsUpdate2* update) {
  auto it = views_.find(update->view_id);
  if (it == views_.end()) {
    return;
  }
  it->second->accessible()->Update(*update);
}

void FlutterLinuxEngine::SendSemanticsAction(FlutterViewId view_id,
                                             uint64_t node_id,
                                             FlutterSemanticsAction action,
                                             const std::vector<uint8_t>& data) {
  if (engine_ == nullptr) {
    return;
  }
  FlutterSendSemanticsActionInfo info = {};
  info.struct_size = sizeof(FlutterSendSemanticsActionInfo);
  info.view_id = view_id;
  info.node_id = node_id;
  info.action = action;
  info.data = data.empty() ? nullptr : data.data();
  info.data_length = data.size();
  embedder_api_.SendSemanticsAction(engine_, &info);
}

void FlutterLinuxEngine::OnAccessibilityEnabledChanged(bool enabled) {
  if (!enabled) {
    for (const auto& [view_id, view] : views_) {
      view->accessible()->Clear();
    }
  }
  if (engine_ != nullptr) {
    embedder_api_.UpdateSemanticsEnabled(engine_, enabled);
  }
}

// Handles messages like:
// {"type": "announce", "data": {"viewId": 0, "message": "Hello",
//                               "assertiveness": 0}}
void FlutterLinuxEngine::HandleAccessibilityMessage(const uint8_t* data,
                                                    size_t size) {
  std::unique_ptr<EncodableValue> message =
      StandardMessageCodec::GetInstance().DecodeMessage(data, size);
  const auto* map =
      message ? std::get_if<EncodableMap>(message.get()) : nullptr;
  if (map == nullptr) {
    return;
  }
  auto type = map->find(EncodableValue("type"));
  auto contents = map->find(EncodableValue("data"));
  if (type == map->end() || contents == map->end() ||
      type->second != EncodableValue("announce")) {
    return;
  }
  const auto* fields = std::get_if<EncodableMap>(&contents->second);
  if (fields == nullptr) {
    return;
  }
  auto text = fields->find(EncodableValue("message"));
  if (text == fields->end() ||
      !std::holds_alternative<std::string>(text->second)) {
    return;
  }
  FlutterViewId view_id = kImplicitViewId;
  auto view_id_value = fields->find(EncodableValue("viewId"));
  if (view_id_value != fields->end()) {
    view_id = view_id_value->second.TryGetLongValue().value_or(view_id);
  }
  bool assertive = false;
  auto assertiveness = fields->find(EncodableValue("assertiveness"));
  if (assertiveness != fields->end()) {
    assertive = assertiveness->second.TryGetLongValue().value_or(0) == 1;
  }
  auto view = views_.find(view_id);
  if (view == views_.end()) {
    return;
  }
  view->second->accessible()->Announce(std::get<std::string>(text->second),
                                       assertive);
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
