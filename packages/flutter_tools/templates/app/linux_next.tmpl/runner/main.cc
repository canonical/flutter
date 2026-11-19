#include <flutter_linux/flutter_linux.h>

#include <cstdio>

int main(int argc, char** argv) {
  auto application = flutter::FlutterDesktopApplication::create();
  if (!application) {
    fprintf(stderr, "Failed to create Flutter application\n");
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

  if (!engine->run()) {
    fprintf(stderr, "Failed to run Flutter engine\n");
    return 1;
  }

  application->run();

  return 0;
}
