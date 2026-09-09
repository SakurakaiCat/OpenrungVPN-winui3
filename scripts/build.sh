#!/usr/bin/env bash
# Build openrung-winui3 from WSL: cross-compiles the Go core for Windows and
# drives the Windows host's dotnet SDK for the WinUI3 app.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

# --- Go core (Windows, with uTLS Reality support; WinDivert kept external) ---
echo "==> building openrung-core.exe (windows/amd64)"
(cd core && CGO_ENABLED=0 GOOS=windows GOARCH=amd64 \
  go build -tags "with_utls with_external_windivert" \
  -trimpath -ldflags "-s -w" -o "$ROOT/dist/core/openrung-core.exe" .)

# --- WinUI3 app via the Windows host's dotnet ---
DOTNET="${DOTNET:-$HOME/.dotnet/dotnet.exe}"
if [ ! -x "$DOTNET" ]; then
  # fall back to a Windows-host install
  LOCALAPPDATA_WIN=$(powershell.exe -NoProfile -Command 'Write-Output $env:LOCALAPPDATA' | tr -d '\r')
  DOTNET="$(wslpath "$LOCALAPPDATA_WIN/Microsoft/dotnet/dotnet.exe")"
fi
WIN_ROOT=$(wslpath -w "$ROOT")

echo "==> publishing OpenRung.WinUI (win-x64, self-contained)"
"$DOTNET" publish "$WIN_ROOT\\app\\OpenRung.WinUI\\OpenRung.WinUI.csproj" \
  -c Release -r win-x64 --self-contained \
  -p:WindowsPackageType=None -p:WindowsAppSDKSelfContained=true \
  -p:PublishSingleFile=false \
  -o "$WIN_ROOT\\dist"

echo "==> done: $ROOT/dist"
