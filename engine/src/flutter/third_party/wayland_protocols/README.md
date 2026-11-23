# wayland_protocols

Wayland client protocol bindings used by the Linux Next embedder
(`//flutter/shell/platform/linux_next`).

The bindings are generated with `wayland-scanner` 1.18.0, the version of
libwayland in the build sysroot. Newer versions of `wayland-scanner` generate
code that requires libwayland 1.20 or later.

The core protocol is generated too, so core interfaces can be used at newer
versions than the libwayland in the sysroot. The generated interfaces have
hidden visibility so they don't clash with those exported by libwayland.

| Files | Source |
| --- | --- |
| `wayland-*` | [`protocol/wayland.xml`](https://gitlab.freedesktop.org/wayland/wayland/-/blob/1.24.0/protocol/wayland.xml) from wayland 1.24.0 |
| `xdg-shell-*` | [`stable/xdg-shell/xdg-shell.xml`](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.48/stable/xdg-shell/xdg-shell.xml) from wayland-protocols 1.48 |

To add or update a protocol, build `wayland-scanner` from the
[1.18.0 release](https://gitlab.freedesktop.org/wayland/wayland/-/tree/1.18.0)
and run:

```sh
wayland-scanner -c client-header <name>.xml <name>-client-protocol.h
wayland-scanner private-code <name>.xml <name>-protocol.c
```

Then add the files to `BUILD.gn`.
