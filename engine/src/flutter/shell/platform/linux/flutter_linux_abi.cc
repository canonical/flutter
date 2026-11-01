// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux_abi.h"

#include <sys/epoll.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/path_utils.h"
#include "flutter/shell/platform/linux/compositor_opengl.h"
#include "flutter/shell/platform/linux/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/external_texture.h"
#include "flutter/shell/platform/linux/flutter_linux_application.h"
#include "flutter/shell/platform/linux/flutter_linux_engine.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/platform_view_manager.h"
#include "flutter/shell/platform/linux/plugin_messenger.h"
#include "flutter/shell/platform/linux/wayland_display.h"
#include "flutter/shell/platform/linux/wayland_window.h"
#include "flutter/shell/platform/linux/x11_display.h"
#include "flutter/shell/platform/linux/x11_window.h"

namespace {

// Returns |display| if it is a Wayland display, otherwise nullptr.
flutter::WaylandDisplay* AsWaylandDisplay(flutter::Display* display) {
  return display->backend() == flutter::Display::Backend::kWayland
             ? static_cast<flutter::WaylandDisplay*>(display)
             : nullptr;
}

flutter::FlutterLinuxApplication* ToApplication(
    FlutterLinuxApplication* application) {
  return reinterpret_cast<flutter::FlutterLinuxApplication*>(application);
}

flutter::FlutterLinuxEngine* ToEngine(FlutterLinuxEngine* engine) {
  return reinterpret_cast<flutter::FlutterLinuxEngine*>(engine);
}

flutter::LinuxPlatformView* ToPlatformView(FlutterLinuxPlatformView* view) {
  return reinterpret_cast<flutter::LinuxPlatformView*>(view);
}

flutter::LinuxPlatformView::Buffer* ToPlatformViewBuffer(
    FlutterLinuxPlatformViewBuffer* buffer) {
  return reinterpret_cast<flutter::LinuxPlatformView::Buffer*>(buffer);
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

flutter::YuvColorSpace ToYuvColorSpace(FlutterLinuxYuvColorSpace space) {
  switch (space) {
    case FLUTTER_LINUX_YUV_COLOR_SPACE_BT601:
      return flutter::YuvColorSpace::kBt601;
    case FLUTTER_LINUX_YUV_COLOR_SPACE_BT709:
      return flutter::YuvColorSpace::kBt709;
    case FLUTTER_LINUX_YUV_COLOR_SPACE_BT2020:
      return flutter::YuvColorSpace::kBt2020;
    default:
      return flutter::YuvColorSpace::kDefault;
  }
}

flutter::YuvRange ToYuvRange(FlutterLinuxYuvRange range) {
  switch (range) {
    case FLUTTER_LINUX_YUV_RANGE_NARROW:
      return flutter::YuvRange::kNarrow;
    case FLUTTER_LINUX_YUV_RANGE_FULL:
      return flutter::YuvRange::kFull;
    default:
      return flutter::YuvRange::kDefault;
  }
}

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

void flutter_linux_application_set_application_id(
    FlutterLinuxApplication* application,
    const char* application_id) {
  ToApplication(application)->SetApplicationId(application_id);
}

FlutterLinuxDisplayBackend flutter_linux_application_get_display_backend(
    FlutterLinuxApplication* application) {
  return ToApplication(application)->display()->backend() ==
                 flutter::Display::Backend::kX11
             ? FLUTTER_LINUX_DISPLAY_BACKEND_X11
             : FLUTTER_LINUX_DISPLAY_BACKEND_WAYLAND;
}

wl_display* flutter_linux_application_get_wl_display(
    FlutterLinuxApplication* application) {
  flutter::WaylandDisplay* display =
      AsWaylandDisplay(ToApplication(application)->display());
  return display != nullptr ? display->display() : nullptr;
}

xcb_connection_t* flutter_linux_application_get_xcb_connection(
    FlutterLinuxApplication* application) {
  flutter::Display* display = ToApplication(application)->display();
  if (display->backend() != flutter::Display::Backend::kX11) {
    return nullptr;
  }
  return static_cast<flutter::X11Display*>(display)->connection();
}

size_t flutter_linux_application_get_wayland_protocol_count(
    FlutterLinuxApplication* application) {
  flutter::WaylandDisplay* display =
      AsWaylandDisplay(ToApplication(application)->display());
  return display != nullptr ? display->protocols().size() : 0;
}

void flutter_linux_application_get_wayland_protocol(
    FlutterLinuxApplication* application,
    size_t index,
    const char** name,
    uint32_t* version) {
  const flutter::WaylandDisplay::Protocol& protocol =
      AsWaylandDisplay(ToApplication(application)->display())
          ->protocols()
          .at(index);
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
  flutter::Display* display = e->application()->display();
  flutter::WaylandDisplay* wayland_display = AsWaylandDisplay(display);

  FlutterLinuxRenderingInfo result = {};
  result.struct_size = sizeof(FlutterLinuxRenderingInfo);
  result.renderer = e->IsImpellerEnabled() ? FLUTTER_LINUX_RENDERER_IMPELLER
                                           : FLUTTER_LINUX_RENDERER_SKIA;
  result.graphics_api = FLUTTER_LINUX_GRAPHICS_API_OPENGL_ES;
  result.gpu_device = egl->gpu()->path().c_str();
  result.gl_vendor = egl->gl_vendor().c_str();
  result.gl_renderer = egl->gl_renderer().c_str();
  result.gl_version = egl->gl_version().c_str();
  if (wayland_display != nullptr) {
    result.buffer_sharing = e->compositor()->uses_dmabuf()
                                ? FLUTTER_LINUX_BUFFER_SHARING_LINUX_DMABUF
                                : FLUTTER_LINUX_BUFFER_SHARING_WL_SHM;
  } else {
    result.buffer_sharing =
        static_cast<flutter::X11Display*>(display)->has_dri3()
            ? FLUTTER_LINUX_BUFFER_SHARING_DRI3
            : FLUTTER_LINUX_BUFFER_SHARING_MIT_SHM;
  }
  result.buffer_format = flutter::DmabufBuffer::kFormat;
  result.buffer_modifier = e->compositor()->buffer_modifier();
  result.buffer_synchronization =
      e->compositor()->explicit_sync()
          ? FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_EXPLICIT
          : FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_IMPLICIT;
  if (wayland_display != nullptr) {
    result.presentation_feedback = wayland_display->presentation() != nullptr;
    result.fifo = wayland_display->fifo_manager() != nullptr;
    result.fractional_scale =
        wayland_display->viewporter() != nullptr &&
        wayland_display->fractional_scale_manager() != nullptr;
  } else {
    // The Present extension reports when frames are shown.
    result.presentation_feedback = true;
  }

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

int64_t flutter_linux_engine_register_pixel_buffer_texture(
    FlutterLinuxEngine* engine,
    FlutterLinuxPixelBufferCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  flutter::FlutterLinuxEngine* e = ToEngine(engine);
  return e->texture_registrar()->RegisterTexture(
      std::make_unique<flutter::PixelBufferTexture>(
          e->egl(),
          [notify = std::move(notify), callback, user_data](
              size_t width,
              size_t height) -> std::optional<flutter::PixelBufferFrame> {
            const FlutterLinuxPixelBuffer* buffer =
                callback(width, height, user_data);
            if (buffer == nullptr) {
              return std::nullopt;
            }
            if (buffer->struct_size < sizeof(FlutterLinuxPixelBuffer)) {
              FML_LOG(ERROR) << "Invalid FlutterLinuxPixelBuffer struct_size";
              return std::nullopt;
            }
            flutter::PixelBufferFrame frame;
            frame.pixels = buffer->buffer;
            frame.width = buffer->width;
            frame.height = buffer->height;
            frame.row_bytes = buffer->row_bytes;
            if (buffer->release_callback != nullptr) {
              frame.release = [release = buffer->release_callback,
                               release_user_data =
                                   buffer->release_user_data]() {
                release(release_user_data);
              };
            }
            return frame;
          }));
}

int64_t flutter_linux_engine_register_dmabuf_texture(
    FlutterLinuxEngine* engine,
    FlutterLinuxDmabufCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  flutter::FlutterLinuxEngine* e = ToEngine(engine);
  return e->texture_registrar()->RegisterTexture(
      std::make_unique<flutter::DmabufTexture>(
          e->egl(),
          [notify = std::move(notify), callback, user_data](
              size_t width,
              size_t height) -> std::optional<flutter::DmabufFrame> {
            const FlutterLinuxDmabuf* dmabuf =
                callback(width, height, user_data);
            if (dmabuf == nullptr) {
              return std::nullopt;
            }
            flutter::DmabufFrame frame;
            if (dmabuf->release_callback != nullptr) {
              frame.release = [release = dmabuf->release_callback,
                               release_user_data =
                                   dmabuf->release_user_data]() {
                release(release_user_data);
              };
            }
            if (dmabuf->struct_size < sizeof(FlutterLinuxDmabuf) ||
                dmabuf->n_planes > FLUTTER_LINUX_DMABUF_MAX_PLANES) {
              FML_LOG(ERROR) << "Invalid FlutterLinuxDmabuf";
              if (frame.release) {
                frame.release();
              }
              return std::nullopt;
            }
            frame.width = dmabuf->width;
            frame.height = dmabuf->height;
            frame.format = dmabuf->format;
            frame.modifier = dmabuf->modifier;
            for (uint32_t i = 0; i < dmabuf->n_planes; i++) {
              frame.planes.push_back({dmabuf->planes[i].fd,
                                      dmabuf->planes[i].offset,
                                      dmabuf->planes[i].stride});
            }
            frame.color_space = ToYuvColorSpace(dmabuf->color_space);
            frame.range = ToYuvRange(dmabuf->range);
            return frame;
          }));
}

bool flutter_linux_engine_mark_texture_frame_available(
    FlutterLinuxEngine* engine,
    int64_t texture_id) {
  return ToEngine(engine)->texture_registrar()->MarkTextureFrameAvailable(
      texture_id);
}

void flutter_linux_engine_unregister_texture(FlutterLinuxEngine* engine,
                                             int64_t texture_id,
                                             FlutterLinuxCallback done,
                                             void* user_data) {
  std::function<void()> callback;
  if (done != nullptr) {
    callback = [done, user_data]() { done(user_data); };
  }
  ToEngine(engine)->texture_registrar()->UnregisterTexture(texture_id,
                                                           std::move(callback));
}

void flutter_linux_engine_register_platform_view_type(
    FlutterLinuxEngine* engine,
    const char* view_type,
    FlutterLinuxPlatformViewFactory factory,
    FlutterLinuxCallback destroy,
    void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  ToEngine(engine)->platform_view_manager()->RegisterViewType(
      view_type, [notify = std::move(notify), factory, user_data](
                     flutter::LinuxPlatformView* view,
                     const std::vector<uint8_t>& params) {
        return factory(reinterpret_cast<FlutterLinuxPlatformView*>(view),
                       view->id(), params.data(), params.size(), user_data);
      });
}

void flutter_linux_platform_view_set_callbacks(
    FlutterLinuxPlatformView* view,
    const FlutterLinuxPlatformViewCallbacks* callbacks,
    void* user_data,
    FlutterLinuxCallback destroy) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  FlutterLinuxPlatformViewCallbacks c = {};
  if (callbacks != nullptr) {
    std::memcpy(&c, callbacks, std::min(callbacks->struct_size, sizeof(c)));
  }
  flutter::LinuxPlatformView::ResizeCallback on_resize;
  if (c.resize != nullptr) {
    on_resize = [notify, resize = c.resize, user_data](int width, int height,
                                                       double scale) {
      resize(width, height, scale, user_data);
    };
  }
  flutter::LinuxPlatformView::PointerCallback on_pointer =
      [notify, pointer_event = c.pointer_event,
       user_data](const flutter::PlatformViewPointerEvent& event) {
        if (pointer_event == nullptr) {
          return;
        }
        FlutterLinuxPlatformViewPointerEvent e = {};
        e.struct_size = sizeof(e);
        e.kind = static_cast<FlutterLinuxPointerEventKind>(event.kind);
        e.device_kind =
            static_cast<FlutterLinuxPointerDeviceKind>(event.device_kind);
        e.device = event.device;
        e.buttons = event.buttons;
        e.x = event.x;
        e.y = event.y;
        e.scroll_delta_x = event.scroll_delta_x;
        e.scroll_delta_y = event.scroll_delta_y;
        e.timestamp_us = event.timestamp_us;
        pointer_event(&e, user_data);
      };
  ToPlatformView(view)->SetCallbacks(std::move(on_resize),
                                     std::move(on_pointer));
}

int64_t flutter_linux_platform_view_get_id(FlutterLinuxPlatformView* view) {
  return ToPlatformView(view)->id();
}

FlutterLinuxPlatformViewBuffer* flutter_linux_platform_view_create_buffer(
    FlutterLinuxPlatformView* view,
    const FlutterLinuxDmabuf* dmabuf) {
  if (dmabuf == nullptr || dmabuf->struct_size < sizeof(FlutterLinuxDmabuf) ||
      dmabuf->n_planes > FLUTTER_LINUX_DMABUF_MAX_PLANES) {
    FML_LOG(ERROR) << "Invalid FlutterLinuxDmabuf";
    return nullptr;
  }
  flutter::DmabufFrame frame;
  frame.width = dmabuf->width;
  frame.height = dmabuf->height;
  frame.format = dmabuf->format;
  frame.modifier = dmabuf->modifier;
  for (uint32_t i = 0; i < dmabuf->n_planes; i++) {
    frame.planes.push_back({dmabuf->planes[i].fd, dmabuf->planes[i].offset,
                            dmabuf->planes[i].stride});
  }
  if (dmabuf->release_callback != nullptr) {
    frame.release = [release = dmabuf->release_callback,
                     release_user_data = dmabuf->release_user_data]() {
      release(release_user_data);
    };
  }
  return reinterpret_cast<FlutterLinuxPlatformViewBuffer*>(
      ToPlatformView(view)->CreateBuffer(frame).release());
}

void flutter_linux_platform_view_present(
    FlutterLinuxPlatformView* view,
    FlutterLinuxPlatformViewBuffer* buffer) {
  ToPlatformView(view)->Present(ToPlatformViewBuffer(buffer));
}

void flutter_linux_platform_view_buffer_free(
    FlutterLinuxPlatformViewBuffer* buffer) {
  delete ToPlatformViewBuffer(buffer);
}

wl_surface* flutter_linux_view_get_wl_surface(FlutterLinuxView* view) {
  flutter::FlutterLinuxView* v = ToView(view);
  if (v->engine()->application()->display()->backend() !=
      flutter::Display::Backend::kWayland) {
    return nullptr;
  }
  return static_cast<flutter::WaylandWindow*>(v->window())->surface();
}

uint32_t flutter_linux_view_get_x11_window(FlutterLinuxView* view) {
  flutter::FlutterLinuxView* v = ToView(view);
  if (v->engine()->application()->display()->backend() !=
      flutter::Display::Backend::kX11) {
    return 0;
  }
  return static_cast<flutter::X11Window*>(v->window())->window();
}

void flutter_linux_view_get_size(FlutterLinuxView* view,
                                 int* width,
                                 int* height) {
  flutter::Window* window = ToView(view)->window();
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
