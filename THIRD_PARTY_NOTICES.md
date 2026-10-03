# Third-party notices

Everything in this project that came from someone else, what it is used for, and its license. The
release package carries these notices and the license texts in `PPSA99666/licenses/`.

## Shipped in the release

| Component | Used as | License |
| --- | --- | --- |
| [DOOM source code](https://github.com/id-Software/DOOM) (`linuxdoom-1.10`), id Software | The game engine in `src/doom/`, with the changes listed in `DEVELOPMENT.md` | GNU GPL v2 (`LICENSE`) |
| DOOM shareware IWAD v1.9 (`DOOM1.WAD`, episode 1), id Software | Bundled game data, `wads/doom1.wad`, unmodified; fetched by `tools/fetch-shareware.sh`. Its `M_DOOM` logo, scaled up, is the home-screen icon (`sce_sys/icon0.png`) | id Software's shareware terms: free to share unmodified and free of charge |
| [libarchive](https://www.libarchive.org/) 3.8.9 | Reading ZIP, 7Z and RAR archives in the importer (`tools/fetch-archive-libs.sh`, configured by `third_party/`) | BSD 2-Clause, with a few files public domain or CC0 (`licenses/libarchive.txt`) |
| [xz](https://tukaani.org/xz/) 5.8.4, liblzma | LZMA and LZMA2 decoding for 7Z archives | BSD Zero Clause (`licenses/liblzma.txt`) |
| [zlib](https://zlib.net/) 1.3.2 | Deflate decoding for ZIP archives | zlib license (`licenses/zlib.txt`) |
| `libc.prx` from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), BlackBearReloaded | Runtime module the PS5 loader requires next to a native title (`sce_module/libc.prx`), built from its source at commit `b1315a9` | GNU GPL v3 or later (`licenses/libc-prx-GPL-3.0.txt`) |

## Used to build

| Component | Used as | License |
| --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) `b1315a9` | `ps5-native-tool` (PIE to PS5 module conversion, FSELF signing) and its intermediate linker script `ps5-pie.ld`, fetched by `tools/fetch-native-tools.sh`, not copied into this repository | GNU GPL v3 or later |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.42, John Törnblom | C library headers, system import stubs, `prospero-lld`; the SDK compiler wrapper for the test launcher payload | GNU GPL v3 |
| [LLVM/Clang](https://llvm.org/) 18 | C compiler for the x86-64 PS5 target and the AMDGPU compute kernel | Apache 2.0 with LLVM exception |
| [libcurl](https://curl.se/) | HTTP for the PC test build only (`src/host/http.c`); the console uses the system's `sceHttp` | curl license |

## Studied for platform facts (no code copied)

These open-source projects were read to learn how the PS5's system interfaces behave. Their code was
not copied; the facts they document are hardware and system behaviour.

| Project | What was learned | License |
| --- | --- | --- |
| [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | AGC call sequence (wait-for-safe-render, command buffer layout, dispatch, flip, suspend point), compute register offsets, direct memory type and cache maintenance, the RELEASE_MEM/WAIT_REG_MEM fence pattern and its packet constants | GNU GPL v3 or later |
| [SDL PS5 port](https://github.com/ps5-payload-dev/SDL) | VideoOut, ScePad and AudioOut call contracts, the tiled scanout pixel layout, the IME dialog structures | zlib |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Native title startup contract, `param.json` fields, sandbox notes, the `sceHttp`/`sceSsl` setup that works in a sandboxed title | GNU GPL v3 or later |
| [ProsperoLight](https://github.com/blackbearreloaded/ProsperoLight) | Opening the system keyboard from a native title (`sceCommonDialogInitialize`, the IME dialog module ID and parameter layout) | GNU GPL v3 |
| [SharpProspero](https://github.com/SvenGDK/SharpProspero) | The `sceKernelGetdents` record layout for listing folders in the sandbox | GNU GPL v3 |
| [ps5-yamagi-quake2](https://github.com/blackbearreloaded/ps5-yamagi-quake2) | Native app heap limits and the system exit path | GNU GPL v3 or later |
| [websrv](https://github.com/ps5-payload-dev/websrv), [ps5upload](https://github.com/phantomptr/ps5upload) | How homebrew is launched and how files reach the console (the test tools use ps5upload's helper) | GNU GPL v3 |

DOOM is a registered trademark of id Software / ZeniMax Media. This project is not affiliated with
or endorsed by them. The only game data included is id Software's freely distributable shareware
episode; the full games are not included and the app does not point to any source for them.
