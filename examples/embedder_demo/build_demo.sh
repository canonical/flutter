#!/bin/bash
#
# Builds the demo app with the GTK embedder (from master) and with the new
# embedder (from this branch), both in profile mode with locally built engines.
#
# This checks out master, builds the engine and app, then checks out the
# original branch again and does the same. The bundles are copied to
# build/demo/gtk and build/demo/new, so they can be run at any time with
# run_gtk.sh and run_new.sh.
#
# Set BASE to build the GTK embedder from a different commit, e.g.
# BASE=origin/master ./build_demo.sh

set -e -u -o pipefail

# Everything is in a function so bash has read the whole script before it is
# removed by the checkout of master.
main() {
  local demo_dir repo engine_src flutter original base
  demo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  repo="$(git -C "$demo_dir" rev-parse --show-toplevel)"
  engine_src="$repo/engine/src"
  flutter="$repo/bin/flutter --no-version-check"
  local out="$demo_dir/build/demo"
  local engine_flags=(--local-engine-src-path "$engine_src" --local-engine=host_profile --local-engine-host=host_profile)

  cd "$repo"
  if ! git diff --quiet HEAD; then
    echo "Commit or stash your changes first, this script switches branches." >&2
    exit 1
  fi
  if [ ! -f "$engine_src/out/host_profile/build.ninja" ]; then
    echo "Configure the profile engine first, in engine/src:" >&2
    echo "  ./flutter/tools/gn --runtime-mode=profile --ccache --no-lto" >&2
    exit 1
  fi
  original="$(git symbolic-ref -q --short HEAD || git rev-parse HEAD)"
  base="$(git merge-base HEAD "${BASE:-master}")"

  # Window size, so both versions match.
  local width height
  read -r width height < <(sed -n 's/.*FlutterDesktopView::create(engine, \([0-9]*\), \([0-9]*\)).*/\1 \2/p' \
    "$demo_dir/linux/runner/main.cc")

  # The source is staged outside the tree, as it doesn't exist on master.
  rm -rf "$out/src"
  mkdir -p "$out/src"
  cp -r "$demo_dir/lib" "$demo_dir/pubspec.yaml" "$out/src/"

  # Hooks are skipped as the engine's post-checkout hook needs depot_tools, and
  # only suggests running gclient sync.
  local git_checkout="git -c core.hooksPath=/dev/null checkout -q"

  # Return to the original branch however this exits.
  trap "cd '$repo' && $git_checkout '$original'" EXIT

  echo "=== Building GTK embedder from master ($(git log -1 --format='%h %s' "$base"))"
  $git_checkout --detach "$base"
  OUT=out/host_profile "$engine_src/build.sh" \
    flutter/shell/platform/linux:linux \
    flutter/shell/platform/linux:flutter_gtk \
    gen_snapshot flutter_patched_sdk dart-sdk
  rm -rf "$out/gtk_project"
  $flutter create --platforms linux --project-name embedder_demo --org dev.flutter \
    --no-pub "$out/gtk_project" > /dev/null
  rm -rf "$out/gtk_project/lib" "$out/gtk_project/test" "$out/gtk_project/analysis_options.yaml"
  cp -r "$out/src/lib" "$out/src/pubspec.yaml" "$out/gtk_project/"
  # Use the same window size as the new embedder's runner.
  sed -i "s/gtk_window_set_default_size(window, [0-9]*, [0-9]*)/gtk_window_set_default_size(window, $width, $height)/" \
    "$out/gtk_project/linux/runner/my_application.cc"
  grep -q "gtk_window_set_default_size(window, $width, $height)" \
    "$out/gtk_project/linux/runner/my_application.cc"
  (cd "$out/gtk_project" && $flutter build linux --profile "${engine_flags[@]}")
  rm -rf "$out/gtk"
  cp -r "$out/gtk_project/build/linux/x64/profile/bundle" "$out/gtk"

  echo "=== Building new embedder from $original"
  $git_checkout "$original"
  trap - EXIT
  OUT=out/host_profile "$engine_src/build.sh" \
    flutter/shell/platform/linux:linux \
    flutter/shell/platform/linux:flutter_linux_archive \
    gen_snapshot flutter_patched_sdk dart-sdk
  (cd "$demo_dir" && $flutter build linux --profile "${engine_flags[@]}")
  rm -rf "$out/new"
  cp -r "$demo_dir/build/linux/x64/profile/bundle" "$out/new"

  echo
  echo "Done. Run with:"
  echo "  $demo_dir/run_gtk.sh"
  echo "  $demo_dir/run_new.sh"
}

main "$@"; exit
