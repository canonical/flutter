#include <flutter_linux/flutter_linux.h>

#include <cstdio>

#include "flutter/generated_plugin_registrant.h"

int main(int argc, char** argv) {
  auto application = flutter::FlutterDesktopApplication::create();
  if (!application) {
    fprintf(stderr, "Failed to connect to Wayland compositor\n");
    return 1;
  }

  flutter::FlutterDesktopEngineProperties properties;
  // Paths are relative to the directory containing the executable.
  properties.assets_path = "data/flutter_assets";
  properties.icu_data_path = "data/icudtl.dat";
  properties.aot_library_path = "lib/libapp.so";
  properties.dart_entrypoint_arguments.assign(argv + 1, argv + argc);

  application->set_application_id(APPLICATION_ID);

  auto engine = flutter::FlutterDesktopEngine::create(application, properties);
  if (!engine) {
    fprintf(stderr, "Failed to create Flutter engine\n");
    return 1;
  }
  RegisterPlugins(*engine);

  auto view = flutter::FlutterDesktopView::create(engine, 900, 720);
  if (!view) {
    fprintf(stderr, "Failed to create Flutter view\n");
    return 1;
  }
  view->set_title(WINDOW_TITLE);

  if (!engine->run()) {
    fprintf(stderr, "Failed to run Flutter engine\n");
    return 1;
  }

  // Runs until the window is closed.
  application->run();

  return 0;
}
