#include <flutter_linux/flutter_linux.h>

#include <cstdio>

int main(int argc, char** argv) {
  flutter::FlutterDesktopEngineProperties properties;
  // Paths are relative to the directory containing the executable.
  properties.assets_path = "data/flutter_assets";
  properties.icu_data_path = "data/icudtl.dat";
  properties.aot_library_path = "lib/libapp.so";
  properties.dart_entrypoint_arguments.assign(argv + 1, argv + argc);

  auto engine = flutter::FlutterDesktopEngine::create(properties);
  if (!engine) {
    fprintf(stderr, "Failed to create Flutter engine\n");
    return 1;
  }

  auto view = flutter::FlutterDesktopView::create(engine, 1280, 720);
  if (!view) {
    fprintf(stderr, "Failed to create Flutter view\n");
    return 1;
  }
  view->set_title(WINDOW_TITLE);
  view->set_application_id(APPLICATION_ID);

  if (!engine->run()) {
    fprintf(stderr, "Failed to run Flutter engine\n");
    return 1;
  }

  // Runs until the window is closed.
  engine->run_event_loop();

  return 0;
}
