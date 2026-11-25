// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_ABI_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_ABI_H_

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
#include "flutter_linux_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _FlutterLinuxApplication FlutterLinuxApplication;
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

// On success, sets |application| to the new application.
FLUTTER_EXPORT FlutterLinuxResult
flutter_linux_application_new(FlutterLinuxApplication** application);

FLUTTER_EXPORT void flutter_linux_application_free(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_run(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_quit(
    FlutterLinuxApplication* application);

FLUTTER_EXPORT void flutter_linux_application_set_application_id(
    FlutterLinuxApplication* application,
    const char* application_id);

FLUTTER_EXPORT FlutterLinuxEngine* flutter_linux_engine_new(
    FlutterLinuxApplication* application,
    const FlutterLinuxEngineProperties* properties);

FLUTTER_EXPORT void flutter_linux_engine_free(FlutterLinuxEngine* engine);

FLUTTER_EXPORT FlutterLinuxResult
flutter_linux_engine_run(FlutterLinuxEngine* engine);

FLUTTER_EXPORT bool flutter_linux_engine_is_running(FlutterLinuxEngine* engine);

// Creates the implicit view. Must be called before the engine runs, and only
// once per engine. On success, sets |view| to the new view.
FLUTTER_EXPORT FlutterLinuxResult
flutter_linux_view_new_implicit(FlutterLinuxEngine* engine,
                                int width,
                                int height,
                                FlutterLinuxView** view);

// Creates a view other than the implicit view. On success, sets |view| to the
// new view.
FLUTTER_EXPORT FlutterLinuxResult
flutter_linux_view_new(FlutterLinuxEngine* engine,
                       int width,
                       int height,
                       FlutterLinuxView** view);

FLUTTER_EXPORT void flutter_linux_view_free(FlutterLinuxView* view);

FLUTTER_EXPORT int64_t flutter_linux_view_get_id(FlutterLinuxView* view);

FLUTTER_EXPORT void flutter_linux_view_set_title(FlutterLinuxView* view,
                                                 const char* title);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_PUBLIC_FLUTTER_LINUX_ABI_H_
