#!/usr/bin/env bash
# The xsdk and vulkan4xenon checkouts the console build links (tools/xsdk/build_recomp.sh), with isaac360's patches
# (tools/xsdk/patches) on top and their libraries built. The shared toolchain in /opt/ctr360-xsdk stays read only.
#
#   [DEPS=/opt/isaac360-xsdk/deps TOOLCHAIN=/opt/ctr360-xsdk/toolchain/bin/powerpc-xsdk-elf- JOBS=2] tools/xsdk/deps.sh
#
# Patches: xsdk-display-output (XSDK_DISPLAY_OUTPUT: the output's own size shown 1:1, a 1080p output's 1920x1080)
# and vulkan4xenon-display-output (v4x_set_display_output: the display and swapchain at that size). A patch already
# in its checkout is left as it is; one that no longer applies (a new upstream head) stops here.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="${DEPS:-/opt/isaac360-xsdk/deps}"
TOOLCHAIN="${TOOLCHAIN:-/opt/ctr360-xsdk/toolchain/bin/powerpc-xsdk-elf-}"
JOBS="${JOBS:-2}"
for repo in xsdk vulkan4xenon; do
  [ -d "$DEPS/$repo/.git" ] || git clone -q "https://github.com/xenosworks/$repo.git" "$DEPS/$repo"
  for p in "$ROOT"/tools/xsdk/patches/"$repo"-*.patch; do
    if git -C "$DEPS/$repo" apply --reverse --check "$p" 2>/dev/null; then
      continue
    fi
    git -C "$DEPS/$repo" apply "$p" || { echo "$(basename "$p") no longer applies to $repo $(git -C "$DEPS/$repo" rev-parse --short HEAD)" >&2; exit 1; }
    echo "applied $(basename "$p")"
  done
done
make -C "$DEPS/xsdk" -j"$JOBS" TOOLCHAIN="$TOOLCHAIN" build/libxsdk.a build/host/libxsc.so >/dev/null
make -C "$DEPS/vulkan4xenon" -j"$JOBS" TOOLCHAIN="$TOOLCHAIN" XSDK="$DEPS/xsdk" build/libvulkan4xenon.a >/dev/null
echo "xsdk $(git -C "$DEPS/xsdk" rev-parse --short HEAD), vulkan4xenon $(git -C "$DEPS/vulkan4xenon" rev-parse --short HEAD), patched and built"
