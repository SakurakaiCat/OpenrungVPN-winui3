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
if [ ! -f "$(wslpath "$VSWHERE_WIN")" ]; then
  echo "vswhere.exe not found; install Visual Studio Build Tools (C++ + WinUI)" >&2
  exit 1
fi
MSBUILD_WIN=$("$VSWHERE_WIN" -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | head -1)
if [ -z "$MSBUILD_WIN" ]; then
  echo "MSBuild not found" >&2
  exit 1
fi
MSBUILD="$(wslpath "$MSBUILD_WIN")"

WIN_ROOT=$(wslpath -w "$ROOT")
PROJ="$WIN_ROOT\\app\\OpenRung.WinUI.Cpp\\OpenRung.WinUI.Cpp.vcxproj"

echo "==> building OpenRung.WinUI (C++/WinRT, win-x64, self-contained)"
"$MSBUILD" "$PROJ" -restore -m \
  -p:Configuration=Release -p:Platform=x64 \
  -p:OutDir="$WIN_ROOT\\dist\\" \
  -p:IntDir="$WIN_ROOT\\app\\OpenRung.WinUI.Cpp\\obj\\x64\\Release\\"

echo "==> done: $ROOT/dist"
