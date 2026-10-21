# macOS: building, packaging, signing

Everything here is also in `scripts/package_macos.sh`, which does the build, the checks and the signing in one go.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `MB3_UNIVERSAL` | OFF | One binary with arm64 and x86_64. Turns on `MB3_FETCH_SDL3`. |
| `MB3_FETCH_SDL3` | OFF | Compile SDL3 (the pinned `release-3.2.10`) from source and link it statically, instead of using an installed one. |
| `MB3_BUNDLE_ASSETS` | OFF | Copy the converted game assets into `Contents/Resources/assets` after each build. For your own copy only, see below. |
| `MB3_ASSETS_DIR` | `$MB3_ASSETS`, else `<source>/assets` | Where `AppIcon.icns` and the assets to bundle are read from. |

A plain build is what `README.md` shows: it uses Homebrew's SDL3 (a dylib for this Mac's architecture only), so
the binary needs Homebrew and cannot be universal. That is fine for development. A build that is meant to run
elsewhere compiles SDL3 itself, which is what `MB3_FETCH_SDL3` and `MB3_UNIVERSAL` ask for; naming
architectures with `-DCMAKE_OSX_ARCHITECTURES=...` does the same.

`CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET` are read while `project()` detects the compiler,
so the top-level `CMakeLists.txt` sets them before that call. (Set afterwards, as an earlier version did,
they only filled the cache: builds were native-only and targeted whatever macOS built them.) Such a build
targets macOS **13.3**, which is the oldest target whose libc++ has `std::to_chars` for floating point, and
`std::format` needs it; the code uses `std::format` for logging. Older targets fail to compile.

```sh
cmake -S . -B build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release -DMB3_UNIVERSAL=ON
cmake --build build-universal
lipo -info build-universal/app/MuggleBall3.app/Contents/MacOS/MuggleBall3   # x86_64 arm64
arch -x86_64 build-universal/app/MuggleBall3.app/Contents/MacOS/MuggleBall3 --screenshot out.png   # Rosetta
otool -L build-universal/app/MuggleBall3.app/Contents/MacOS/MuggleBall3     # system libraries only
```

## Icon

`tools/make_icon.py` reads the icon from the original's `MagicBall3.exe` and builds `assets/AppIcon.icns`
(details in `ASSET_FORMATS.md`); `tools/convert_assets.py` runs it. CMake puts the file in the bundle and sets
`CFBundleIconFile` **when it finds it at configure time**, so run `cmake` again after creating it. The icon is
the original game's artwork. It stays in the gitignored `assets/` folder, and a build made with it carries it:
if you hand an app to other people, decide whether that is what you want (build without it by pointing
`MB3_ASSETS_DIR` at a folder that has none).

## Window

Fullscreen: F11 and Alt+Enter (handled by the window), and Ctrl+Cmd+F (the "Toggle Full Screen" item SDL puts
in the Window menu, so the window leaves that one alone: SDL reports the key too, and handling it twice would
switch back at once). It is macOS's own fullscreen Space, so it animates and `Window::fullscreen()` changes when
the animation ends. The size the window had before, in points, and whether it was fullscreen are saved when the
game closes (`settings.json`, see `SAVES.md`) and used next time; a size from `--size` is not saved. The
window is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`: sizes are in points, the renderer draws in pixels, and
the camera's aspect ratio comes from the pixel size. The 3D view keeps its vertical field of view whatever the
shape of the window.

## An app that runs from the Finder

The Finder does not pass `MB3_ASSETS`. For your own use, bundle the assets:

```sh
scripts/package_macos.sh --bundle-assets            # assets from $MB3_ASSETS or ./assets
scripts/package_macos.sh --universal --bundle-assets /path/to/assets --out dist
```

or, with CMake directly, `-DMB3_BUNDLE_ASSETS=ON [-DMB3_ASSETS_DIR=...]`. The app then has the game's files in
`Contents/Resources/assets` and finds them first of all but `MB3_ASSETS`. **Those files belong to Alawar and
Dream Dale.** An app with them inside is for yourself: do not upload it, hand it out or notarize it. The script
refuses to combine `--bundle-assets` with a Developer ID or notarization, and always signs such an app ad-hoc.

Without bundled assets the app looks in `MB3_ASSETS`, `./assets`, then `assets/` inside the per-user data folder
(`~/Library/Application Support/MuggleBall3/`), where each player can put the folder `convert_assets.py` made.

## Packaging

```sh
scripts/package_macos.sh [--universal] [--bundle-assets [DIR]] [--out DIR] [-- extra cmake arguments]
```

It configures `build/package-universal` or `build/package-native` as a Release build with `MB3_FETCH_SDL3`
(so the app has no Homebrew dependency), builds, copies the app to `dist/` (or `--out`), then:

1. checks the architectures (`lipo`) and that the binary links only `/usr/lib` and `/System/Library`;
2. signs: ad-hoc (`codesign --sign -`) unless `DEVELOPER_ID` is set, then `codesign --verify --strict` and `codesign -dv`;
3. with `NOTARY_PROFILE`: notarizes and staples (below);
4. writes `dist/MuggleBall3-<version>-<universal|native>.zip` with `ditto`.

An ad-hoc signed app runs on the Mac that built it, and elsewhere only after Gatekeeper is told to allow it
(right-click, Open). That is what a local build needs.

## Developer ID signing and notarization

This needs an Apple Developer Program membership and is run by you, with your credentials; nothing in the
repository holds any.

1. In Xcode (Settings, Accounts, Manage Certificates) or on developer.apple.com, create a **Developer ID
   Application** certificate and make sure it is in your login keychain. Its name is what `DEVELOPER_ID` wants:

   ```sh
   security find-identity -v -p codesigning
   #   1) 0123...  "Developer ID Application: Your Name (TEAMID)"
   ```

2. Create an app-specific password at appleid.apple.com, and store notarization credentials in the keychain
   once (the profile name is yours to pick):

   ```sh
   xcrun notarytool store-credentials "mb3-notary" --apple-id you@example.com --team-id TEAMID
   # it asks for the app-specific password
   ```

3. Build, sign, notarize and staple a universal app:

   ```sh
   DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)" NOTARY_PROFILE=mb3-notary \
       scripts/package_macos.sh --universal
   ```

   Behind it, in order: `codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" MuggleBall3.app`
   (hardened runtime, which notarization requires; no entitlements are needed because SDL3 is linked statically
   and nothing loads code, uses JIT or reads another app's data), `ditto -c -k --keepParent` to a temporary zip,
   `xcrun notarytool submit ... --keychain-profile "$NOTARY_PROFILE" --wait`, `xcrun stapler staple`,
   `xcrun stapler validate` and `spctl --assess --type execute`. If Apple does not accept it, the script prints
   the notary log and stops; fix what the log names (usually a binary that is not signed with the hardened
   runtime or a timestamp) and run it again.

4. Check the result yourself:

   ```sh
   codesign -dvv dist/MuggleBall3.app          # Authority=Developer ID Application ..., flags=0x10000(runtime)
   xcrun stapler validate dist/MuggleBall3.app
   spctl -a -vv -t exec dist/MuggleBall3.app   # accepted, source=Notarized Developer ID
   ```

The zip contains no game data (see above), so whoever opens it needs their own converted assets in
`~/Library/Application Support/MuggleBall3/assets` or `MB3_ASSETS`.
