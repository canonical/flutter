import 'window.dart';

/// Creates a default [WindowingOwner] for web. Returns `null` as web does not
/// support multiple windows.
WindowingOwner? createDefaultOwner() {
  return null;
}
