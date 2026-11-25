#include <flutter_linux/flutter_linux.h>

#include <cstdio>

int main(int argc, char** argv) {
  flutter::FlutterDesktopResult result;
  auto application = flutter::FlutterDesktopApplication::create(&result);
  if (!application) {
    fprintf(stderr, "Failed to create Flutter application: %s\n",
            flutter::to_string(result));
    return 1;
  }
  application->set_application_id(APPLICATION_ID);

  flutter::FlutterDesktopEngineProperties properties;
  // Paths are relative to the directory containing the executable.
  properties.assets_path = "data/flutter_assets";
  properties.icu_data_path = "data/icudtl.dat";
  properties.aot_library_path = "lib/libapp.so";
  properties.dart_entrypoint_arguments.assign(argv + 1, argv + argc);

  auto engine = flutter::FlutterDesktopEngine::create(application, properties);
  if (!engine) {
    fprintf(stderr, "Failed to create Flutter engine\n");
    return 1;
  }

  auto view =
      flutter::FlutterDesktopView::create_implicit(engine, 1280, 720, &result);
  if (!view) {
    fprintf(stderr, "Failed to create Flutter view: %s\n",
            flutter::to_string(result));
    return 1;
  }
  view->set_title(WINDOW_TITLE);

  if (!engine->run(&result)) {
    fprintf(stderr, "Failed to run Flutter engine: %s\n",
            flutter::to_string(result));
    return 1;
  }

  application->run();

  return 0;
}
