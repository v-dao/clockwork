#!/usr/bin/env bash
# Generate libvulkan-1.dll.a from vulkan-1.def (MinGW dlltool). Git tracks only the .def file.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/third_party/vulkan-win32-mingw"
DEF="$DIR/vulkan-1.def"
OUT="$DIR/libvulkan-1.dll.a"
if [[ ! -f "$DEF" ]]; then
  echo "bootstrap_vulkan_mingw: missing $DEF" >&2
  exit 1
fi
DLLTOOL="${MINGW_DLLTOOL:-dlltool}"
mkdir -p "$DIR"
(cd "$DIR" && "$DLLTOOL" -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll)
if [[ ! -f "$OUT" ]]; then
  echo "bootstrap_vulkan_mingw: expected output missing: $OUT" >&2
  exit 1
fi
echo "bootstrap_vulkan_mingw: wrote $OUT"
