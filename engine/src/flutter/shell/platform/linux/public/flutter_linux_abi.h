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

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _FlutterLinuxApplication FlutterLinuxApplication;
typedef struct _FlutterLinuxEngine FlutterLinuxEngine;
typedef struct _FlutterLinuxView FlutterLinuxView;
typedef struct _FlutterLinuxMessenger FlutterLinuxMessenger;
typedef struct _FlutterLinuxMessageResponse FlutterLinuxMessageResponse;

// Wayland objects, from libwayland-client.
struct wl_display;
struct wl_surface;

// The X11 connection, from libxcb.
struct xcb_connection_t;

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
  FLUTTER_LINUX_DISPLAY_BACKEND_WAYLAND,
  FLUTTER_LINUX_DISPLAY_BACKEND_X11,
} FlutterLinuxDisplayBackend;

typedef enum {
  FLUTTER_LINUX_BUFFER_SHARING_LINUX_DMABUF,
  FLUTTER_LINUX_BUFFER_SHARING_WL_SHM,
  FLUTTER_LINUX_BUFFER_SHARING_DRI3,
  FLUTTER_LINUX_BUFFER_SHARING_MIT_SHM,
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

// Connects to the display server: the Wayland compositor if available,
// otherwise the X server. The FLUTTER_LINUX_BACKEND environment variable may
// be set to "wayland" or "x11" to choose one. Returns NULL if no display
// server or GPU is available.
FLUTTER_EXPORT FlutterLinuxApplication* flutter_linux_application_new(void);

FLUTTER_EXPORT void flutter_linux_application_free(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_run(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_quit(
    FlutterLinuxApplication* application);

// Sets the application ID given to windows created after this call.
FLUTTER_EXPORT void flutter_linux_application_set_application_id(
    FlutterLinuxApplication* application,
    const char* application_id);

// Returns the display server the application is connected to. Wayland is
// used if available, otherwise X11. The FLUTTER_LINUX_BACKEND environment
// variable may be set to "wayland" or "x11" to choose one.
FLUTTER_EXPORT FlutterLinuxDisplayBackend
flutter_linux_application_get_display_backend(
    FlutterLinuxApplication* application);

// Returns the connection to the Wayland compositor shared by all engines, or
// NULL if the application is using X11. Plugins may create their own event
// queues on it to use it from other threads.
FLUTTER_EXPORT struct wl_display* flutter_linux_application_get_wl_display(
    FlutterLinuxApplication* application);

// Returns the connection to the X server shared by all engines, or NULL if
// the application is using Wayland. Events on the connection are processed
// by the embedder.
FLUTTER_EXPORT struct xcb_connection_t*
flutter_linux_application_get_xcb_connection(
    FlutterLinuxApplication* application);

// Returns the number of Wayland globals bound from the compositor, or zero if
// the application is using X11.
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

// Plugin support.
//
// Functions are called on the platform thread, except where noted. Plugins
// are expected to stop any threads they use before their engine is freed.

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

// Called with a reply to a message, or NULL and zero if there was no reply.
typedef void (*FlutterLinuxReplyCallback)(const uint8_t* data,
                                          size_t size,
                                          void* user_data);

// Called with a message from the framework. |response| must be passed to
// flutter_linux_message_response_send exactly once.
typedef void (*FlutterLinuxMessageHandler)(
    const uint8_t* data,
    size_t size,
    FlutterLinuxMessageResponse* response,
    void* user_data);

// Returns the messenger used to communicate with the framework running in
// |engine|. It remains valid until the engine is freed.
FLUTTER_EXPORT FlutterLinuxMessenger* flutter_linux_engine_get_messenger(
    FlutterLinuxEngine* engine);

// Sends |data| on |channel|. |reply|, which may be NULL, is called exactly
// once on the platform thread, including if the engine is freed before a
// reply is received. May be called from any thread while the engine exists.
FLUTTER_EXPORT void flutter_linux_messenger_send(
    FlutterLinuxMessenger* messenger,
    const char* channel,
    const uint8_t* data,
    size_t size,
    FlutterLinuxReplyCallback reply,
    void* user_data);

// Sets |handler| to be called for messages on |channel|, replacing any
// existing handler. Pass a NULL handler to remove it. |destroy|, which may be
// NULL, is called when the handler is replaced or removed, or the engine is
// freed.
FLUTTER_EXPORT void flutter_linux_messenger_set_handler(
    FlutterLinuxMessenger* messenger,
    const char* channel,
    FlutterLinuxMessageHandler handler,
    void* user_data,
    FlutterLinuxCallback destroy);

// Responds to a message and frees |response|. Must be called exactly once for
// each message. May be called from any thread, including after the engine is
// freed (in which case the response is dropped).
FLUTTER_EXPORT void flutter_linux_message_response_send(
    FlutterLinuxMessageResponse* response,
    const uint8_t* data,
    size_t size);

// Calls |callback| on the platform thread after |delay_ns| nanoseconds.
// |destroy|, which may be NULL, is called on the platform thread after the
// callback, or instead of it if the engine is freed first. May be called
// from any thread while the engine exists.
FLUTTER_EXPORT void flutter_linux_engine_post_task(
    FlutterLinuxEngine* engine,
    uint64_t delay_ns,
    FlutterLinuxCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data);

// Calls |callback| on the platform thread when any of |events|
// (FlutterLinuxFdEvent flags) occur on |fd|. |destroy|, which may be NULL, is
// called when the watch is removed or the engine is freed. Returns an ID for
// the watch, or zero on failure.
FLUTTER_EXPORT uint64_t
flutter_linux_engine_add_fd_watch(FlutterLinuxEngine* engine,
                                  int fd,
                                  uint32_t events,
                                  FlutterLinuxFdCallback callback,
                                  FlutterLinuxCallback destroy,
                                  void* user_data);

// Changes the events the watch with |watch_id| waits for.
FLUTTER_EXPORT bool flutter_linux_engine_set_fd_watch_events(
    FlutterLinuxEngine* engine,
    uint64_t watch_id,
    uint32_t events);

// Removes the watch with |watch_id|. This may be called from its callback.
FLUTTER_EXPORT void flutter_linux_engine_remove_fd_watch(
    FlutterLinuxEngine* engine,
    uint64_t watch_id);

// External textures show content from plugins (e.g. video or a camera) in a
// Texture widget. The texture ID is passed to the widget.
//
// A texture's callback is called on the raster thread when the framework
// draws the texture after flutter_linux_engine_mark_texture_frame_available
// has been called. The frame it returns is used until the next time it is
// called.

// A frame of pixels in CPU memory, copied to the GPU when drawn.
typedef struct {
  // The size of this struct. Must be sizeof(FlutterLinuxPixelBuffer).
  size_t struct_size;
  // Premultiplied RGBA pixels, 8 bits per channel.
  const uint8_t* buffer;
  size_t width;
  size_t height;
  // The number of bytes from the start of one row to the next, or zero if
  // rows are tightly packed.
  size_t row_bytes;
  // Called, from any thread, once |buffer| has been copied. May be NULL.
  FlutterLinuxCallback release_callback;
  void* release_user_data;
} FlutterLinuxPixelBuffer;

// Called to get the latest frame for display at |width|x|height| physical
// pixels. Returns NULL if there is no frame. The returned struct only needs
// to remain valid until the callback returns.
typedef const FlutterLinuxPixelBuffer* (*FlutterLinuxPixelBufferCallback)(
    size_t width,
    size_t height,
    void* user_data);

// The conversion from YUV to RGB.
typedef enum {
  // Chosen by the driver.
  FLUTTER_LINUX_YUV_COLOR_SPACE_DEFAULT,
  FLUTTER_LINUX_YUV_COLOR_SPACE_BT601,
  FLUTTER_LINUX_YUV_COLOR_SPACE_BT709,
  FLUTTER_LINUX_YUV_COLOR_SPACE_BT2020,
} FlutterLinuxYuvColorSpace;

// The range of YUV values.
typedef enum {
  // Chosen by the driver.
  FLUTTER_LINUX_YUV_RANGE_DEFAULT,
  // Y from 16-235, UV from 16-240 (for 8 bit values).
  FLUTTER_LINUX_YUV_RANGE_NARROW,
  FLUTTER_LINUX_YUV_RANGE_FULL,
} FlutterLinuxYuvRange;

#define FLUTTER_LINUX_DMABUF_MAX_PLANES 4

typedef struct {
  // A dmabuf file descriptor. It is not closed or kept by the engine.
  int fd;
  uint32_t offset;
  uint32_t stride;
} FlutterLinuxDmabufPlane;

// A frame in a GPU buffer, shown without being copied. Any format and
// modifier the GPU can sample can be used, including YUV formats such as
// NV12.
typedef struct {
  // The size of this struct. Must be sizeof(FlutterLinuxDmabuf).
  size_t struct_size;
  uint32_t width;
  uint32_t height;
  // The DRM fourcc format, e.g. DRM_FORMAT_ARGB8888 or DRM_FORMAT_NV12.
  uint32_t format;
  // The DRM format modifier, or DRM_FORMAT_MOD_INVALID if the layout is
  // implied by the driver.
  uint64_t modifier;
  uint32_t n_planes;
  FlutterLinuxDmabufPlane planes[FLUTTER_LINUX_DMABUF_MAX_PLANES];
  // Used for YUV formats.
  FlutterLinuxYuvColorSpace color_space;
  FlutterLinuxYuvRange range;
  // Called, from any thread, once the engine has stopped using the buffer
  // and it can be reused. This may be after the texture is unregistered.
  // The buffer must be finished rendering when it is returned, as no fences
  // are waited for. May be NULL.
  FlutterLinuxCallback release_callback;
  void* release_user_data;
} FlutterLinuxDmabuf;

// Called to get the latest frame for display at |width|x|height| physical
// pixels. Returns NULL if there is no frame. The returned struct only needs
// to remain valid until the callback returns.
typedef const FlutterLinuxDmabuf* (
    *FlutterLinuxDmabufCallback)(size_t width, size_t height, void* user_data);

// Registers a texture that copies frames from CPU memory. |destroy|, which
// may be NULL, is called on the platform thread when the texture is
// unregistered or the engine is freed. Returns the texture ID, or -1 on
// failure.
FLUTTER_EXPORT int64_t flutter_linux_engine_register_pixel_buffer_texture(
    FlutterLinuxEngine* engine,
    FlutterLinuxPixelBufferCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data);

// Registers a texture that shows frames from dmabufs. |destroy|, which may be
// NULL, is called on the platform thread when the texture is unregistered or
// the engine is freed. Returns the texture ID, or -1 on failure.
FLUTTER_EXPORT int64_t flutter_linux_engine_register_dmabuf_texture(
    FlutterLinuxEngine* engine,
    FlutterLinuxDmabufCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data);

// Tells the engine the texture with |texture_id| has a new frame, so its
// callback will be called the next time it is drawn. May be called from any
// thread while the engine exists. Returns false if there is no such texture.
FLUTTER_EXPORT bool flutter_linux_engine_mark_texture_frame_available(
    FlutterLinuxEngine* engine,
    int64_t texture_id);

// Unregisters the texture with |texture_id|. Its callback may be called on
// the raster thread until |done|, which may be NULL, is called on the
// platform thread. The texture's |destroy| is called just before |done|.
// Both may be called after the engine is freed.
FLUTTER_EXPORT void flutter_linux_engine_unregister_texture(
    FlutterLinuxEngine* engine,
    int64_t texture_id,
    FlutterLinuxCallback done,
    void* user_data);

// Platform views show native content (e.g. video or a web page) in a
// PlatformView widget, such as LinuxView. The content is shown in its own
// Wayland surface, so the compositor can put it on screen directly without it
// being drawn by Flutter. The plugin provides the content as dmabufs, and the
// engine places, clips and stacks the surface to match the widget.

typedef struct _FlutterLinuxPlatformView FlutterLinuxPlatformView;
typedef struct _FlutterLinuxPlatformViewBuffer FlutterLinuxPlatformViewBuffer;

// Sets up |view| when the framework creates a platform view. |params| are the
// widget's creationParams, encoded with its codec. Returns false if the view
// can't be created.
typedef bool (*FlutterLinuxPlatformViewFactory)(FlutterLinuxPlatformView* view,
                                                int64_t view_id,
                                                const uint8_t* params,
                                                size_t params_size,
                                                void* user_data);

// Registers |factory| to create views of |view_type|, replacing any existing
// factory. |destroy|, which may be NULL, is called when the factory is
// replaced or the engine is freed. Platform views are only supported on
// Wayland; on X11 requests to create them fail.
FLUTTER_EXPORT void flutter_linux_engine_register_platform_view_type(
    FlutterLinuxEngine* engine,
    const char* view_type,
    FlutterLinuxPlatformViewFactory factory,
    FlutterLinuxCallback destroy,
    void* user_data);

typedef enum {
  FLUTTER_LINUX_POINTER_EVENT_DOWN,
  FLUTTER_LINUX_POINTER_EVENT_MOVE,
  FLUTTER_LINUX_POINTER_EVENT_UP,
  FLUTTER_LINUX_POINTER_EVENT_CANCEL,
  // The pointer moved while no buttons are pressed.
  FLUTTER_LINUX_POINTER_EVENT_HOVER,
  // The pointer left the view.
  FLUTTER_LINUX_POINTER_EVENT_EXIT,
  FLUTTER_LINUX_POINTER_EVENT_SCROLL,
} FlutterLinuxPointerEventKind;

typedef enum {
  FLUTTER_LINUX_POINTER_DEVICE_MOUSE,
  FLUTTER_LINUX_POINTER_DEVICE_TOUCH,
  FLUTTER_LINUX_POINTER_DEVICE_STYLUS,
  FLUTTER_LINUX_POINTER_DEVICE_TRACKPAD,
} FlutterLinuxPointerDeviceKind;

// A pointer event in a platform view.
typedef struct {
  // The size of this struct.
  size_t struct_size;
  FlutterLinuxPointerEventKind kind;
  FlutterLinuxPointerDeviceKind device_kind;
  // Identifies the pointer, e.g. a touch point.
  int32_t device;
  // The pressed buttons, as Flutter's kPrimaryButton etc. flags.
  int64_t buttons;
  // The position in logical pixels relative to the view's top left, before
  // any transforms the view is shown with.
  double x;
  double y;
  // For FLUTTER_LINUX_POINTER_EVENT_SCROLL, the logical pixels to scroll.
  double scroll_delta_x;
  double scroll_delta_y;
  int64_t timestamp_us;
} FlutterLinuxPlatformViewPointerEvent;

typedef struct {
  // The size of this struct. Must be sizeof(FlutterLinuxPlatformViewCallbacks).
  size_t struct_size;
  // Called when the size the view is shown at changes. Buffers of
  // |width|x|height| physical pixels are shown without scaling. |scale| is
  // the number of physical pixels per logical pixel. May be NULL.
  void (*resize)(int width, int height, double scale, void* user_data);
  // Called with pointer events in the view. May be NULL.
  void (*pointer_event)(const FlutterLinuxPlatformViewPointerEvent* event,
                        void* user_data);
} FlutterLinuxPlatformViewCallbacks;

// Sets the callbacks for |view|, typically in the factory. |destroy|, which
// may be NULL, is called when the callbacks are replaced or the framework
// disposes the view, after which |view| must no longer be used.
FLUTTER_EXPORT void flutter_linux_platform_view_set_callbacks(
    FlutterLinuxPlatformView* view,
    const FlutterLinuxPlatformViewCallbacks* callbacks,
    void* user_data,
    FlutterLinuxCallback destroy);

// Returns the ID the framework uses for |view|.
FLUTTER_EXPORT int64_t
flutter_linux_platform_view_get_id(FlutterLinuxPlatformView* view);

// Creates a buffer that can be presented in |view| from |dmabuf|. The file
// descriptors are not closed or kept. The format and modifier must be
// supported by the Wayland compositor; DRM_FORMAT_ARGB8888,
// DRM_FORMAT_XRGB8888 and DRM_FORMAT_NV12 usually are. |dmabuf|'s release
// callback is called each time the buffer can be reused after it has been
// presented: once another buffer has replaced it and the compositor has
// finished with it. The color space and range are not used. Returns NULL if
// the buffer can't be used.
FLUTTER_EXPORT FlutterLinuxPlatformViewBuffer*
flutter_linux_platform_view_create_buffer(FlutterLinuxPlatformView* view,
                                          const FlutterLinuxDmabuf* dmabuf);

// Shows |buffer| in |view|, replacing the previous buffer. Rendering into the
// buffer must be complete. Pass NULL to show nothing.
FLUTTER_EXPORT void flutter_linux_platform_view_present(
    FlutterLinuxPlatformView* view,
    FlutterLinuxPlatformViewBuffer* buffer);

// Frees |buffer|. This may be after its view is disposed.
FLUTTER_EXPORT void flutter_linux_platform_view_buffer_free(
    FlutterLinuxPlatformViewBuffer* buffer);

// The surface the view's Flutter content is shown in. Its origin is the
// origin of the view's content, and it is the root of the window's surface
// tree, so it can be used to identify the window (e.g. with xdg-foreign).
// NULL if the application is using X11.
FLUTTER_EXPORT struct wl_surface* flutter_linux_view_get_wl_surface(
    FlutterLinuxView* view);

// The X11 window the view's content is shown in, or zero if the application
// is using Wayland.
FLUTTER_EXPORT uint32_t
flutter_linux_view_get_x11_window(FlutterLinuxView* view);

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
