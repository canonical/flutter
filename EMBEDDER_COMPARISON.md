# Linux embedder comparison

This compares three embedders:

- **New**: `flutter_linux`, the new embedder on branch `linux-next-test`.
  Wayland only, C++, no toolkit.
- **Old**: `flutter_linux_gtk`, the GTK embedder on `master` at `56f0dcbb331`,
  which is the base of this branch.
- **Sony**: [sony/flutter-embedded-linux](https://github.com/sony/flutter-embedded-linux)
  at `1653fa6` (2025-05-09). Sony was analysed from its source, not run.

## Summary

- **Performance.** The new embedder uses less of everything at the same frame
  rate. Startup takes less than half the time. App CPU is 45–75% lower and app
  GPU about 45–70% lower. Memory is 50 MiB less RSS, 20 MiB less PSS and 50–90
  MiB less GPU memory. The compositor (gnome-shell) also uses about 40–45% less
  CPU when animating.
- **Background rendering.** When the window can't be seen, the new embedder stops
  drawing. The old one keeps rendering at 60 fps; see the note under Benchmarks.
- **Dependencies.** The new embedder loads 16 shared libraries, compared with 68
  for GTK. The engine library is a similar size; the new one is 0.2 MB larger
  because it includes AT-SPI/D-Bus and title text layout.
- **Features.** The new embedder leads on Wayland presentation, input, multiple
  windows and accessibility. It is still missing several platform channels the
  old one has: clipboard, settings, lifecycle, locales, textures and system
  sounds.
- **Sony.** Sony is a portable kiosk/embedded embedder: Wayland, X11 and DRM, chosen
  at compile time. On desktop Wayland it is clearly behind both of ours. It
  renders through `wl_egl_window`/`eglSwapBuffers`, has no fractional scaling, no
  accessibility, a single window and vsync off by default. It also needs a forked
  `flutter-elinux` tool, and the last commit was May 2025. It is not a good base
  for an upstream desktop embedder. Its DRM/GBM backend is its main unique value.

## Code size

| | Old (GTK) | New | Sony |
|---|---:|---:|---:|
| Embedder C/C++ files | 244 | 97 | ~110 |
| Lines, all | 52,468 | 25,010 | ~24.0k (incl. ~8.8k copied from Flutter) |
| Lines, excluding tests | 27,645 | 21,218 | 15,212 embedder + ~8.8k copied |
| Shared code also used | `common` (input, switches) | `common` C++ client wrapper, input, json (~3k lines, shared with Windows) | copies of `common` and the client wrapper |
| Object code (profile, `size`) | ~312 KB | ~495 KB | n/a |
| `libflutter_linux*.so` (profile, stripped) | 18.30 MB | 18.48 MB | uses a separately built `libflutter_engine.so` |
| Shared libraries loaded by the embedder | 68 (GTK, GLib, Pango, Cairo, ATK, X11…) | 16 (wayland-client, xkbcommon, EGL, GLES, gbm, drm, libc…) | EGL, GLES, xkbcommon, plus wayland-client/cursor/egl, or X11, or drm/gbm/libinput/udev/systemd |
| App template, lines | 403 (`main.cc` plus a GTK `MyApplication` class) | 294 (a single `main.cc`) | provided by the `flutter-elinux` tool |

Notes:

- The new embedder's object code is larger mainly because of things GTK and ATK
  used to provide:
  - AT-SPI and the D-Bus implementation: about 170 KB and 5.2k lines;
  - client-side decorations with title text layout: 1.7k lines;
  - the Wayland protocol code.
- Lines in the new embedder by area, excluding tests:

  | Area | Lines |
  |---|---:|
  | Accessibility and D-Bus | 5.2k |
  | Wayland | 4.5k |
  | Input and text | 2.9k |
  | Engine, view and event loop | 4.0k |
  | Rendering | 1.6k |
  | Decorations | 1.7k |

## Benchmarks

### Method

**Setup**

- Both embedders were built locally as `host_profile` (no LTO) from the same
  engine sources.
- Both render with Impeller using SDFs, the default on `master`. I changed the
  new embedder to also default to Impeller (see below).
- Machine: AMD GPU (amdgpu) with a 3840×2160 display at 60 Hz.
- Desktop: GNOME Shell on Wayland, at scale 2.
- Window: 1280×720 logical, so the view is 2560×1440 physical.

**App**

The same app was used for both (`/tmp/bench_src/main.dart`), in three scenarios:

- `idle`: static text.
- `light`: a single `CircularProgressIndicator`.
- `heavy`: 200 rotating, shadowed, rounded tiles with text.

Each run warms up for 3 s and then measures for 10 s.

**What was measured**

The harness is `/tmp/bench_src/harness.py`. It measures:

| Metric | How |
|---|---|
| Startup | Time from exec until the first `FrameTiming` |
| CPU | `utime` + `stime` from `/proc/<pid>/stat` |
| GPU time | `drm-engine-*` in `/proc/<pid>/fdinfo` |
| GPU memory | resident VRAM + GTT |
| RSS and PSS | `smaps_rollup` |
| FPS and frame times | `FrameTiming`, measured by the app |

The CPU and GPU figures were taken for both the app and gnome-shell.

**Runs**

- The table shows medians of 3–4 interleaved runs.
- A run taken while the screen was off has been excluded.
- gnome-shell figures include other desktop activity, so they are noisy (±3–5%).

### Results

| Scenario | Metric | Old (GTK) | New | Change |
|---|---|---:|---:|---:|
| all | Startup to first frame (ms) | 170–220 | 87–100 | **−50–60%** |
| idle | App CPU / GPU % | 0.0 / 0.0 | 0.0 / 0.0 | – |
| idle | RSS / PSS (MiB) | 187 / 95 | 137 / 77 | −50 / −18 |
| idle | GPU memory (MiB) | 208 | 120 | −88 |
| idle | Threads | 40 | 30 | −10 |
| light | FPS | 60.1 | 60.1 | – |
| light | App CPU % | 22.8 | 5.4 | **−76%** |
| light | App GPU % | 6.1 | 1.7 | **−72%** |
| light | gnome-shell CPU % / GPU % | 20.0 / 13.3 | 11.8 / 17.2 | −41% / +29% |
| light | Raster avg / p99 (ms) | 0.8 / 1.2 | 0.5 / 1.4 | |
| light | RSS / PSS / GPU memory (MiB) | 195 / 103 / 208 | 145 / 86 / 156 | |
| heavy | FPS | 60.2 | 60.0 | – |
| heavy | App CPU % | 74.8 | 42.0 | **−44%** |
| heavy | App GPU % | 12.0 | 6.7 | **−44%** |
| heavy | gnome-shell CPU % / GPU % | 20.2 / 12.7 | 10.8 / 13.4 | −47% / ~ |
| heavy | Build avg (ms) | 4.4 | 3.8 | |
| heavy | Raster avg / p99 (ms) | 3.1 / 4.2 | 2.2 / 3.1 | −29% |
| heavy | RSS / PSS / GPU memory (MiB) | 212 / 120 / 225 | 158 / 98 / 156 | |

### Why the new embedder is faster

**App CPU and GPU**

The old embedder on Wayland renders each frame twice:

- Flutter renders into an FBO.
- That frame is then blitted or drawn into a `wl_egl_window` subsurface.
- The result is presented with `eglSwapBuffers`, and GTK still runs its own frame
  clock and drawing alongside.

The new embedder presents the engine's own backing stores directly to the
compositor, with no copy:

- GBM dmabufs, attached with `zwp_linux_dmabuf_v1`;
- explicit sync through `linux-drm-syncobj`;
- `fifo-v1`;
- `presentation-time`, which feeds a vsync predictor.

**gnome-shell CPU**

GTK commits a parent surface every frame as well as the subsurface. The new
embedder commits a single surface. The decorations are subsurfaces that are only
redrawn when they change.

**gnome-shell GPU**

The light scenario is slightly higher for the new embedder (+29%). This is within
desktop noise across runs. It may also be that GNOME can't scan out a flipped
(`FLIPPED_180`) buffer directly; that is worth checking with
`MUTTER_DEBUG=kms`.

**Background rendering**

In the excluded screen-off run:

- The old embedder kept rendering at 60 fps, using 55% CPU on the heavy scene.
  It has no `vsync_callback`, so it relies on the engine's timer-based fallback.
- The new embedder correctly dropped to about 0 fps, because it waits for frame
  callbacks.

### Impeller default (changed in this work)

At first the new embedder only enabled Impeller with
`--enable-impeller`, so it was rendering with Skia. Environment switches are
compiled out of profile and release builds, so it couldn't be turned on either.
Skia was about 4× slower to raster the heavy scene (11 ms vs 2.3 ms).

The new embedder now matches `master`:

- Impeller with `--impeller-use-sdfs` is the default.
- `--enable-impeller=false` turns it off.

This is uncommitted. The window title text is not affected. It is laid out with
SkParagraph and rasterized on the CPU into an `SkBitmap`, which is independent of
the engine's GPU backend.

## Features

✅ = supported, ⚠️ = partial, ❌ = not supported.

### Platform and rendering

| Feature | Old (GTK) | New | Sony |
|---|---|---|---|
| Wayland | ✅ via GTK | ✅ | ✅ |
| X11 | ✅ via GTK | ❌ | ✅ |
| DRM/KMS without a compositor | ❌ | ❌ | ✅ GBM and EGLStream, chosen at compile time |
| Toolkit-free | ❌ GTK 3 | ✅ | ✅ |
| Embedder API rendering path | `FlutterCompositor` | `FlutterCompositor` | simple GL renderer config (`make_current`/`present`) |
| Frame presentation on Wayland | FBO, then a copy into a `wl_egl_window` subsurface via `eglSwapBuffers` | zero-copy GBM dmabuf, `linux-dmabuf` v4 feedback | `wl_egl_window` + `eglSwapBuffers` |
| Explicit sync (`linux-drm-syncobj`) | ❌ | ✅ | ❌ |
| `fifo-v1` / `presentation-time` | ❌ / ❌ | ✅ / ✅ | ❌ / ✅ |
| Vsync driven by the compositor | ❌ timer fallback, renders while hidden | ✅ frame callbacks + presentation times | ⚠️ off by default (`ENABLE_VSYNC=OFF`) |
| Software rendering fallback | ✅ (`fl_compositor_software`) | ⚠️ GL rendering copied to `wl_shm` when there is no dmabuf | ❌ |
| Impeller | ✅ default (GLES, SDF) | ✅ default (GLES, SDF) | ⚠️ accepts the switch, GL only |
| Vulkan | ❌ | ❌ | ❌ |
| External textures | ✅ GL and pixel buffer | ❌ | ✅ GL, EGLImage and pixel buffer |
| Platform views | ❌ | ❌ | ⚠️ partial |

### Windows and displays

| Feature | Old (GTK) | New | Sony |
|---|---|---|---|
| HiDPI | ✅ integer (GTK 3) | ✅ fractional (`fractional-scale-v1` + viewporter), integer fallback | ⚠️ integer `wl_output.scale` only |
| Display updates (`NotifyDisplayUpdate`) and display ID per view | ✅ | ✅ `wl_output` + `xdg-output`, hotplug | ❌ |
| Multiple views and windows | ⚠️ windowing API, top-level windows | ✅ top-levels, dialogs (`xdg-dialog`), popups and tooltips, sized to content, constraints | ❌ single view |
| Multiple engines | ✅ | ✅ sharing one display connection | ❌ |
| Decorations | ✅ GTK header bar | ✅ client-side (title text, buttons, shadows, rounded corners) or server-side via `xdg-decoration` | ⚠️ client-side drawn with GL shaders (title, buttons, resize) |
| Window activation (`xdg-activation`) | ✅ via GTK | ✅ | ❌ |

### Input

| Feature | Old (GTK) | New | Sony |
|---|---|---|---|
| Keyboard | ✅ GDK | ✅ xkbcommon, embedder key events, key repeat | ⚠️ xkbcommon, **no key repeat** |
| Text input / IME | ✅ GtkIMContext | ✅ `text-input-v3` | ⚠️ text-input v1 and v3; many client options ignored |
| Mouse cursor | ✅ GDK cursor names | ✅ `cursor-shape-v1` | ⚠️ `wayland-cursor` themes, a few shapes |
| Scrolling | ✅ smooth and discrete | ⚠️ smooth and discrete (no `axis_value120`) | ⚠️ basic axis events |
| Touchpad pinch and rotate | ✅ GTK gestures | ✅ `pointer-gestures` | ❌ |
| Touch | ✅ | ✅ | ✅ |
| Tablet / stylus | ❌ | ❌ | ❌ |

### Platform channels and accessibility

| Feature | Old (GTK) | New | Sony |
|---|---|---|---|
| Clipboard | ✅ | ❌ | ✅ `wl_data_device` |
| `flutter/platform`: exit, `SystemNavigator.pop`, sounds | ✅ | ❌ | ⚠️ exit and clipboard |
| `flutter/settings` (dark mode, text scale, 24-hour time) | ✅ portal and GSettings | ❌ | ⚠️ hard-coded light theme |
| Lifecycle | ✅ | ❌ | ✅ |
| Locales | ✅ | ❌ | ❌ |
| Accessibility | ✅ ATK (through GTK's AT-SPI bridge) | ✅ AT-SPI directly over its own D-Bus code, with actions, text, values, selection, relations and dialogs | ❌ |

### API and maintenance

| Feature | Old (GTK) | New | Sony |
|---|---|---|---|
| Plugin API | GObject C (`FlValue`, `FlMethodChannel`…) | C++ client wrapper shared with Windows, over a C ABI | C API + C++ client wrapper (Windows style) |
| Build and tooling | upstream `flutter` tool | upstream `flutter` tool | forked `flutter-elinux` tool and engine artifacts |
| Upstream and maintenance | upstream | upstream branch | third-party; last commit 2025-05; 7 TODOs in the source |

## Sony embedder notes

These are from source analysis of `/tmp/sony`.

- **Structure.** The backend is selected at compile time with the `BACKEND_TYPE`
  CMake option (`WAYLAND`, `X11`, `DRM-GBM` or `DRM-EGLSTREAM`). So one binary
  can't fall back from Wayland to X11 or DRM.
- **Rendering.**
  - It uses the legacy OpenGL renderer callbacks, so the engine renders straight
    into the EGL window surface. That is efficient for a single fullscreen view,
    but it rules out multiple views and the compositor-backed presentation used
    by the new embedder.
  - Damage-region support is disabled because of flicker (`CMakeLists.txt`).
- **Vsync.**
  - It is off by default.
  - On Wayland it uses `presentation-time` feedback, or frame callbacks when that
    isn't available.
  - The DRM and X11 paths are TODO.
- **Scaling.** Integer only, set with `wl_surface_set_buffer_scale`. It uses the
  largest scale of the outputs the surface has entered. There is no
  fractional-scale or viewporter support.
- **Input.**
  - No key repeat: `repeat_info` is only logged.
  - No pointer gestures and no tablet support.
  - Pointer events carry a TODO about view IDs.
- **Settings.** Brightness is hard-coded to light, and there is no portal or
  GSettings integration.
- **Unique strengths.**
  - DRM/GBM and EGLStream backends with libinput, for kiosks and devices with no
    compositor.
  - External textures, including EGLImage.
  - Partial platform views.
- **Assessment.** As a desktop Linux embedder for upstream Flutter it would need
  most of what the new embedder already has: zero-copy presentation, fractional
  scaling, accessibility, multi-window and multi-engine support, and newer
  protocols. It also has a less maintainable structure: compile-time backends
  and copies of Flutter's `common` code. If DRM/KMS support is wanted, adding a
  KMS backend to the new embedder looks like less work. Its GBM buffers,
  explicit sync and frame scheduling already fit atomic KMS page-flips.

## Gaps in the new embedder

These are ordered by how much they affect apps:

1. **`flutter/platform`**: clipboard (`wl_data_device`), exit and
   `requestAppExit`, `SystemNavigator.pop`, sounds.
2. **`flutter/settings`**: dark mode, text scale, 24-hour time, accent colour,
   from the XDG settings portal over the existing D-Bus code.
3. **Lifecycle and locales.**
4. **External textures**, and later platform views; dmabuf import fits the
   existing compositor.
5. **Drag and drop, primary selection, high-resolution scrolling
   (`axis_value120`, `wl_seat` v8+), tablet input.**
6. **Vulkan / Impeller-Vulkan.**
7. **Optional:** a DRM/KMS backend for embedded devices, which is Sony's niche.

## Reproducing

```sh
# Engines (profile, no LTO): in engine/src
./flutter/tools/gn --runtime-mode=profile --ccache --no-lto
OUT=out/host_profile ./build.sh flutter/shell/platform/linux:linux \
  flutter/shell/platform/linux:flutter_linux_archive gen_snapshot \
  flutter_patched_sdk dart-sdk
# On master, the GTK archive target is flutter/shell/platform/linux:flutter_gtk.

# App, built with each branch's flutter tool
flutter build linux --profile --local-engine=host_profile --local-engine-host=host_profile

# Measure (modes: idle, light, heavy)
python3 /tmp/bench_src/harness.py build/linux/x64/profile/bundle/bench heavy 3
```
