# molebox-dump

`MagicBall3.dat` is a MoleBox virtual file system. The game's unpacked
executable (`game32.img`, written next to `MagicBall3.exe` at start-up) hooks
its own `CreateFileA` / `ReadFile` / `FindFirstFileA` imports so that reads of
`Data\...` are served from the package. Nothing in Windows itself is patched.

This tool is a proxy `bass.dll`. It forwards every `BASS_*` export to the real
library (renamed `bassorig.dll`) and, 20 seconds after start-up, walks the
virtual `Data\` tree through the game's hooked imports and writes every file to
`C:\MB3OUT\`. It only works with your own installed copy of the game.

The IAT slot addresses in `dumper.c` are specific to the Magic Ball 3 build
from 2006-12-29 (`game32.img`):

| Import          | IAT slot   |
|-----------------|------------|
| FindFirstFileA  | 0x4f0110   |
| FindClose       | 0x4f0114   |
| FindNextFileA   | 0x4f0118   |
| GetFileSize     | 0x4f0158   |
| CreateFileA     | 0x4f0164   |
| CloseHandle     | 0x4f0170   |
| ReadFile        | 0x4f0188   |

## Build and run (Linux or macOS with Wine)

```sh
i686-w64-mingw32-gcc -O2 -shared -o bass.dll dumper.c bass.def -Wl,--enable-stdcall-fixup

cd "<game dir>"
mv bass.dll bassorig.dll
cp /path/to/built/bass.dll .
wine MagicBall3.exe        # click "Play Demo Now", wait ~30 s at the main menu
```

Result: `C:\MB3OUT\Data\` (768 files) and `C:\MB3OUT\_dump.log`
(`DONE files=768 fail=0`). Restore the original `bass.dll` afterwards.
