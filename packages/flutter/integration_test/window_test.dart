import 'dart:io' show Platform;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void startApp() => runWidget(MultiWindowApp(initialWindows: [
      (BuildContext context) => createRegularWindow(
          context: context,
          size: const Size(800, 600),
          builder: (BuildContext context) {
            return const MaterialApp(home: MyApp());
          })
    ]));

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Plugin example app'),
      ),
      body: Center(
        child: Text('Platform: ${Platform.operatingSystem}\n'),
      ),
    );
  }
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets(
      'Test whether or not createRegularWindow will throw on this platform',
      (WidgetTester tester) async {
    startApp();
    await tester.pumpAndSettle();
  });
}
