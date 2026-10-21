#!/bin/bash
# Builds Muggle Ball 3 as a macOS app bundle and signs it.
#
#   scripts/package_macos.sh [--universal] [--bundle-assets [DIR]] [--out DIR] [-- CMAKE_ARGS...]
#
#   --universal            arm64 and x86_64 in one binary (default: this Mac's architecture)
#   --bundle-assets [DIR]  copy the converted game assets (default: $MB3_ASSETS, else ./assets) into the app,
#                          so it runs from the Finder without MB3_ASSETS. For your own copy only: the files
#                          belong to Alawar and Dream Dale, so this is ad-hoc signed and never notarized.
#   --out DIR              where the app and the .zip go (default: dist)
#   -- CMAKE_ARGS...       anything else for the cmake configure step
#
# Signing is ad-hoc by default, which is enough to run it on this Mac. For a build that other people can
# open without Gatekeeper warnings, set these (see docs/MACOS.md):
#   DEVELOPER_ID    the signing identity, e.g. "Developer ID Application: Your Name (TEAMID)": signs with the
#                   hardened runtime and a secure timestamp instead of ad-hoc
#   NOTARY_PROFILE  a keychain profile made with `xcrun notarytool store-credentials`: submits the signed
#                   app to Apple, waits, staples the ticket and writes the .zip from the stapled app
set -euo pipefail

cd "$(dirname "$0")/.."

universal=0
bundle_assets=0
assets_dir="${MB3_ASSETS:-$PWD/assets}"
out=dist
cmake_args=()

die() { echo "package_macos: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --universal) universal=1 ;;
        --bundle-assets)
            bundle_assets=1
            if [[ $# -gt 1 && "$2" != -* ]]; then assets_dir="$2"; shift; fi
            ;;
        --out) [[ $# -gt 1 ]] || die "--out needs a folder"; out="$2"; shift ;;
        --) shift; cmake_args=("$@"); break ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

[[ "$(uname -s)" == Darwin ]] || die "this script builds a macOS app bundle and needs macOS"
for tool in cmake codesign ditto lipo otool; do
    command -v "$tool" >/dev/null || die "$tool not found"
done
if [[ -n "${NOTARY_PROFILE:-}" && -z "${DEVELOPER_ID:-}" ]]; then
    die "NOTARY_PROFILE needs DEVELOPER_ID too: only a Developer ID signature can be notarized"
fi
if [[ $bundle_assets -eq 1 && ( -n "${DEVELOPER_ID:-}" || -n "${NOTARY_PROFILE:-}" ) ]]; then
    die "an app with the game's files inside is for your own use: it is not signed with a Developer ID or notarized, because that is for apps you hand out"
fi

if [[ $universal -eq 1 ]]; then variant=universal; else variant=native; fi
build="build/package-$variant"

# A Developer ID build is for handing out, so it carries none of the game's artwork, not even its icon.
if [[ -n "${DEVELOPER_ID:-}" ]]; then game_icon=OFF; else game_icon=ON; fi
configure=(-S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DMB3_BUILD_TESTS=OFF -DMB3_GAME_ICON=$game_icon
           -DMB3_FETCH_SDL3=ON -DMB3_UNIVERSAL=$([[ $universal -eq 1 ]] && echo ON || echo OFF)
           -DMB3_BUNDLE_ASSETS=$([[ $bundle_assets -eq 1 ]] && echo ON || echo OFF))
if [[ $bundle_assets -eq 1 ]]; then
    [[ -d "$assets_dir/Data" ]] || die "no converted assets in $assets_dir (run tools/convert_assets.py, or pass the folder)"
    configure+=(-DMB3_ASSETS_DIR="$assets_dir")
fi
command -v ninja >/dev/null && configure+=(-G Ninja)

echo "==> configure ($variant)"
cmake "${configure[@]}" ${cmake_args[@]+"${cmake_args[@]}"}
echo "==> build"
cmake --build "$build"

built="$build/app/MuggleBall3.app"
[[ -d "$built" ]] || die "the build produced no $built"
mkdir -p "$out"
app="$out/MuggleBall3.app"
rm -rf "$app"
ditto "$built" "$app"
exe="$app/Contents/MacOS/MuggleBall3"

echo "==> check"
archs="$(lipo -archs "$exe")"
echo "architectures: $archs"
if [[ $universal -eq 1 ]]; then
    [[ " $archs " == *" arm64 "* && " $archs " == *" x86_64 "* ]] || die "expected arm64 and x86_64, got: $archs"
fi
# The app must run on a Mac without Homebrew: only system libraries and frameworks.
foreign="$(otool -L "$exe" | awk '/^\t/ {print $1}' | grep -Ev '^(/usr/lib/|/System/Library/)' || true)"
[[ -z "$foreign" ]] || die "the binary links libraries from outside the system: $foreign"
echo "links only system libraries"
if [[ $game_icon == OFF && -e "$app/Contents/Resources/AppIcon.icns" ]]; then
    die "a Developer ID build must not contain the game's icon"
fi

echo "==> sign"
if [[ -n "${DEVELOPER_ID:-}" ]]; then
    codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" "$app"
else
    codesign --force --sign - --timestamp=none "$app"
fi
codesign --verify --strict --verbose=2 "$app"
codesign -dv "$app" 2>&1 | sed 's/^/  /'

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
zip="$out/MuggleBall3-$version-$variant.zip"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    echo "==> notarize (this waits for Apple, usually a few minutes)"
    submission="$out/MuggleBall3-notarize.zip"
    ditto -c -k --keepParent "$app" "$submission"
    result="$(xcrun notarytool submit "$submission" --keychain-profile "$NOTARY_PROFILE" --wait 2>&1)" || true
    echo "$result"
    rm -f "$submission"
    if ! grep -q 'status: Accepted' <<<"$result"; then
        id="$(awk '/^ *id:/ {print $2; exit}' <<<"$result")"
        [[ -z "$id" ]] || xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" || true
        die "notarization was not accepted"
    fi
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    spctl --assess --type execute --verbose=2 "$app"
fi

rm -f "$zip"
ditto -c -k --keepParent "$app" "$zip"
echo "==> done"
echo "app: $app"
echo "zip: $zip"
if [[ $bundle_assets -eq 1 ]]; then
    echo "this app contains the game's files: keep it to yourself and do not distribute it"
fi
