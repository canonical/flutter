## Multi Window Samples

Samples from [The Multi View Playground](https://github.com/goderbauer/mvp/blob/main/README.md) for experimenting with rendering into multiple views without using Flutter's windowing API.

### raw_static.dart

Renders some view-specific information into each `FlutterView` available in `PlatformDispatcher.views` using only APIs
exposed by `dart:ui`. A new frame is only scheduled if the metrics of a `FlutterView` change or if a view is
added/removed.

### raw_dynamic.dart

Renders a spinning rectangular into each `FlutterView` available in `PlatformDispatcher.views` using only APIs exposed
by `dart:ui`. Frames are continuously scheduled to keep the animation running.

### widgets_static.dart

Renders some view-specific information into each `FlutterView` available in `PlatformDispatcher.views` using the Flutter
widget framework (`package:flutter/widgets.dart`). A new frame is only scheduled if the metrics of a `FlutterView`
change or if a view is added/removed.

### widgets_dynamic.dart

Renders a spinning rectangular into each `FlutterView` available in `PlatformDispatcher.views` using the Flutter
widget framework (`package:flutter/widgets.dart`). Frames are continuously scheduled to keep the animation running.

### widgets_counter.dart

Renders the Counter app (an interactive Material Design app) into each `FlutterView` available in
`PlatformDispatcher.views`.
