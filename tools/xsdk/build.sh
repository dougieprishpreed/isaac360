#!/usr/bin/env bash
# Builds the console's default.xex with the open xsdk toolchain (Linux, or WSL on Windows).
# Defaults: the CTR360-XSDK distro's shared checkouts in /opt/ctr360-xsdk (read only: their libraries must be
# built already), boi360's own output in /opt/boi360-xsdk.
#
#   [XSDK=... VULKAN4XENON=... GLSLANG=... XSDK_TOOLCHAIN=... JOBS=2] tools/xsdk/build.sh
#
# Run the prepare scripts on Windows first (tools/build_windows_target.ps1 does): the build reads the generated
# tables in build/generated. Output: build/console/xex/default.xex with the licences it ships under;
# tools/stage_console.py stages it with the data pack.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS=/opt/ctr360-xsdk
XSDK="${XSDK:-$DEPS/xsdk}"
VULKAN4XENON="${VULKAN4XENON:-$DEPS/vulkan4xenon}"
GLSLANG="${GLSLANG:-$DEPS/glslang/build-host/StandAlone/glslang}"
XSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$DEPS/toolchain}"
OUT="${BOI_XSDK_BUILD:-/opt/boi360-xsdk/output}"
JOBS="${JOBS:-2}"
mkdir -p "$OUT"
for f in "$XSDK/build/libxsdk.a" "$XSDK/build/host/libxsc.so" "$VULKAN4XENON/build/libvulkan4xenon.a" "$GLSLANG" \
         "$ROOT/build/generated/frontend_assets.h"; do
  [ -e "$f" ] || { echo "missing $f" >&2; exit 1; }
done
echo "xsdk $(git -C "$XSDK" rev-parse --short HEAD), vulkan4xenon $(git -C "$VULKAN4XENON" rev-parse --short HEAD)"

# Shaders: the renderer's SPIR-V (src/render/shaders/spirv.inc) compiled to Xenos microcode by xost, in the bundle
# vulkan4xenon looks shader modules up in. A shader xost refuses stops the build here.
SH="$OUT/shaders"
mkdir -p "$SH"
files="$(python3 "$ROOT/tools/xsdk/spirv_files.py" "$ROOT/src/render/shaders/spirv.inc" "$SH")"
(
  cd "$SH"
  stamp="$(cat $files "$VULKAN4XENON/tools/build_shaders.py" "$XSDK/build/host/libxsc.so" | sha256sum)"
  if [ ! -f boi_shaders.c ] || [ "$(cat stamp 2>/dev/null)" != "$stamp" ]; then
    python3 "$VULKAN4XENON/tools/build_shaders.py" --xost "$XSDK/xenos-toolchain" --glslang "$GLSLANG" \
      --output boi_shaders --optimize $files
    echo "$stamp" > stamp
  fi
)

COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)"
if [ -n "$COMMIT" ] &&
  [ -n "$(git -C "$ROOT" -c core.autocrlf=input status --porcelain --untracked-files=no 2>/dev/null)" ]; then
  COMMIT="$COMMIT+"
fi

cmake -S "$ROOT/tools/xsdk" -B "$OUT/cmake" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$XSDK/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="$XSDK_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
  -DVULKAN4XENON="$VULKAN4XENON" -DBOI_SHADERS="$SH" -DBOI_GIT_COMMIT="$COMMIT"
cmake --build "$OUT/cmake" -j"$JOBS"

cp "$OUT/cmake/boi360.xex" "$OUT/default.xex"
DST="$ROOT/build/console"
mkdir -p "$DST/xex" "$DST/licenses"
cp "$OUT/default.xex" "$DST/xex/default.xex"
cp "$XSDK/LICENSE" "$DST/licenses/xsdk.txt"
cp "$XSDK/THIRD_PARTY_NOTICES.md" "$DST/licenses/xsdk-third-party.md" 2>/dev/null || true
cp "$VULKAN4XENON/LICENSE" "$DST/licenses/vulkan4xenon.txt"
cp "$VULKAN4XENON/THIRD_PARTY_NOTICES.md" "$DST/licenses/vulkan4xenon-third-party.md" 2>/dev/null || true
cp "$ROOT/src/third_party/vulkan-headers/LICENSE.md" "$DST/licenses/vulkan-headers.md"
cp "$XSDK_TOOLCHAIN"/share/*-licenses/* "$DST/licenses/" 2>/dev/null || true
echo "Built $DST/xex/default.xex ($(stat -c %s "$OUT/default.xex") bytes, source $COMMIT)"
