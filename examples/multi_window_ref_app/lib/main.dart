import 'package:flutter/material.dart';
import 'app/main_window.dart';

void main() {
  final RegularWindowController controller = RegularWindowController(
    size: const Size(800, 600),
    sizeConstraints: BoxConstraints.loose(Size(500, 500)),
    title: "Multi-Window Reference Application",
  );
  runWidget(RegularWindow(
      controller: controller,
      child: MaterialApp(home: MainWindow(mainController: controller))));
}
