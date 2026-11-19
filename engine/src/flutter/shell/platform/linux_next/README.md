# Linux Next Platform Embedder

> [!WARNING]
> This embedder is experimental and under development.

This is the next iteration of the Flutter Linux embedder.

It is built as `libflutter_linux.so` and has a different API from the current
Linux embedder. Apps use it from a `linux_next` directory, which can be
alongside the `linux` directory used by the current embedder.

## Building an app

Prebuilt engine artifacts aren't available for this embedder, so
[build the engine locally](../../../../../../docs/engine/contributing/Setting-up-the-Engine-development-environment.md)
and run apps with `--local-engine`.

To add this embedder to an app:

```sh
flutter create --platforms=linux_next .
```

Then run it with:

```sh
flutter run -d linux-next
```
