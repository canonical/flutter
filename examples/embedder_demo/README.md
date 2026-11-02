# Embedder demo

Compares the resource usage of the GTK Linux embedder on `master` with the new
Linux embedder on this branch, using the same app.

The app shows one of two scenes, and its own resource usage, and that of the
compositor, in a panel that updates each second:

- Frame rate and average raster time.
- CPU, where 100% is one core.
- GPU time and GPU memory, from `/proc/<pid>/fdinfo` (where the driver
  reports them).
- RSS and PSS memory, and the number of threads.
- Time from the process starting to the first frame.
- Compositor (e.g. `gnome-shell`) CPU and GPU.

The scene can be changed with the buttons at the bottom, the `G` and `A` keys,
or by passing `--scene=galaxy|app`:

- **Galaxy**: an animated galaxy, to stress rendering.
- **App**: a typical Material app, with an app bar, a list of messages and
  photo cards, chips, a switch, progress bars, a floating action button and a
  navigation bar.

The workload can be changed with the buttons at the bottom, the `1`, `2` and `3`
keys, or by passing `--mode=idle|light|heavy`:

| Workload  | Galaxy               | App                                                                                          |
| --------- | -------------------- | -------------------------------------------------------------------------------------------- |
| **Idle**  | The galaxy is paused | Static                                                                                       |
| **Light** | 12,000 stars         | An indeterminate progress bar, and small updates each second (chips, switch, badge, storage bar, likes, nav bar) |
| **Heavy** | 60,000 stars         | The list scrolls continuously, and a detail page is pushed or popped every 2.5 seconds      |

The scene and workload can also be set with the `DEMO_SCENE` and `DEMO_MODE`
environment variables.

## Building

The profile engine needs to be configured once, in `engine/src`:

```sh
./flutter/tools/gn --runtime-mode=profile --ccache --no-lto
```

Then build both versions:

```sh
./build_demo.sh
```

This checks out `master` (the commit this branch is based on), builds the GTK
engine and the app, then checks out this branch again and builds the new engine
and the app. Your working tree must have no uncommitted changes. The bundles are
copied to `build/demo/gtk` and `build/demo/new`.

## Running

```sh
./run_gtk.sh
./run_new.sh
```

Both accept `--scene=galaxy|app` and `--mode=idle|light|heavy`.

Both windows are 900×720, so they fit side by side on a 1920 pixel wide (logical)
display. The size is set in `linux/runner/main.cc`, and `build_demo.sh` uses the
same size for the GTK version.
