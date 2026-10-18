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
#include <stdint.h>

#include "flutter_export.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _FlutterLinuxEngine FlutterLinuxEngine;
typedef struct _FlutterLinuxView FlutterLinuxView;

typedef struct {
  const char* assets_path;
  const char* icu_data_path;
  const char* aot_library_path;
  const char* dart_entrypoint;
  int dart_entrypoint_argc;
  const char* const* dart_entrypoint_argv;
} FlutterLinuxEngineProperties;

FLUTTER_EXPORT FlutterLinuxEngine* flutter_linux_engine_new(
    const FlutterLinuxEngineProperties* properties);

FLUTTER_EXPORT void flutter_linux_engine_free(FlutterLinuxEngine* engine);

FLUTTER_EXPORT bool flutter_linux_engine_run(FlutterLinuxEngine* engine);

FLUTTER_EXPORT bool flutter_linux_engine_is_running(FlutterLinuxEngine* engine);

FLUTTER_EXPORT void flutter_linux_engine_run_event_loop(
    FlutterLinuxEngine* engine);

FLUTTER_EXPORT void flutter_linux_engine_quit_event_loop(
    FlutterLinuxEngine* engine);

FLUTTER_EXPORT FlutterLinuxView*
flutter_linux_view_new(FlutterLinuxEngine* engine, int width, int height);

FLUTTER_EXPORT void flutter_linux_view_free(FlutterLinuxView* view);

FLUTTER_EXPORT int64_t flutter_linux_view_get_id(FlutterLinuxView* view);

FLUTTER_EXPORT void flutter_linux_view_set_title(FlutterLinuxView* view,
                                                 const char* title);

FLUTTER_EXPORT void flutter_linux_view_set_application_id(
    FlutterLinuxView* view,
    const char* application_id);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_ABI_H_
