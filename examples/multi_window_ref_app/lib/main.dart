import 'package:flutter/material.dart';

import 'app/my_app.dart';

void main() {
  runWidget(MultiWindowApp(initialWindows: [
    (context) => createRegularWindow(
        context: context,
        size: const Size(800, 600),
        builder: (context) {
          return const MyApp();
        })
  ]));
}
