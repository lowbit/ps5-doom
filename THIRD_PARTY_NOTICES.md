# Third-party notices

Everything in this project that came from someone else, what it is used for, and its license.

## Shipped in the release

| Component | Used as | License |
| --- | --- | --- |
| [DOOM source code](https://github.com/id-Software/DOOM) (`linuxdoom-1.10`), id Software | The game engine in `src/doom/`, with the changes listed in `DEVELOPMENT.md` | GNU GPL v2 (`LICENSE`) |
| [Freedoom](https://freedoom.github.io/) 0.13.0, Freedoom project | Bundled game data: `wads/freedoom1.wad`, `wads/freedoom2.wad` | BSD 3-Clause (`wads/FREEDOOM-COPYING.txt`, credits in `wads/FREEDOOM-CREDITS.txt`) |
| `libc.prx` from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), BlackBearReloaded | Runtime module the PS5 loader requires next to a native title (`sce_module/libc.prx`), built from its source at commit `b1315a9` | GNU GPL v3 or later |

## Used to build

| Component | Used as | License |
| --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) `b1315a9` | `ps5-native-tool` (PIE to PS5 module conversion, FSELF signing) and its intermediate linker script `ps5-pie.ld`, fetched by `tools/fetch-native-tools.sh`, not copied into this repository | GNU GPL v3 or later |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.42, John Törnblom | C library headers, system import stubs, `prospero-lld`; the SDK compiler wrapper for the test launcher payload | GNU GPL v3 |
| [LLVM/Clang](https://llvm.org/) 18 | C compiler for the x86-64 PS5 target and the AMDGPU compute kernel | Apache 2.0 with LLVM exception |
| DOOM shareware IWAD (`DOOM1.WAD`), id Software | Regression data for the PC test build only (demo playback); fetched by `tools/fetch-shareware.sh`, not shipped | id Software shareware terms |

## Studied for platform facts (no code copied)

These open-source projects were read to learn how the PS5's system interfaces behave. Their code was
not copied; the facts they document are hardware and system behaviour.

| Project | What was learned | License |
| --- | --- | --- |
| [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | AGC call sequence (wait-for-safe-render, command buffer layout, dispatch, flip, suspend point), compute register offsets, direct memory type and cache maintenance, the RELEASE_MEM/WAIT_REG_MEM fence pattern and its packet constants | GNU GPL v3 or later |
| [SDL PS5 port](https://github.com/ps5-payload-dev/SDL) | VideoOut, ScePad and AudioOut call contracts and the tiled scanout pixel layout | zlib |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Native title startup contract, `param.json` fields, sandbox notes | GNU GPL v3 or later |
| [ps5-yamagi-quake2](https://github.com/blackbearreloaded/ps5-yamagi-quake2) | Native app heap limits and the system exit path | GNU GPL v3 or later |
| [websrv](https://github.com/ps5-payload-dev/websrv), [ps5upload](https://github.com/phantomptr/ps5upload) | How homebrew is launched and how files reach the console (the test tools use ps5upload's helper) | GNU GPL v3 |

DOOM is a registered trademark of id Software / ZeniMax Media. This project is not affiliated with
or endorsed by them. No commercial game data is included.
