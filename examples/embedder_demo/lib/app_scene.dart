// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A typical app made of common Material widgets, to compare embedders on
// rendering that is closer to real apps than the galaxy.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

const List<String> _kNames = <String>[
  'Ada Lovelace',
  'Alan Turing',
  'Grace Hopper',
  'Linus Torvalds',
  'Margaret Hamilton',
  'Dennis Ritchie',
  'Barbara Liskov',
  'Ken Thompson',
  'Frances Allen',
  'Donald Knuth',
];

const List<String> _kMessages = <String>[
  'Are we still on for the release review tomorrow morning?',
  'I pushed the fix for the flaky test, can you take a look?',
  'The photos from the weekend trip are up, the lake was amazing.',
  'Lunch at the usual place? I can book a table for twelve.',
  'Reminder: the design sync moved to Thursday afternoon.',
  'Thanks for the help with the build, it works on my machine now.',
  'Here are the slides for the talk, feedback very welcome.',
];

const List<String> _kChips = <String>['All', 'Unread', 'Photos', 'Starred', 'Work'];

const List<List<Color>> _kGradients = <List<Color>>[
  <Color>[Color(0xFF355C7D), Color(0xFF6C5B7B), Color(0xFFC06C84)],
  <Color>[Color(0xFF0F2027), Color(0xFF203A43), Color(0xFF2C5364)],
  <Color>[Color(0xFFFF7E5F), Color(0xFFFEB47B)],
  <Color>[Color(0xFF11998E), Color(0xFF38EF7D)],
];

const List<IconData> _kPhotoIcons = <IconData>[
  Icons.landscape,
  Icons.beach_access,
  Icons.wb_sunny,
  Icons.forest,
];

/// A typical app. With [updates] small parts of it change every second, as
/// when content arrives. With [scrolling] it also scrolls continuously and
/// opens and closes a page.
class AppScene extends StatefulWidget {
  const AppScene({super.key, required this.updates, required this.scrolling});

  final bool updates;
  final bool scrolling;

  @override
  State<AppScene> createState() => _AppSceneState();
}

class _AppSceneState extends State<AppScene> with SingleTickerProviderStateMixin {
  final GlobalKey<NavigatorState> _navigatorKey = GlobalKey<NavigatorState>();
  final ScrollController _scrollController = ScrollController();
  late final Ticker _scrollTicker = createTicker(_onScrollTick);
  Duration _lastScrollTick = Duration.zero;
  Timer? _updateTimer;
  Timer? _pageTimer;
  bool _pageOpen = false;

  // Changed by the updates.
  int _step = 0;

  @override
  void initState() {
    super.initState();
    _updateActivity();
  }

  @override
  void didUpdateWidget(AppScene oldWidget) {
    super.didUpdateWidget(oldWidget);
    _updateActivity();
  }

  @override
  void dispose() {
    _updateTimer?.cancel();
    _pageTimer?.cancel();
    _scrollTicker.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  void _updateActivity() {
    if (widget.updates) {
      _updateTimer ??= Timer.periodic(const Duration(seconds: 1), (_) => setState(() => _step++));
    } else {
      _updateTimer?.cancel();
      _updateTimer = null;
    }

    if (widget.scrolling) {
      if (!_scrollTicker.isActive) {
        _lastScrollTick = Duration.zero;
        _scrollTicker.start();
      }
      _pageTimer ??= Timer.periodic(const Duration(milliseconds: 2500), (_) => _togglePage());
    } else {
      _scrollTicker.stop();
      _pageTimer?.cancel();
      _pageTimer = null;
      if (_pageOpen) {
        // This can be called while building, when the navigator can't change.
        SchedulerBinding.instance.addPostFrameCallback((_) {
          if (_pageOpen) {
            _togglePage();
          }
        });
      }
    }
  }

  void _onScrollTick(Duration elapsed) {
    final double seconds = (elapsed - _lastScrollTick).inMicroseconds / 1e6;
    _lastScrollTick = elapsed;
    if (!_scrollController.hasClients || !_scrollController.position.hasContentDimensions) {
      return;
    }
    final ScrollPosition position = _scrollController.position;
    double offset = position.pixels + 400 * seconds;
    if (offset >= position.maxScrollExtent) {
      offset = 0;
    }
    _scrollController.jumpTo(offset);
  }

  void _togglePage() {
    final NavigatorState? navigator = _navigatorKey.currentState;
    if (navigator == null) {
      return;
    }
    if (_pageOpen) {
      navigator.pop();
    } else {
      navigator.push(MaterialPageRoute<void>(builder: (_) => const _DetailPage()));
    }
    _pageOpen = !_pageOpen;
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      navigatorKey: _navigatorKey,
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF2EE6A8),
          brightness: Brightness.dark,
        ),
      ),
      home: _HomePage(step: _step, syncing: widget.updates, scrollController: _scrollController),
    );
  }
}

class _HomePage extends StatelessWidget {
  const _HomePage({required this.step, required this.syncing, required this.scrollController});

  final int step;
  final bool syncing;
  final ScrollController scrollController;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Inbox'),
        actions: <Widget>[
          IconButton(icon: const Icon(Icons.search), onPressed: () {}),
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: Badge.count(
              count: 3 + step % 7,
              child: const Icon(Icons.notifications_outlined),
            ),
          ),
        ],
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(4),
          child: syncing ? const LinearProgressIndicator() : const SizedBox(height: 4),
        ),
      ),
      body: ListView.builder(
        controller: scrollController,
        itemCount: 300,
        itemBuilder: (BuildContext context, int index) {
          if (index == 0) {
            return _Header(step: step);
          }
          if (index % 6 == 1) {
            return _PhotoCard(index: index, liked: (step + index) % 4 == 0);
          }
          return _MessageTile(index: index, unread: (step + index) % 5 == 0);
        },
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: () {},
        icon: const Icon(Icons.edit),
        label: const Text('Compose'),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: (step ~/ 3) % 3,
        destinations: const <Widget>[
          NavigationDestination(icon: Icon(Icons.inbox), label: 'Inbox'),
          NavigationDestination(icon: Icon(Icons.photo_library_outlined), label: 'Photos'),
          NavigationDestination(icon: Icon(Icons.person_outline), label: 'Profile'),
        ],
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.step});

  final int step;

  @override
  Widget build(BuildContext context) {
    final ColorScheme colors = Theme.of(context).colorScheme;
    final double storage = 0.3 + 0.1 * (step % 6);
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          Wrap(
            spacing: 8,
            runSpacing: 4,
            children: <Widget>[
              for (int i = 0; i < _kChips.length; i++)
                FilterChip(
                  label: Text(_kChips[i]),
                  selected: i == step % _kChips.length,
                  onSelected: (_) {},
                ),
            ],
          ),
          const SizedBox(height: 8),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: <Widget>[
                  Row(
                    children: <Widget>[
                      const Expanded(child: Text('Sync over mobile data')),
                      Switch(value: step.isOdd, onChanged: (_) {}),
                    ],
                  ),
                  Text(
                    'Storage ${(storage * 100).round()}% used',
                    style: TextStyle(color: colors.onSurfaceVariant),
                  ),
                  const SizedBox(height: 6),
                  TweenAnimationBuilder<double>(
                    tween: Tween<double>(end: storage),
                    duration: const Duration(milliseconds: 400),
                    curve: Curves.easeInOut,
                    builder: (BuildContext context, double value, _) => LinearProgressIndicator(
                      value: value,
                      borderRadius: BorderRadius.circular(4),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Avatar extends StatelessWidget {
  const _Avatar({required this.index});

  final int index;

  @override
  Widget build(BuildContext context) {
    final String name = _kNames[index % _kNames.length];
    return CircleAvatar(
      backgroundColor: Colors.primaries[index % Colors.primaries.length],
      foregroundColor: Colors.white,
      child: Text(name.split(' ').map((String part) => part[0]).join()),
    );
  }
}

class _MessageTile extends StatelessWidget {
  const _MessageTile({required this.index, required this.unread});

  final int index;
  final bool unread;

  @override
  Widget build(BuildContext context) {
    final ColorScheme colors = Theme.of(context).colorScheme;
    return ListTile(
      leading: _Avatar(index: index),
      title: Text(
        _kNames[index % _kNames.length],
        style: TextStyle(fontWeight: unread ? FontWeight.bold : FontWeight.normal),
      ),
      subtitle: Text(
        _kMessages[index % _kMessages.length],
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
      ),
      isThreeLine: true,
      trailing: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.end,
        children: <Widget>[
          Text('${9 + index % 12}:${(index * 7 % 60).toString().padLeft(2, '0')}'),
          const SizedBox(height: 4),
          AnimatedOpacity(
            opacity: unread ? 1 : 0,
            duration: const Duration(milliseconds: 300),
            child: Icon(Icons.circle, size: 10, color: colors.primary),
          ),
        ],
      ),
    );
  }
}

class _PhotoCard extends StatelessWidget {
  const _PhotoCard({required this.index, required this.liked});

  final int index;
  final bool liked;

  @override
  Widget build(BuildContext context) {
    final int variant = index ~/ 6;
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      elevation: 3,
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          ListTile(
            leading: _Avatar(index: index),
            title: Text(_kNames[index % _kNames.length]),
            subtitle: const Text('Shared a photo'),
            trailing: const Icon(Icons.more_vert),
          ),
          Container(
            height: 160,
            decoration: BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: _kGradients[variant % _kGradients.length],
              ),
            ),
            child: Icon(
              _kPhotoIcons[variant % _kPhotoIcons.length],
              size: 72,
              color: Colors.white70,
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4),
            child: Row(
              children: <Widget>[
                IconButton(
                  onPressed: () {},
                  icon: AnimatedSwitcher(
                    duration: const Duration(milliseconds: 250),
                    transitionBuilder: (Widget child, Animation<double> animation) =>
                        ScaleTransition(scale: animation, child: child),
                    child: Icon(
                      liked ? Icons.favorite : Icons.favorite_border,
                      key: ValueKey<bool>(liked),
                      color: liked ? Colors.redAccent : null,
                    ),
                  ),
                ),
                IconButton(onPressed: () {}, icon: const Icon(Icons.chat_bubble_outline)),
                IconButton(onPressed: () {}, icon: const Icon(Icons.share_outlined)),
                const Spacer(),
                Text('${12 + index * 3 + (liked ? 1 : 0)} likes'),
                const SizedBox(width: 12),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _DetailPage extends StatelessWidget {
  const _DetailPage();

  @override
  Widget build(BuildContext context) {
    final TextTheme text = Theme.of(context).textTheme;
    return Scaffold(
      appBar: AppBar(title: const Text('Weekend trip')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: <Widget>[
          ClipRRect(
            borderRadius: BorderRadius.circular(16),
            child: Container(
              height: 200,
              decoration: BoxDecoration(gradient: LinearGradient(colors: _kGradients[0])),
              child: const Icon(Icons.landscape, size: 96, color: Colors.white70),
            ),
          ),
          const SizedBox(height: 16),
          Text('Lake district', style: text.headlineSmall),
          const SizedBox(height: 8),
          for (final String message in _kMessages) ...<Widget>[
            Text(message, style: text.bodyLarge),
            const SizedBox(height: 8),
          ],
          const SizedBox(height: 8),
          Row(
            children: <Widget>[
              FilledButton.icon(
                onPressed: () {},
                icon: const Icon(Icons.download),
                label: const Text('Download'),
              ),
              const SizedBox(width: 8),
              OutlinedButton(onPressed: () {}, child: const Text('Share')),
            ],
          ),
        ],
      ),
    );
  }
}
