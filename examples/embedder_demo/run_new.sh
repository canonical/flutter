#!/bin/bash
#
# Runs the demo with the new embedder from this branch. Build it first with
# build_demo.sh. Arguments are passed to the app, e.g. --mode=heavy.

bundle="$(dirname "${BASH_SOURCE[0]}")/build/demo/new"
if [ ! -x "$bundle/embedder_demo" ]; then
  echo "Not built, run build_demo.sh first." >&2
  exit 1
fi
exec "$bundle/embedder_demo" "$@"
