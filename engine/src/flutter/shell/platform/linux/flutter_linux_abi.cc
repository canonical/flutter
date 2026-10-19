// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux_abi.h"

#include <sys/epoll.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/path_utils.h"
#include "flutter/shell/platform/linux/compositor_opengl.h"
#include "flutter/shell/platform/linux/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/flutter_linux_application.h"
#include "flutter/shell/platform/linux/flutter_linux_engine.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/plugin_messenger.h"

namespace {

flutter::FlutterLinuxApplication* ToApplication(
    FlutterLinuxApplication* application) {
  return reinterpret_cast<flutter::FlutterLinuxApplication*>(application);
}

flutter::FlutterLinuxEngine* ToEngine(FlutterLinuxEngine* engine) {
  return reinterpret_cast<flutter::FlutterLinuxEngine*>(engine);
}

flutter::FlutterLinuxView* ToView(FlutterLinuxView* view) {
  return reinterpret_cast<flutter::FlutterLinuxView*>(view);
}

FlutterLinuxMessenger* ToMessenger(flutter::PluginMessenger* messenger) {
  return reinterpret_cast<FlutterLinuxMessenger*>(messenger);
}

flutter::PluginMessenger* FromMessenger(FlutterLinuxMessenger* messenger) {
  return reinterpret_cast<flutter::PluginMessenger*>(messenger);
}

// Calls a destroy notify function when destroyed.
class DestroyNotify {
 public:
  DestroyNotify(FlutterLinuxCallback destroy, void* user_data)
      : destroy_(destroy), user_data_(user_data) {}
  ~DestroyNotify() {
    if (destroy_ != nullptr) {
      destroy_(user_data_);
    }
  }

 private:
  FlutterLinuxCallback destroy_;
  void* user_data_;

  FML_DISALLOW_COPY_AND_ASSIGN(DestroyNotify);
};

uint32_t ToEpollEvents(uint32_t events) {
  uint32_t result = 0;
  if (events & FLUTTER_LINUX_FD_EVENT_READABLE) {
    result |= EPOLLIN;
  }
  if (events & FLUTTER_LINUX_FD_EVENT_WRITABLE) {
    result |= EPOLLOUT;
  }
  return result;
}

uint32_t FromEpollEvents(uint32_t events) {
  uint32_t result = 0;
  if (events & EPOLLIN) {
    result |= FLUTTER_LINUX_FD_EVENT_READABLE;
  }
  if (events & EPOLLOUT) {
    result |= FLUTTER_LINUX_FD_EVENT_WRITABLE;
  }
  if (events & EPOLLERR) {
    result |= FLUTTER_LINUX_FD_EVENT_ERROR;
  }
  if (events & EPOLLHUP) {
    result |= FLUTTER_LINUX_FD_EVENT_HANG_UP;
  }
  return result;
}

// Resolves |path| relative to the executable's directory.
std::filesystem::path ResolvePath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return {};
  }
  std::filesystem::path result(path);
  if (result.is_relative()) {
    result = flutter::GetExecutableDirectory() / result;
  }
  return result;
}

}  // namespace

// A response to a message, holding a reference to the messenger.
struct _FlutterLinuxMessageResponse {
  flutter::PluginMessenger* messenger;
  uint64_t id;
};

FlutterLinuxApplication* flutter_linux_application_new(void) {
  return reinterpret_cast<FlutterLinuxApplication*>(
      flutter::FlutterLinuxApplication::Create().release());
}

void flutter_linux_application_free(FlutterLinuxApplication* application) {
  delete ToApplication(application);
}

void flutter_linux_application_run(FlutterLinuxApplication* application) {
  ToApplication(application)->Run();
}

void flutter_linux_application_quit(FlutterLinuxApplication* application) {
  ToApplication(application)->Quit();
}

wl_display* flutter_linux_application_get_wl_display(
    FlutterLinuxApplication* application) {
  return ToApplication(application)->display()->display();
}

size_t flutter_linux_application_get_wayland_protocol_count(
    FlutterLinuxApplication* application) {
  return ToApplication(application)->display()->protocols().size();
}

void flutter_linux_application_get_wayland_protocol(
    FlutterLinuxApplication* application,
    size_t index,
    const char** name,
    uint32_t* version) {
  const flutter::WaylandDisplay::Protocol& protocol =
      ToApplication(application)->display()->protocols().at(index);
  *name = protocol.name;
  *version = protocol.version;
}

FlutterLinuxEngine* flutter_linux_engine_new(
    FlutterLinuxApplication* application,
    const FlutterLinuxEngineProperties* properties) {
  flutter::FlutterLinuxEngine::Properties engine_properties;
  engine_properties.assets_path = ResolvePath(properties->assets_path);
  engine_properties.icu_data_path = ResolvePath(properties->icu_data_path);
  engine_properties.aot_library_path =
      ResolvePath(properties->aot_library_path);
  if (properties->dart_entrypoint != nullptr) {
    engine_properties.dart_entrypoint = properties->dart_entrypoint;
  }
  for (int i = 0; i < properties->dart_entrypoint_argc; i++) {
    engine_properties.dart_entrypoint_arguments.push_back(
        properties->dart_entrypoint_argv[i]);
  }

  return reinterpret_cast<FlutterLinuxEngine*>(
      flutter::FlutterLinuxEngine::Create(ToApplication(application),
                                          std::move(engine_properties))
          .release());
}

void flutter_linux_engine_free(FlutterLinuxEngine* engine) {
  delete ToEngine(engine);
}

bool flutter_linux_engine_run(FlutterLinuxEngine* engine) {
  return ToEngine(engine)->Run();
}

bool flutter_linux_engine_is_running(FlutterLinuxEngine* engine) {
  return ToEngine(engine)->running();
}

void flutter_linux_engine_get_rendering_info(FlutterLinuxEngine* engine,
                                             FlutterLinuxRenderingInfo* info) {
  flutter::FlutterLinuxEngine* e = ToEngine(engine);
  flutter::EGLManager* egl = e->egl();
  flutter::WaylandDisplay* display = e->application()->display();

  FlutterLinuxRenderingInfo result = {};
  result.struct_size = sizeof(FlutterLinuxRenderingInfo);
  result.renderer = e->IsImpellerEnabled() ? FLUTTER_LINUX_RENDERER_IMPELLER
                                           : FLUTTER_LINUX_RENDERER_SKIA;
  result.graphics_api = FLUTTER_LINUX_GRAPHICS_API_OPENGL_ES;
  result.gpu_device = egl->gpu()->path().c_str();
  result.gl_vendor = egl->gl_vendor().c_str();
  result.gl_renderer = egl->gl_renderer().c_str();
  result.gl_version = egl->gl_version().c_str();
  result.buffer_sharing = e->compositor()->uses_dmabuf()
                              ? FLUTTER_LINUX_BUFFER_SHARING_LINUX_DMABUF
                              : FLUTTER_LINUX_BUFFER_SHARING_WL_SHM;
  result.buffer_format = flutter::DmabufBuffer::kFormat;
  result.buffer_modifier = e->compositor()->buffer_modifier();
  result.buffer_synchronization =
      e->compositor()->explicit_sync()
          ? FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_EXPLICIT
          : FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_IMPLICIT;
  result.presentation_feedback = display->presentation() != nullptr;
  result.fifo = display->fifo_manager() != nullptr;
  result.fractional_scale = display->viewporter() != nullptr &&
                            display->fractional_scale_manager() != nullptr;

  if (info->struct_size < sizeof(info->struct_size)) {
    return;
  }
  size_t size = std::min(info->struct_size, sizeof(result));
  memcpy(info, &result, size);
  info->struct_size = size;
}

FlutterLinuxView* flutter_linux_view_new(FlutterLinuxEngine* engine,
                                         int width,
                                         int height) {
  return reinterpret_cast<FlutterLinuxView*>(
      ToEngine(engine)->CreateView(width, height).release());
}

void flutter_linux_view_free(FlutterLinuxView* view_handle) {
  flutter::FlutterLinuxView* view = ToView(view_handle);
  // Ensure the raster thread is no longer presenting to the view before it is
  // destroyed.
  view->engine()->RemoveView(view);
  delete view;
}

int64_t flutter_linux_view_get_id(FlutterLinuxView* view) {
  return ToView(view)->view_id();
}

void flutter_linux_view_set_title(FlutterLinuxView* view, const char* title) {
  ToView(view)->window()->SetTitle(title);
}

void flutter_linux_view_set_application_id(FlutterLinuxView* view,
                                           const char* application_id) {
  ToView(view)->window()->SetApplicationId(application_id);
}

void flutter_linux_view_set_close_callback(
    FlutterLinuxView* view,
    FlutterLinuxViewCloseCallback callback,
    void* user_data) {
  if (callback == nullptr) {
    ToView(view)->SetCloseCallback(nullptr);
    return;
  }
  ToView(view)->SetCloseCallback(
      [callback, user_data]() { callback(user_data); });
}

FlutterLinuxMessenger* flutter_linux_engine_get_messenger(
    FlutterLinuxEngine* engine) {
  return ToMessenger(ToEngine(engine)->plugin_messenger());
}

void flutter_linux_messenger_send(FlutterLinuxMessenger* messenger,
                                  const char* channel,
                                  const uint8_t* data,
                                  size_t size,
                                  FlutterLinuxReplyCallback reply,
                                  void* user_data) {
  flutter::PluginMessenger::ReplyCallback reply_callback;
  if (reply != nullptr) {
    reply_callback = [reply, user_data](const uint8_t* data, size_t size) {
      reply(data, size, user_data);
    };
  }
  FromMessenger(messenger)->Send(channel,
                                 std::vector<uint8_t>(data, data + size),
                                 std::move(reply_callback));
}

void flutter_linux_messenger_set_handler(FlutterLinuxMessenger* messenger,
                                         const char* channel,
                                         FlutterLinuxMessageHandler handler,
                                         void* user_data,
                                         FlutterLinuxCallback destroy) {
  flutter::PluginMessenger* plugin_messenger = FromMessenger(messenger);
  if (handler == nullptr) {
    plugin_messenger->SetMessageHandler(channel, nullptr);
    DestroyNotify notify(destroy, user_data);
    return;
  }
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  plugin_messenger->SetMessageHandler(
      channel,
      [plugin_messenger, handler, user_data, notify = std::move(notify)](
          const uint8_t* data, size_t size, uint64_t response_id) {
        auto* response = new FlutterLinuxMessageResponse{
            plugin_messenger->AddRef(), response_id};
        handler(data, size, response, user_data);
      });
}

void flutter_linux_message_response_send(FlutterLinuxMessageResponse* response,
                                         const uint8_t* data,
                                         size_t size) {
  std::vector<uint8_t> message;
  if (data != nullptr) {
    message.assign(data, data + size);
  }
  response->messenger->SendResponse(response->id, std::move(message));
  response->messenger->Release();
  delete response;
}

void flutter_linux_engine_post_task(FlutterLinuxEngine* engine,
                                    uint64_t delay_ns,
                                    FlutterLinuxCallback callback,
                                    FlutterLinuxCallback destroy,
                                    void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  // Clamped so the target time doesn't overflow.
  constexpr uint64_t kMaxDelayNs = std::numeric_limits<int64_t>::max() / 2;
  ToEngine(engine)->plugin_event_loop()->PostTask(
      [notify = std::move(notify), callback, user_data]() {
        callback(user_data);
      },
      std::chrono::nanoseconds(std::min(delay_ns, kMaxDelayNs)));
}

uint64_t flutter_linux_engine_add_fd_watch(FlutterLinuxEngine* engine,
                                           int fd,
                                           uint32_t events,
                                           FlutterLinuxFdCallback callback,
                                           FlutterLinuxCallback destroy,
                                           void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  return ToEngine(engine)->plugin_event_loop()->AddWatch(
      fd, ToEpollEvents(events),
      [notify = std::move(notify), fd, callback, user_data](uint32_t events) {
        callback(fd, FromEpollEvents(events), user_data);
      });
}

bool flutter_linux_engine_set_fd_watch_events(FlutterLinuxEngine* engine,
                                              uint64_t watch_id,
                                              uint32_t events) {
  return ToEngine(engine)->plugin_event_loop()->SetWatchEvents(
      watch_id, ToEpollEvents(events));
}

void flutter_linux_engine_remove_fd_watch(FlutterLinuxEngine* engine,
                                          uint64_t watch_id) {
  ToEngine(engine)->plugin_event_loop()->RemoveWatch(watch_id);
}

wl_surface* flutter_linux_view_get_wl_surface(FlutterLinuxView* view) {
  return ToView(view)->window()->surface();
}

void flutter_linux_view_get_size(FlutterLinuxView* view,
                                 int* width,
                                 int* height) {
  flutter::WaylandWindow* window = ToView(view)->window();
  if (width != nullptr) {
    *width = window->width();
  }
  if (height != nullptr) {
    *height = window->height();
  }
}

double flutter_linux_view_get_scale(FlutterLinuxView* view) {
  return ToView(view)->window()->scale();
}
