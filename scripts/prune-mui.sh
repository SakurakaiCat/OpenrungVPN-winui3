#!/usr/bin/env bash
# Prune the Windows App SDK's MUI locale satellite folders (fr-CA, de-DE, …)
# from an assembled dist tree. Each satellite holds only the WinUI framework's
# localized .mui strings (Microsoft.ui.xaml(.Phone).dll.mui); the app's own
# text lives in code, and MUI falls back to the embedded neutral resources
# when a folder is absent, so every language except the keepers can go.
#
# Usage: prune-mui.sh <dist-dir>
# Exits non-zero if any satellite folder survives the pass, so every
# packaging path (build.sh, build.ps1's bash call, release CI) that runs
# this script can never ship the locale folders again.
set -euo pipefail

dist=${1:?usage: prune-mui.sh <dist-dir>}
[ -d "$dist" ] || { echo "prune-mui.sh: not a directory: $dist" >&2; exit 1; }

# A locale satellite is a directory whose top level holds ONLY *.mui files
# (Assets/, Views/, core/ etc. contain other entries and are never touched).
# Keep en-us: MUI's neutral fallback + the app's own languages.
is_keeper() { case "$1" in en-us | zh-CN | zh-TW) return 0 ;; *) return 1 ;; esac; }

pruned=0
leftover=0
while IFS= read -r -d '' d; do
    nonMui=0
    while IFS= read -r entry; do
        case "$entry" in *.mui) ;; *) nonMui=1; break ;; esac
    done < <(ls -A "$d")
    [ "$nonMui" -eq 0 ] || continue
    if is_keeper "$(basename "$d")"; then
        continue
    elif rm -rf "$d"; then
        pruned=$((pruned + 1))
    else
        leftover=$((leftover + 1))
    fi
done < <(find "$dist" -mindepth 1 -maxdepth 1 -type d -print0)
echo "==> pruned $pruned MUI locale folder(s) from $dist"

# Guard: fail the build if anything satellite-shaped is still there.
while IFS= read -r -d '' d; do
    nonMui=0
    while IFS= read -r entry; do
        case "$entry" in *.mui) ;; *) nonMui=1; break ;; esac
    done < <(ls -A "$d")
    [ "$nonMui" -eq 0 ] || continue
    is_keeper "$(basename "$d")" && continue
    echo "unpruned MUI locale folder remains: $d" >&2
    leftover=$((leftover + 1))
done < <(find "$dist" -mindepth 1 -maxdepth 1 -type d -print0)
[ "$leftover" -eq 0 ] || exit 1
