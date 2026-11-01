# Linux embedder comparison

This compares five embedders:

- **New**: `flutter_linux`, the new embedder on branch `linux-next-test`.
  C++, no toolkit. Wayland first, with X11 and DRM/KMS backends chosen at run
  time.
- **Old**: `flutter_linux_gtk`, the GTK embedder on `master` at `56f0dcbb331`,
  which is the base of this branch.
- **Sony**: [sony/flutter-embedded-linux](https://github.com/sony/flutter-embedded-linux)
  at `1653fa6` (2025-05-09).
- **ivi-homescreen**: [toyota-connected/ivi-homescreen](https://github.com/toyota-connected/ivi-homescreen)
  at `d9c105e` (2026-10-18).
- **flutter-pi**: [ardera/flutter-pi](https://github.com/ardera/flutter-pi)
  at `8b86c52` (2026-09-17).

Only Old and New were benchmarked, and only on Wayland; the benchmarks predate
the X11 and KMS backends. The three third-party embedders were analysed
from their source, not run.

## Summary

- **Performance.** The new embedder uses less of everything at the same frame
  rate. Startup takes less than half the time. App CPU is 45–75% lower and app
  GPU about 45–70% lower. Memory is 50 MiB less RSS, 20 MiB less PSS and 50–90
  MiB less GPU memory. The compositor (gnome-shell) also uses about 40–45% less
  CPU when animating.
- **Background rendering.** When the window can't be seen, the new embedder stops
  drawing. The old one keeps rendering at 60 fps; see the note under Benchmarks.
- **Dependencies.** The new embedder links 22 shared libraries (including the
  XCB ones for X11), compared with 68 for GTK. libxkbcommon-x11, libinput and
  libudev are only loaded when the X11 or KMS backend needs them. The engine
  library is a similar size; the new one was 0.2 MB larger before the X11 and
  KMS backends, because it includes AT-SPI/D-Bus and title text layout.
- **Backends.** One binary picks Wayland, X11 or DRM/KMS at run time
  (`FLUTTER_LINUX_BACKEND`, otherwise Wayland if `WAYLAND_DISPLAY` is set, then
  X11 if `DISPLAY` is set, then KMS). The backends share the engine, input,
  text, accessibility and compositor code behind backend-neutral `Display` and
  `Window` interfaces. X11 and KMS are simpler than Wayland: no platform views,
  and KMS has a single undecorated fullscreen window.
- **Features.** The new embedder leads on Wayland presentation, input, multiple
  windows and accessibility. It now matches the old one's platform channels
  (clipboard, settings, lifecycle, locales and the system bell), but has no
  GSettings fallback for settings.
- **Sony.** Sony is a portable kiosk/embedded embedder: Wayland, X11 and DRM, chosen
  at compile time. On desktop Wayland it is clearly behind both of ours. It
  renders through `wl_egl_window`/`eglSwapBuffers`, has no fractional scaling, no
  accessibility, a single window and vsync off by default. It also needs a forked
  `flutter-elinux` tool, and the last commit was May 2025. It is not a good base
  for an upstream desktop embedder. Its DRM/GBM backend was its main unique
  value, and the new embedder now has one too.
- **ivi-homescreen.** This is the strongest third-party option, and it is very
  actively maintained. It targets automotive and embedded systems. It has many
  backends chosen at run time: Wayland, DRM/KMS and headless, each with EGL or
  Vulkan, plus software. It also has platform views, external textures,
  multiple displays and multiple engines, and AccessKit-based accessibility.
  But it is about 4× the size of the new embedder (93k lines plus 22k shared). Its
  EGL path still uses `wl_egl_window`/`eglSwapBuffers`, and only its Vulkan path
  presents dmabufs. It has no decorations, pinch/rotate gestures, or clipboard
  (TODO). It is built around TOML configuration and shell roles such as AGL and
  IVI rather than desktop app conventions.
- **flutter-pi.** This is a DRM/KMS-only embedder written in C for Raspberry Pi
  and kiosks. It cannot run under a Wayland compositor, so it is not a candidate
  for desktop use. Its KMS design is excellent, though: `FlutterCompositor`
  layers mapped onto hardware planes with atomic commits, a hardware cursor
  plane, and libinput. It is a good reference for extending the new embedder's
  simpler KMS backend.

## Code size

| | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---:|---:|---:|---:|---:|
| Language | C (GObject) | C++ | C++ | C++ | C |
| Lines, all | 52,468 | 39,588 | ~24.0k (incl. ~8.8k copied from Flutter) | ~146k (93k `shell/` + 22k `shared/` + 31k tests) | ~39k |
| Lines, excluding tests | 27,645 | 34,072; Wayland 7.8k, X11 3.8k, KMS 2.7k | 15,212 embedder + ~8.8k copied | ~115k; backends 48k, bundled plugins 12k, Wayland 5k | ~39k; core 27k, plugins 9k, util 3k |
| Shared code also used | `common` (input, switches) | `common` C++ client wrapper, input, json (~3k lines, shared with Windows) | copies of `common` and the client wrapper | own C plugin ABI with C++ wrappers | none |
| Object code (profile, `size`) | ~312 KB | ~495 KB (before X11 and KMS) | n/a | n/a | n/a |
| `libflutter_linux*.so` (profile, stripped) | 18.30 MB | 18.48 MB (before X11 and KMS) | uses a separately built `libflutter_engine.so` | uses `libflutter_engine.so` | uses `libflutter_engine.so` |
| Shared libraries loaded by the embedder | 68 (GTK, GLib, Pango, Cairo, ATK, X11…) | 22 linked (wayland-client, xcb and its extensions, xkbcommon, EGL, GLES, gbm, drm, libc…); xkbcommon-x11, libinput and libudev loaded on demand | EGL, GLES, xkbcommon, plus wayland-client/cursor/egl, or X11, or drm/gbm/libinput/udev/systemd | wayland, EGL/GLES, Vulkan, drm/gbm, libinput/udev/libseat, xkbcommon, AccessKit (optional), pugixml, fmt, toml++ | drm, gbm, libinput, udev, systemd, xkbcommon; EGL/GLES, Vulkan, GStreamer optional |
| App template, lines | 403 (`main.cc` plus a GTK `MyApplication` class) | 294 (a single `main.cc`) | provided by the `flutter-elinux` tool | none; app bundles plus `config.toml` | provided by `flutterpi_tool` |

Notes:

- The new embedder's object code is larger mainly because of things GTK and ATK
  used to provide:
  - AT-SPI and the D-Bus implementation: about 170 KB and 5.2k lines;
  - client-side decorations with title text layout: 1.7k lines;
  - the Wayland protocol code.
- Lines in the new embedder by area, excluding tests and generated protocol
  code:

  | Area | Lines |
  |---|---:|
  | Engine, view, public API, channels and event loop | 7.9k |
  | Wayland (excluding decorations) | 6.2k |
  | Accessibility and D-Bus | 5.5k |
  | X11 | 3.8k |
  | Rendering, textures and platform views | 3.5k |
  | Input and text | 3.0k |
  | KMS | 2.7k |
  | Decorations | 1.6k |

## Benchmarks

### Method

**Setup**

- Both embedders were built locally as `host_profile` (no LTO) from the same
  engine sources.
- Both render with Impeller using SDFs, which is the default for both.
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
| light | Raster avg / p99 (ms) | 0.8 / 1.2 | 0.5 / 1.4 | −38% / +17% |
| light | RSS / PSS (MiB) | 195 / 103 | 145 / 86 | −50 / −17 |
| light | GPU memory (MiB) | 208 | 156 | −52 |
| heavy | FPS | 60.2 | 60.0 | – |
| heavy | App CPU % | 74.8 | 42.0 | **−44%** |
| heavy | App GPU % | 12.0 | 6.7 | **−44%** |
| heavy | gnome-shell CPU % / GPU % | 20.2 / 12.7 | 10.8 / 13.4 | −47% / ~ |
| heavy | Build avg (ms) | 4.4 | 3.8 | −14% |
| heavy | Raster avg / p99 (ms) | 3.1 / 4.2 | 2.2 / 3.1 | −29% / −26% |
| heavy | RSS / PSS (MiB) | 212 / 120 | 158 / 98 | −54 / −22 |
| heavy | GPU memory (MiB) | 225 | 156 | −69 |

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

## Features

✅ = supported, ⚠️ = partial, ❌ = not supported.

### Platform and rendering

| Feature | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---|---|---|---|---|
| Wayland | ✅ via GTK | ✅ | ✅ | ✅ xdg, AGL, IVI and simple shells | ❌ |
| X11 | ✅ via GTK | ✅ XCB; DRI3 or MIT-SHM pixmaps shown with Present | ✅ | ❌ | ❌ |
| DRM/KMS without a compositor | ❌ | ✅ atomic KMS with render fences and legacy fallback, logind session, libinput; one fullscreen window on one output | ✅ GBM and EGLStream, chosen at compile time | ✅ EGL, Vulkan or dumb buffers, plus DRM leasing; chosen at run time | ✅ only mode; atomic KMS with legacy fallback |
| Toolkit-free | ❌ GTK 3 | ✅ | ✅ | ✅ | ✅ |
| Embedder API rendering path | `FlutterCompositor` | `FlutterCompositor` | simple GL renderer config (`make_current`/`present`) | GL renderer config; `FlutterCompositor` optional (needed for platform views) | `FlutterCompositor`, with layers mapped to hardware planes |
| Frame presentation on Wayland | FBO, then a copy into a `wl_egl_window` subsurface via `eglSwapBuffers` | zero-copy GBM dmabuf, `linux-dmabuf` v4 feedback (X11: one copy into a DRI3 or SHM pixmap; KMS: GBM scanout buffers, no copy) | `wl_egl_window` + `eglSwapBuffers` | EGL: `wl_egl_window` + `eglSwapBuffers`; Vulkan: swapchain or dmabuf | n/a (GBM buffers flipped by KMS) |
| Explicit sync (`linux-drm-syncobj`) | ❌ | ✅ | ❌ | ⚠️ Vulkan only, experimental | n/a (KMS fences) |
| `fifo-v1` / `presentation-time` | ❌ / ❌ | ✅ / ✅ | ❌ / ✅ | ❌ / ✅ | n/a |
| Vsync driven by the compositor | ❌ timer fallback, renders while hidden | ✅ frame callbacks + presentation times (X11: Present complete events; KMS: page-flip events) | ⚠️ off by default (`ENABLE_VSYNC=OFF`) | ✅ presentation feedback, or page flip on DRM | ✅ page-flip events (no engine `vsync_callback`) |
| Software rendering fallback | ✅ (`fl_compositor_software`) | ⚠️ GL rendering copied to `wl_shm` when there is no dmabuf | ❌ | ✅ software, fbdev and dumb buffers | ✅ |
| Impeller | ✅ default (GLES, SDF) | ✅ default (GLES, SDF) | ⚠️ accepts the switch, GL only | ✅ (partial repaint disabled with Impeller) | ❌ no specific support |
| Vulkan | ❌ | ❌ | ❌ | ✅ | ⚠️ optional, off by default |
| External textures | ✅ GL and pixel buffer | ✅ pixel buffer and dmabuf (zero-copy, incl. YUV) | ✅ GL, EGLImage and pixel buffer | ✅ | ✅ |
| Platform views | ❌ | ⚠️ Wayland only: zero-copy plugin dmabufs in subsurfaces, Flutter overlays in subsurfaces above them | ⚠️ partial | ✅ | ✅ dmabuf composition and GStreamer video |

### Windows and displays

| Feature | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---|---|---|---|---|
| HiDPI | ✅ integer (GTK 3) | ✅ fractional (`fractional-scale-v1` + viewporter), integer fallback (X11: `Xft.dpi`; KMS: from panel size or `FLUTTER_KMS_SCALE`) | ⚠️ integer `wl_output.scale` only | ✅ `fractional-scale-v1` + viewporter | ⚠️ ratio from the panel's physical size, or `--pixel-ratio` |
| Display updates (`NotifyDisplayUpdate`) and display ID per view | ✅ | ✅ `wl_output` + `xdg-output`, hotplug | ❌ | ✅ multiple displays, hotplug | ⚠️ one display, at startup |
| Multiple views and windows | ⚠️ windowing API, top-level windows | ✅ top-levels, dialogs (`xdg-dialog`), popups and tooltips, sized to content, constraints (also on X11; KMS has one window) | ❌ single view | ⚠️ multiple views/outputs, mostly fullscreen surfaces | ❌ single view |
| Multiple engines | ✅ | ✅ sharing one display connection | ❌ | ✅ | ❌ |
| Decorations | ✅ GTK header bar | ✅ client-side (title text, buttons, shadows, rounded corners) or server-side via `xdg-decoration` | ⚠️ client-side drawn with GL shaders (title, buttons, resize) | ❌ none (no `xdg-decoration` or CSD) | n/a |
| Window activation (`xdg-activation`) | ✅ via GTK | ✅ | ❌ | ❌ | n/a |

### Input

| Feature | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---|---|---|---|---|
| Keyboard | ✅ GDK | ✅ xkbcommon, embedder key events, key repeat | ⚠️ xkbcommon, **no key repeat** | ✅ xkbcommon, key repeat | ✅ libinput + xkbcommon, key repeat |
| Text input / IME | ✅ GtkIMContext | ✅ `text-input-v3` (no IME on X11 or KMS) | ⚠️ text-input v1 and v3; many client options ignored | ✅ text-input v1 or v3 (chosen at build time) | ⚠️ no IME |
| Mouse cursor | ✅ GDK cursor names | ✅ `cursor-shape-v1` (X11: cursor font; KMS: hardware cursor plane) | ⚠️ `wayland-cursor` themes, a few shapes | ✅ `cursor-shape-v1` | ✅ hardware cursor plane |
| Scrolling | ✅ smooth and discrete | ⚠️ smooth and discrete (no `axis_value120`) | ⚠️ basic axis events | ⚠️ no `axis_value120` | ✅ libinput |
| Touchpad pinch and rotate | ✅ GTK gestures | ✅ `pointer-gestures` | ❌ | ❌ | ❌ |
| Touch | ✅ | ✅ | ✅ | ✅ | ✅ |
| Tablet / stylus | ❌ | ❌ | ❌ | ❌ | ✅ libinput |

### Platform channels and accessibility

| Feature | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---|---|---|---|---|
| Clipboard | ✅ | ✅ `wl_data_device`, X11 selections (KMS: within the app only) | ✅ `wl_data_device` | ❌ TODO | ❌ stub |
| `flutter/platform`: exit, `SystemNavigator.pop`, sounds | ✅ | ✅ bell through `xdg-system-bell`; exit code ignored | ⚠️ exit and clipboard | ✅ | ⚠️ |
| `flutter/settings` (dark mode, text scale, 24-hour time) | ✅ portal and GSettings | ⚠️ portal only, plus high contrast and reduced motion | ⚠️ hard-coded light theme | ⚠️ from config | ⚠️ |
| Lifecycle | ✅ | ✅ from activated/suspended state | ✅ | ✅ | ✅ |
| Locales | ✅ | ✅ | ❌ | ✅ | ✅ |
| Accessibility | ✅ ATK (through GTK's AT-SPI bridge) | ✅ AT-SPI directly over its own D-Bus code, with actions, text, values, selection, relations and dialogs | ❌ | ⚠️ AccessKit to AT-SPI, optional; not fetched by the build | ❌ channel only |

### API and maintenance

| Feature | Old (GTK) | New | Sony | ivi-homescreen | flutter-pi |
|---|---|---|---|---|---|
| Plugin API | GObject C (`FlValue`, `FlMethodChannel`…) | C++ client wrapper shared with Windows, over a C ABI | C API + C++ client wrapper (Windows style) | C ABI + C++ wrappers; many bundled plugins | C plugins compiled into the embedder |
| Build and tooling | upstream `flutter` tool | upstream `flutter` tool | forked `flutter-elinux` tool and engine artifacts | CMake, `config.toml`, Flutter tool for the app bundle | `flutterpi_tool` and its engine artifacts |
| Upstream and maintenance | upstream | upstream branch | third-party; last commit 2025-05; 7 TODOs in the source | third-party; very active (commits this week); 10 TODOs in `shell/` | third-party; active (2026-09); 8 TODOs |

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
  and copies of Flutter's `common` code. Its X11 and DRM backends are no longer
  unique: the new embedder now has both, chosen at run time.

## ivi-homescreen notes

These are from source analysis of `/tmp/ivi`.

- **Structure.** Several backends can be compiled in and one is picked at run
  time (`shell/backend/`): Wayland EGL/Vulkan, Wayland leased DRM, DRM/KMS
  EGL/Vulkan, headless EGL/Vulkan, and software. Wayland shell roles are xdg,
  AGL, IVI and RDK/simple. Configuration is a layered TOML file plus command
  line options.
- **Rendering.**
  - EGL uses the GL renderer config and presents with `wl_egl_window` and
    `eglSwapBuffers` (`shell/backend/wayland_egl/wayland_egl.cc`).
    `FlutterCompositor` is only used when platform views need interleaving.
  - With Impeller, EGL partial repaint is disabled, so every frame is a full
    repaint.
  - Vulkan presents through a swapchain or a dmabuf compositor path, with
    experimental `linux-drm-syncobj`. There is no `fifo-v1`.
  - Vsync comes from `presentation-time` feedback on Wayland and page flips on
    DRM, fed to the engine's `vsync_callback`.
- **Windows.** It supports multiple outputs and views with hotplug,
  `NotifyDisplayUpdate`, fractional scale with viewporter, and multiple engines.
  It has no decorations of any kind, and no popups or dialogs as separate
  windows.
- **Input.** xkbcommon with key repeat, text-input v1 or v3, `cursor-shape`.
  There are no pointer gestures, no tablet and no `axis_value120`.
- **Channels.** Platform, lifecycle, locales, text input, external textures and
  platform views are implemented. The clipboard is a TODO
  (`platform_handler.cc`). Accessibility uses AccessKit's AT-SPI adapter, but the
  AccessKit C bindings must be supplied by hand.
- **Assessment.** It is a capable, well-maintained embedded/automotive host. As
  a desktop embedder it is much larger than needed, and its default EGL path
  isn't zero-copy. It lacks desktop window behaviour: decorations, popups,
  dialogs, clipboard and settings. Adopting it would mean either taking on a
  large automotive-focused codebase or carving out its desktop parts. The new
  embedder already covers the desktop path in about a fifth of the code. Parts
  worth borrowing ideas from are its Vulkan backend, platform views and external
  textures.

## flutter-pi notes

These are from source analysis of `/tmp/flutterpi`.

- **Structure.** It is written in C and only drives DRM/KMS directly. It needs
  exclusive use of the display, so it can't run under a Wayland or X11
  compositor.
- **Rendering.**
  - It uses `FlutterCompositor` (`src/compositor_ng.c`). Flutter layers become
    GBM/dmabuf surfaces assigned to KMS hardware planes where possible, with
    fallback to GL composition. Atomic KMS is used, with a legacy fallback.
  - EGL/GLES is the default. Vulkan is optional and off by default, and a
    software renderer is available. There is no Impeller-specific handling.
  - Frame timing comes from page-flip and vblank events. The engine
    `vsync_callback` is disabled ("broken since 2.2").
- **Displays.** One display and one view, chosen at startup. The pixel ratio
  is calculated from the panel's physical size, and rotation by 90° steps is
  supported.
- **Input.** libinput with xkbcommon and key repeat, touch, mouse, scroll and
  stylus, and a hardware cursor plane. There is no IME.
- **Plugins.** Services (lifecycle and orientation; the clipboard handlers are empty stubs), raw
  keyboard, text input, external textures, platform views, and GStreamer video
  and audio players.
- **Assessment.** It is not a desktop embedder. Its plane-based KMS compositor
  is the best design reference for extending the new embedder's KMS backend,
  which already uses the same GBM/dmabuf buffers.

## Gaps in the new embedder

These are ordered by how much they affect apps:

1. **Platform channel follow-ups**: the `System.exitApplication` exit code is
   ignored, there is no GSettings fallback when the settings portal is missing,
   and no accent colour.
2. **Platform view follow-ups**: keyboard focus, a texture fallback for
   rotations and rounded clips (currently approximated by their bounding box),
   plugin access to a raw `wl_surface`, acquire fences for plugin buffers.
3. **Drag and drop, primary selection, high-resolution scrolling
   (`axis_value120`, `wl_seat` v8+), tablet input.**
4. **X11 follow-ups**: IME (XIM or IBus), platform views, zero-copy
   presentation.
5. **KMS follow-ups**: hardware planes for platform views
   and overlays, multiple outputs and hotplug, a system clipboard. flutter-pi's
   plane-based design is the best reference.
6. **Vulkan / Impeller-Vulkan.**

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
