#include <flutter_linux/flutter_linux.h>

#include <cstdio>

int main(int argc, char** argv) {
  FlutterDesktopEngineProperties properties = {};
  // Paths are relative to the directory containing the executable.
  properties.assets_path = "data/flutter_assets";
  properties.icu_data_path = "data/icudtl.dat";
  properties.aot_library_path = "lib/libapp.so";
  properties.dart_entrypoint_argc = argc - 1;
  properties.dart_entrypoint_argv = const_cast<const char**>(argv + 1);

  FlutterDesktopEngineRef engine = FlutterDesktopEngineCreate(&properties);
  if (engine == nullptr) {
    fprintf(stderr, "Failed to create Flutter engine\n");
    return 1;
  }

  // The view controller takes ownership of the engine and runs it.
  FlutterDesktopViewControllerRef controller =
      FlutterDesktopViewControllerCreate(1280, 720, engine);
  if (controller == nullptr) {
    fprintf(stderr, "Failed to create Flutter view\n");
    return 1;
  }

  FlutterDesktopViewRef view = FlutterDesktopViewControllerGetView(controller);
  FlutterDesktopViewSetTitle(view, WINDOW_TITLE);
  FlutterDesktopViewSetApplicationId(view, APPLICATION_ID);

  // Runs until the window is closed.
  FlutterDesktopEngineRunEventLoop(engine);

  FlutterDesktopViewControllerDestroy(controller);
  return 0;
}
