#!/usr/bin/env bash
# Builds the console's default.xex of the recompiled game (docs/RECOMP_PLAN.md) with the open xsdk toolchain
# (WSL on Windows). Defaults: boi360's own xsdk and vulkan4xenon checkouts in /opt/boi360-xsdk/deps, patched and
# built by tools/xsdk/deps.sh first; the toolchain and glslang from the CTR360-XSDK distro's shared /opt/ctr360-xsdk
# (read only); the output in /opt/boi360-xsdk/recomp.
#
#   [XSDK=... VULKAN4XENON=... GLSLANG=... XSDK_TOOLCHAIN=... JOBS=2 GEN_OPT=-O2] tools/xsdk/build_recomp.sh
#
# Run on Windows first: tools/recomp/translate.py for the three images (build/recomp/out, out_lua, out_msvcp)
# and tools/recomp/shaders.py (build/recomp/gl_shaders.inc, build/recomp/spv). Output:
# build/console/recomp/default.xex with the licences it ships under; tools/stage_console.py stages it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS=/opt/ctr360-xsdk
XSDK="${XSDK:-/opt/boi360-xsdk/deps/xsdk}"
VULKAN4XENON="${VULKAN4XENON:-/opt/boi360-xsdk/deps/vulkan4xenon}"
GLSLANG="${GLSLANG:-$DEPS/glslang/build-host/StandAlone/glslang}"
XSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$DEPS/toolchain}"
OUT="${BOI_XSDK_BUILD:-/opt/boi360-xsdk/recomp}"
JOBS="${JOBS:-2}"
GEN_OPT="${GEN_OPT:--O2}"
mkdir -p "$OUT"
# the display-output patches (tools/xsdk/patches) go on boi360's own checkouts
if [ "$(dirname "$XSDK")" = "$(dirname "$VULKAN4XENON")" ] && [ "$(dirname "$XSDK")" != "$DEPS" ]; then
  DEPS="$(dirname "$XSDK")" TOOLCHAIN="$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-" JOBS="$JOBS" bash "$ROOT/tools/xsdk/deps.sh"
fi
for f in "$XSDK/build/libxsdk.a" "$XSDK/build/host/libxsc.so" "$VULKAN4XENON/build/libvulkan4xenon.a" "$GLSLANG" \
         "$ROOT/build/recomp/gl_shaders.inc" "$ROOT/build/recomp/out/functions.c" "$ROOT/build/recomp/out_lua/functions.c" \
         "$ROOT/build/recomp/out_msvcp/functions.c"; do
  [ -e "$f" ] || { echo "missing $f" >&2; exit 1; }
done
echo "xsdk $(git -C "$XSDK" rev-parse --short HEAD), vulkan4xenon $(git -C "$VULKAN4XENON" rev-parse --short HEAD)"

# Shaders: the game's programs converted by tools/recomp/shaders.py, compiled to Xenos microcode by xost into the
# bundle vulkan4xenon looks shader modules up in. A shader xost refuses stops the build here.
SH="$OUT/shaders"
mkdir -p "$SH"
rm -f "$SH"/*.spv
cp "$ROOT"/build/recomp/spv/*.spv "$SH/"
(
  cd "$SH"
  files="$(ls *.spv | sort)"
  stamp="$(cat $files "$VULKAN4XENON/tools/build_shaders.py" "$XSDK/build/host/libxsc.so" | sha256sum)"
  if [ ! -f boi_shaders.c ] || [ "$(cat stamp 2>/dev/null)" != "$stamp" ]; then
    python3 "$VULKAN4XENON/tools/build_shaders.py" --xost "$XSDK/xenos-toolchain" --glslang "$GLSLANG" \
      --output boi_shaders --optimize $files
    echo "$stamp" > stamp
  fi
)

COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)"

cmake -S "$ROOT/tools/xsdk/recomp" -B "$OUT/cmake" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$XSDK/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="$XSDK_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
  -DVULKAN4XENON="$VULKAN4XENON" -DBOI_SHADERS="$SH" -DBOI_GIT_COMMIT="$COMMIT" -DBOI_GEN_OPT="$GEN_OPT"
cmake --build "$OUT/cmake" -j"$JOBS"

cp "$OUT/cmake/boi360.xex" "$OUT/default.xex"
DST="$ROOT/build/console/recomp"
mkdir -p "$DST/licenses"
cp "$OUT/default.xex" "$DST/default.xex"
cp "$XSDK/LICENSE" "$DST/licenses/xsdk.txt"
cp "$XSDK/THIRD_PARTY_NOTICES.md" "$DST/licenses/xsdk-third-party.md" 2>/dev/null || true
cp "$VULKAN4XENON/LICENSE" "$DST/licenses/vulkan4xenon.txt"
cp "$VULKAN4XENON/THIRD_PARTY_NOTICES.md" "$DST/licenses/vulkan4xenon-third-party.md" 2>/dev/null || true
cp "$ROOT/src/third_party/vulkan-headers/LICENSE.md" "$DST/licenses/vulkan-headers.md"
cp "$ROOT/src/third_party/miniz/LICENSE" "$DST/licenses/miniz.txt"
sed -n '/^This software is available under 2 licenses/,$p' "$ROOT/src/third_party/stb/stb_vorbis.c" > "$DST/licenses/stb_vorbis.txt"
cp "$XSDK_TOOLCHAIN"/share/*-licenses/* "$DST/licenses/" 2>/dev/null || true
size="$(stat -c %s "$OUT/default.xex")"
echo "Built $DST/default.xex ($size bytes, source $COMMIT)"
