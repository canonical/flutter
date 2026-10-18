// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_ABI_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_ABI_H_

// The binary interface to libflutter_linux.so.
//
// Applications should not use this directly, use the C++ API in
// flutter_linux.h instead.
//
// The library is built with its own C++ standard library, so C++ types can't
// be passed across the library boundary. This interface only uses C types so
// that the C++ API can be compiled in the application with the application's
// standard library.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flutter_export.h"
#include "flutter_plugin_registrar.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _FlutterLinuxApplication FlutterLinuxApplication;
typedef struct _FlutterLinuxEngine FlutterLinuxEngine;
typedef struct _FlutterLinuxView FlutterLinuxView;

// Wayland objects, from libwayland-client.
struct wl_display;
struct wl_surface;

typedef struct {
  const char* assets_path;
  const char* icu_data_path;
  const char* aot_library_path;
  const char* dart_entrypoint;
  int dart_entrypoint_argc;
  const char* const* dart_entrypoint_argv;
} FlutterLinuxEngineProperties;

typedef void (*FlutterLinuxViewCloseCallback)(void* user_data);

typedef enum {
  FLUTTER_LINUX_RENDERER_SKIA,
  FLUTTER_LINUX_RENDERER_IMPELLER,
} FlutterLinuxRenderer;

typedef enum {
  FLUTTER_LINUX_GRAPHICS_API_OPENGL_ES,
  FLUTTER_LINUX_GRAPHICS_API_VULKAN,
} FlutterLinuxGraphicsApi;

typedef enum {
  FLUTTER_LINUX_BUFFER_SHARING_LINUX_DMABUF,
  FLUTTER_LINUX_BUFFER_SHARING_WL_SHM,
} FlutterLinuxBufferSharing;

typedef enum {
  FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_IMPLICIT,
  FLUTTER_LINUX_BUFFER_SYNCHRONIZATION_EXPLICIT,
} FlutterLinuxBufferSynchronization;

// Strings remain valid until the engine is freed. Only the first
// |struct_size| bytes are written, so fields can be added in later versions.
typedef struct {
  size_t struct_size;
  FlutterLinuxRenderer renderer;
  FlutterLinuxGraphicsApi graphics_api;
  const char* gpu_device;
  const char* gl_vendor;
  const char* gl_renderer;
  const char* gl_version;
  FlutterLinuxBufferSharing buffer_sharing;
  uint32_t buffer_format;
  uint64_t buffer_modifier;
  FlutterLinuxBufferSynchronization buffer_synchronization;
  bool presentation_feedback;
  bool fifo;
  bool fractional_scale;
} FlutterLinuxRenderingInfo;

FLUTTER_EXPORT FlutterLinuxApplication* flutter_linux_application_new(void);

FLUTTER_EXPORT void flutter_linux_application_free(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_run(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_quit(
    FlutterLinuxApplication* application);

// Returns the number of Wayland globals bound from the compositor.
FLUTTER_EXPORT size_t flutter_linux_application_get_wayland_protocol_count(
    FlutterLinuxApplication* application);

// Gets the interface name and bound version of the Wayland global at |index|.
// |name| remains valid until the application is freed.
FLUTTER_EXPORT void flutter_linux_application_get_wayland_protocol(
    FlutterLinuxApplication* application,
    size_t index,
    const char** name,
    uint32_t* version);

FLUTTER_EXPORT FlutterLinuxEngine* flutter_linux_engine_new(
    FlutterLinuxApplication* application,
    const FlutterLinuxEngineProperties* properties);

FLUTTER_EXPORT void flutter_linux_engine_free(FlutterLinuxEngine* engine);

FLUTTER_EXPORT bool flutter_linux_engine_run(FlutterLinuxEngine* engine);

FLUTTER_EXPORT bool flutter_linux_engine_is_running(FlutterLinuxEngine* engine);

FLUTTER_EXPORT void flutter_linux_engine_get_rendering_info(
    FlutterLinuxEngine* engine,
    FlutterLinuxRenderingInfo* info);

FLUTTER_EXPORT FlutterLinuxView*
flutter_linux_view_new(FlutterLinuxEngine* engine, int width, int height);

FLUTTER_EXPORT void flutter_linux_view_free(FlutterLinuxView* view);

FLUTTER_EXPORT int64_t flutter_linux_view_get_id(FlutterLinuxView* view);

FLUTTER_EXPORT void flutter_linux_view_set_title(FlutterLinuxView* view,
                                                 const char* title);

FLUTTER_EXPORT void flutter_linux_view_set_application_id(
    FlutterLinuxView* view,
    const char* application_id);

// Sets |callback| to be called when the user requests the view be closed,
// replacing the default behavior of quitting the application. Pass NULL to
// restore the default behavior.
FLUTTER_EXPORT void flutter_linux_view_set_close_callback(
    FlutterLinuxView* view,
    FlutterLinuxViewCloseCallback callback,
    void* user_data);

// Plugins.
//
// Each plugin has its own registrar in each engine, which is destroyed with
// the engine. Registrars are used on the platform thread, except where noted.
// Plugins are expected to stop any threads they use before their registrar's
// destruction handler returns.

// File descriptor events.
typedef enum {
  FLUTTER_LINUX_FD_EVENT_READABLE = 1 << 0,
  FLUTTER_LINUX_FD_EVENT_WRITABLE = 1 << 1,
  // Always reported, even if not requested.
  FLUTTER_LINUX_FD_EVENT_ERROR = 1 << 2,
  FLUTTER_LINUX_FD_EVENT_HANG_UP = 1 << 3,
} FlutterLinuxFdEvent;

typedef void (*FlutterLinuxCallback)(void* user_data);
typedef void (*FlutterLinuxFdCallback)(int fd,
                                       uint32_t events,
                                       void* user_data);
typedef void (*FlutterLinuxViewCallback)(int64_t view_id, void* user_data);

// Returns the registrar for the plugin called |plugin_name| in |engine|,
// creating it if necessary. The registrar remains valid until the engine is
// freed.
FLUTTER_EXPORT FlutterDesktopPluginRegistrarRef
flutter_linux_engine_get_plugin_registrar(FlutterLinuxEngine* engine,
                                          const char* plugin_name);

// Returns the connection to the Wayland compositor shared by all engines.
// Plugins may create their own event queues on it to use it from other
// threads.
FLUTTER_EXPORT struct wl_display* flutter_linux_plugin_registrar_get_wl_display(
    FlutterDesktopPluginRegistrarRef registrar);

// Returns the view with |view_id| in the registrar's engine, or NULL if it
// doesn't exist. The view remains valid until the view removed callback is
// called for it.
FLUTTER_EXPORT FlutterLinuxView* flutter_linux_plugin_registrar_get_view(
    FlutterDesktopPluginRegistrarRef registrar,
    int64_t view_id);

// Sets callbacks for when views are added to and removed from the
// registrar's engine. |added| is called for existing views before this
// returns. Pass NULL callbacks to stop receiving them.
FLUTTER_EXPORT void flutter_linux_plugin_registrar_set_view_callbacks(
    FlutterDesktopPluginRegistrarRef registrar,
    FlutterLinuxViewCallback added,
    FlutterLinuxViewCallback removed,
    void* user_data);

// Calls |callback| on the platform thread after |delay_ns| nanoseconds.
// |destroy|, which may be NULL, is called on the platform thread after the
// callback, or instead of it if the registrar is destroyed first. May be
// called from any thread while the registrar exists.
FLUTTER_EXPORT void flutter_linux_plugin_registrar_post_task(
    FlutterDesktopPluginRegistrarRef registrar,
    uint64_t delay_ns,
    FlutterLinuxCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data);

// Calls |callback| on the platform thread when any of |events|
// (FlutterLinuxFdEvent flags) occur on |fd|. |destroy|, which may be NULL, is
// called when the watch is removed or the registrar is destroyed. Returns an
// ID for the watch, or zero on failure.
FLUTTER_EXPORT uint64_t flutter_linux_plugin_registrar_add_fd_watch(
    FlutterDesktopPluginRegistrarRef registrar,
    int fd,
    uint32_t events,
    FlutterLinuxFdCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data);

// Changes the events the watch with |watch_id| waits for.
FLUTTER_EXPORT bool flutter_linux_plugin_registrar_set_fd_watch_events(
    FlutterDesktopPluginRegistrarRef registrar,
    uint64_t watch_id,
    uint32_t events);

// Removes the watch with |watch_id|. This may be called from its callback.
FLUTTER_EXPORT void flutter_linux_plugin_registrar_remove_fd_watch(
    FlutterDesktopPluginRegistrarRef registrar,
    uint64_t watch_id);

// The surface the view's Flutter content is shown in. Its origin is the
// origin of the view's content, and it is the root of the window's surface
// tree, so it can be used to identify the window (e.g. with xdg-foreign).
FLUTTER_EXPORT struct wl_surface* flutter_linux_view_get_wl_surface(
    FlutterLinuxView* view);

// Gets the logical size of the view's content.
FLUTTER_EXPORT void flutter_linux_view_get_size(FlutterLinuxView* view,
                                                int* width,
                                                int* height);

// Returns the number of physical pixels per logical pixel.
FLUTTER_EXPORT double flutter_linux_view_get_scale(FlutterLinuxView* view);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_ABI_H_
