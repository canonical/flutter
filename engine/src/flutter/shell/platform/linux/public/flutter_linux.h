// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flutter_export.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Opaque reference to a Flutter engine instance.
struct FlutterDesktopEngine;
typedef struct FlutterDesktopEngine* FlutterDesktopEngineRef;

// Opaque reference to a Flutter view controller.
struct FlutterDesktopViewController;
typedef struct FlutterDesktopViewController* FlutterDesktopViewControllerRef;

// Opaque reference to a Flutter view.
struct FlutterDesktopView;
typedef struct FlutterDesktopView* FlutterDesktopViewRef;

// The unique identifier for a view.
typedef int64_t FlutterDesktopViewId;

// Properties for configuring a Flutter engine instance.
typedef struct {
  // The path to the flutter_assets folder for the application to be run.
  // This can either be an absolute path or a path relative to the directory
  // containing the executable.
  const char* assets_path;

  // The path to the icudtl.dat file for the version of Flutter you are using.
  // This can either be an absolute path or a path relative to the directory
  // containing the executable.
  const char* icu_data_path;

  // The path to the AOT library file for your application, if any.
  // This can either be an absolute path or a path relative to the directory
  // containing the executable. This can be NULL for a non-AOT build, as it
  // will be ignored in that case.
  const char* aot_library_path;

  // The name of the top-level Dart entrypoint function. If NULL or the empty
  // string, 'main' is assumed. If a custom entrypoint is used, this parameter
  // must specify the name of a top-level function in the same Dart library as
  // the app's main() function. Custom entrypoint functions must be decorated
  // with `@pragma('vm:entry-point')` to ensure the method is not tree-shaken
  // by the Dart compiler.
  const char* dart_entrypoint;

  // Number of elements in the array passed in as dart_entrypoint_argv.
  int dart_entrypoint_argc;

  // Array of Dart entrypoint arguments. This is deep copied during the call
  // to FlutterDesktopEngineCreate.
  const char** dart_entrypoint_argv;
} FlutterDesktopEngineProperties;

// ========== Engine ==========

// Creates a Flutter engine with the given properties.
//
// This connects to the Wayland compositor and initializes rendering. Returns
// NULL if this fails, e.g. if no Wayland compositor is available.
//
// The caller owns the returned reference, and is responsible for calling
// FlutterDesktopEngineDestroy. The lifetime of |engine_properties| is
// required to extend only until the end of this call.
FLUTTER_EXPORT FlutterDesktopEngineRef FlutterDesktopEngineCreate(
    const FlutterDesktopEngineProperties* engine_properties);

// Shuts down and destroys the given engine instance. Returns true if the
// shutdown was successful, or if the engine was not running.
//
// |engine| is no longer valid after this call.
FLUTTER_EXPORT bool FlutterDesktopEngineDestroy(FlutterDesktopEngineRef engine);

// Starts running the given engine instance.
//
// The entry_point parameter is deprecated but preserved for backward
// compatibility. If desired, a custom Dart entrypoint function can be set in
// the dart_entrypoint field of the FlutterDesktopEngineProperties struct
// passed to FlutterDesktopEngineCreate.
//
// If specified, entry_point must be the name of a top-level function from the
// same Dart library that contains the app's main() function, and must be
// decorated with `@pragma(vm:entry-point)` to ensure the method is not
// tree-shaken by the Dart compiler. If conflicting non-null values are passed
// to this function and via the FlutterDesktopEngineProperties struct, the run
// will fail.
//
// The engine must be run on the thread that created it, and this thread
// becomes the platform thread.
//
// Returns false if running the engine failed.
FLUTTER_EXPORT bool FlutterDesktopEngineRun(FlutterDesktopEngineRef engine,
                                            const char* entry_point);

// Runs the engine's event loop on the current thread, which must be the
// thread that created the engine.
//
// This processes Wayland events and Flutter tasks until
// FlutterDesktopEngineQuitEventLoop is called or the last view is closed by
// the user.
FLUTTER_EXPORT void FlutterDesktopEngineRunEventLoop(
    FlutterDesktopEngineRef engine);

// Causes FlutterDesktopEngineRunEventLoop to return.
//
// This may be called from any thread.
FLUTTER_EXPORT void FlutterDesktopEngineQuitEventLoop(
    FlutterDesktopEngineRef engine);

// ========== View Controller ==========

// Creates a view that hosts and displays the given engine instance in a new
// top-level window.
//
// This takes ownership of |engine|, so FlutterDesktopEngineDestroy should no
// longer be called on it, as it will be called internally when the view
// controller is destroyed. If the engine is not already running, the view
// controller will start running it automatically before displaying the
// window.
//
// |width| and |height| are the initial logical size of the window. The
// compositor may choose a different size.
//
// The caller owns the returned reference, and is responsible for calling
// FlutterDesktopViewControllerDestroy. Returns NULL on failure.
FLUTTER_EXPORT FlutterDesktopViewControllerRef
FlutterDesktopViewControllerCreate(int width,
                                   int height,
                                   FlutterDesktopEngineRef engine);

// Shuts down the engine instance associated with |controller|, and cleans up
// associated state.
//
// |controller| is no longer valid after this call.
FLUTTER_EXPORT void FlutterDesktopViewControllerDestroy(
    FlutterDesktopViewControllerRef controller);

// Returns the view controller's view ID.
FLUTTER_EXPORT FlutterDesktopViewId FlutterDesktopViewControllerGetViewId(
    FlutterDesktopViewControllerRef controller);

// Returns the handle for the engine running in |controller|.
//
// Its lifetime is the same as the |controller|'s.
FLUTTER_EXPORT FlutterDesktopEngineRef FlutterDesktopViewControllerGetEngine(
    FlutterDesktopViewControllerRef controller);

// Returns the view managed by the given controller.
FLUTTER_EXPORT FlutterDesktopViewRef
FlutterDesktopViewControllerGetView(FlutterDesktopViewControllerRef controller);

// ========== View ==========

// Sets the title of the window containing |view|.
FLUTTER_EXPORT void FlutterDesktopViewSetTitle(FlutterDesktopViewRef view,
                                               const char* title);

// Sets the application ID of the window containing |view|. This is used by
// the compositor to group windows and to find the application's .desktop
// file, so should match the name of that file.
FLUTTER_EXPORT void FlutterDesktopViewSetApplicationId(
    FlutterDesktopViewRef view,
    const char* application_id);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PUBLIC_FLUTTER_LINUX_H_
