# How the game data was recovered

1. `magic-ball-3.exe` (download) is a Gentee/CreateInstall installer. It
   installs `MagicBall3.exe`, DLLs and five `.dat` files.
2. `Textures.dat`, `Sounds.dat` and `Musics.dat` are plain stored ZIP files.
3. `MagicBall3.exe` is Alawar's trial wrapper. At start-up it writes the real
   game, `game32.img`, which is packed with MoleBox.
4. `MagicBall3.dat` is MoleBox's virtual file system. `game32.img` hooks its own
   `CreateFileA`, `ReadFile`, `FindFirstFileA` and related imports, so reads of
   `Data\...` come from the package.
5. `tools/molebox-dump` is a proxy `bass.dll` that walks the virtual `Data\`
   tree through those hooks and writes every file to disk: 768 files, with no
   failures.

Contents of `Data/`: 83 levels (`Levels/*.mb3`, see `LEVEL_FORMAT.md`), 272
DirectX `.x` meshes, about 390 object XML files, 19 menu layouts, materials,
sound and music definitions, and 3 cooked PhysX convex meshes.
