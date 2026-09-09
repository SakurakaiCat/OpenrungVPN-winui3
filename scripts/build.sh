#!/usr/bin/env bash
# Build openrung-winui3 from WSL: cross-compiles the Go core for Windows and
# drives the Windows host's MSBuild for the C++/WinRT WinUI3 app.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

# --- Go core (Windows, with uTLS Reality support; WinDivert kept external) ---
echo "==> building openrung-core.exe (windows/amd64)"
(cd core && CGO_ENABLED=0 GOOS=windows GOARCH=amd64 \
  go build -tags "with_utls with_external_windivert" \
  -trimpath -ldflags "-s -w" -o "$ROOT/dist/core/openrung-core.exe" .)

# --- C++/WinRT app via the Windows host's MSBuild (vswhere-located) ---
VSWHERE_WIN='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
VSWHERE_WIN_PATH="$(wslpath "$VSWHERE_WIN")"
if [ ! -f "$VSWHERE_WIN_PATH" ]; then
  echo "vswhere.exe not found; install Visual Studio Build Tools (C++ + WinUI)" >&2
  exit 1
fi
MSBUILD_WIN=$("$VSWHERE_WIN_PATH" -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | tr -d '\r' | head -1)
if [ -z "$MSBUILD_WIN" ]; then
  echo "MSBuild not found" >&2
  exit 1
fi
MSBUILD="$(wslpath "$MSBUILD_WIN")"

WIN_ROOT=$(wslpath -w "$ROOT")

# MSVC cannot create its PCH intermediate on the \\wsl.localhost 9P share
# (error C1083), so intermediate and output directories must live on the
# Windows side; the finished payload is copied back into dist\ afterwards.
TMP_WIN=$(powershell.exe -NoProfile -Command '[IO.Path]::GetTempPath()' | tr -d '\r')
BUILD_TMP_WIN="${TMP_WIN}openrung-build"
BUILD_TMP_WIN="${BUILD_TMP_WIN%/}\\"
BUILD_TMP_WSL=$(wslpath "$BUILD_TMP_WIN")

# The XAML toolchain emits lowercase includes (winrt/windows.foundation.h)
# and MSVC cannot write its PCH over \\wsl.localhost (9P: case-sensitive,
# PCH intermediate failures). Stage the whole project onto the Windows
# filesystem, build there with native paths, then copy outputs back.
STAGE_WIN="${BUILD_TMP_WIN}src\\OpenRung.WinUI.Cpp"
STAGE_WSL="$BUILD_TMP_WSL/src/OpenRung.WinUI.Cpp"
rm -rf "$BUILD_TMP_WSL/src"
mkdir -p "$(dirname "$STAGE_WSL")"
cp -r app/OpenRung.WinUI.Cpp "$STAGE_WSL"
rm -rf "$STAGE_WSL/obj" "$STAGE_WSL/x64"

mkdir -p "$BUILD_TMP_WSL/obj" "$BUILD_TMP_WSL/dist"

PROJ="$STAGE_WIN\\OpenRung.WinUI.Cpp.vcxproj"

echo "==> building OpenRung.WinUI (C++/WinRT, win-x64, self-contained)"
"$MSBUILD" "$PROJ" -restore -m \
  -p:Configuration=Release -p:Platform=x64 \
  -p:OutDir="${BUILD_TMP_WIN}dist\\" \
  -p:IntDir="${BUILD_TMP_WIN}obj\\x64\\Release\\"

echo "==> staging build output into $ROOT/dist"
# MSBuild nests the self-contained payload under dist\<TargetName>\, and the
# old C# app left managed artifacts a C++ build never emits — clear them.
rm -rf "$ROOT/dist/OpenRung.WinUI"
mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/OpenRung.WinUI.dll" "$ROOT/dist/OpenRung.WinUI.deps.json" \
  "$ROOT/dist/OpenRung.WinUI.runtimeconfig.json"
cp -r "$BUILD_TMP_WSL/dist/OpenRung.WinUI/." "$ROOT/dist/"

echo "==> done: $ROOT/dist"
