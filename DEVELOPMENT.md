# DOOM for PS5 (native)

id Software's original `linuxdoom-1.10` source running as a native PS5 title: its own home-screen
icon, launched like a game, drawing through Sony's VideoOut and AGC (GPU) drivers, reading the
DualSense through ScePad and playing sound through AudioOut. Title ID `PPSA99666`.

All PS5, platform, audio, launcher and test code here is written for this project. The external
code is id's game source, the archive libraries the importer is built from (libarchive, xz's
liblzma, zlib), the QR code library of the send screen (qrcodegen) and the build tools listed at
the end.

## Layout

| Path | What |
| --- | --- |
| `src/doom/` | id's original game code with the 64-bit and portability fixes listed below |
| `src/port/` | Doom's `i_*` layer (main, system, video, sound, network, controller mapping) on top of the platform API, and the button glyphs shared with the launcher |
| `src/launcher/` | The game list shown before the engine starts: IWAD discovery, settings, Doom-style drawing from the bundled WAD, HTML folder listings, the importer (HTTP or local file, WAD or ZIP/7Z/RAR), and the send screen's HTTP server and web page (`upload.c`, `upload.html`) |
| `src/platform/platform.h` | The platform API: time, log, files and folder listing, video present, pad, rumble, audio output, HTTP, text input |
| `src/audio/` | Sound engine: SFX mixer, MUS sequencer with DMX-style voice allocation, OPL FM synth |
| `src/ps5/` | PS5 backend: startup (`crt0.c`), system, VideoOut, AGC compute presenter, pad, AudioOut, `sceHttp`, the IME keyboard |
| `src/ps5/present.cl` | GPU kernel: palette lookup, sharp-bilinear scaling to 1080p, tiled scanout writes (gfx1010, wave64) |
| `src/host/` | Headless Linux backend for testing on the PC: PNG frames, scripted pad, WAV audio, libcurl HTTP |
| `third_party/` | Build configuration for libarchive, liblzma, zlib and qrcodegen (`config.h` files and `third_party.mk`); the sources are fetched, not copied |
| `test/` | `render_music` (MUS lump to WAV), `present_preview` (runs the GPU kernel's math on the CPU) and the console test plans |
| `sce_sys/` | `param.json` (raise `contentVersion` in every release) and the icon (the shareware WAD's `M_DOOM` logo, scaled 3x on black) |
| `tools/` | Tool, library and shareware WAD fetch, kernel and page embedding, deploy, console test runner, `send.py` (sends files to the send screen like its page) |

## Build

Everything builds in the `ps5-doom-build` image (`docker/Dockerfile`: clang/lld/llvm 18, ninja, gdb,
libcurl for the PC build).

```bash
docker build -t ps5-doom-build docker/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -w /src ps5-doom-build make -j16 ps5 host
```

`make ps5` fetches the pinned tools, the libraries (`tools/fetch-third-party.sh`) and the
shareware WAD on first use, then:

1. compiles the game, the launcher and the PS5 layer for `x86_64-sie-ps5` against the PS5 payload
   SDK headers, and the reading half of libarchive (zip, 7z, rar, rar5), liblzma's decoders and
   zlib's inflate with the same compiler;
2. compiles `present.cl` for `gfx1010` (wave64), links it and embeds it with `tools/embed-kernel.py`, which
   reads the register values from the compiler's kernel descriptor and fails the build if the
   kernel needs anything the presenter does not set up;
3. generates import stubs the SDK lacks (`libSceAgc`, `libSceAgcDriver`, `libSceCommonDialog`) from
   `src/ps5/stubs/*.txt`;
4. links a PIE, converts it to a PS5 module, signs the FSELF and assembles `dist/PPSA99666/` with
   the shareware `doom1.wad` in `wads/`.

The libraries' `malloc`, `calloc`, `realloc`, `free` and `strdup` are renamed at compile time to
`src/launcher/import_memory.c`, which takes blocks of 256 KB and more from `mmap`: a native app's
libc heap cannot hold a 64 MB LZMA dictionary. The PC build uses the same wrapper.

## Testing on the PC

`build/host/bin/doom` is the same game on the headless backend. Environment variables:
`DOOM_WADDIR` (the user's WAD folder), `DOOM_SAVEDIR`, `DOOM_OUT` (frame output dir),
`DOOM_CAPTURE` (frame numbers to save as PNG plus raw indices and palette), `DOOM_FRAMES` (exit
after N frames), `DOOM_AUDIO` (absolute WAV path), `DOOM_INPUT` (pad script:
`frame:BUTTON+BUTTON+LX=-32000:frames;...`), `DOOM_IWAD` (skip the launcher and start this game
file, e.g. `doom1.wad`) and `DOOM_TEXT` (the answer to the keyboard prompt).

```bash
DOOM_IWAD=doom1.wad DOOM_OUT=out DOOM_CAPTURE=120,900 build/host/bin/doom -timedemo demo1
```

Without `DOOM_IWAD` the launcher runs and the pad script drives it. For the importer, serve files
from inside the container with `python3 -m http.server` (no range support) or reach a server on
the Windows host as `host.docker.internal` (`docker run --add-host=host.docker.internal:host-gateway`).

For memory errors, build with AddressSanitizer:
`make host HOST_DIR=build/asan HOST_FLAGS="-O1 -g -fsanitize=address" HOST_CC="clang -fsanitize=address"`.

## The launcher

`I_ChooseIwad` (in `src/port/i_system.c`) runs the launcher before the engine identifies its IWAD
and maps the chosen game to Doom's game mode, mission and language. The launcher:

- scans the WAD folders (`/app0/wads`, then `/download0`) for the known IWAD names, checks each
  file's header and directory, and tells The Ultimate DOOM from DOOM by the presence of `E4M1`;
- imports any ZIP, 7Z or RAR found there that it has not imported before (recorded in
  `/download0/launcher.cfg` by size and path), extracting only known IWAD names;
- downloads from a typed link with `plat_http_get`: an HTML answer is parsed as a folder listing
  (links under the folder that end in `.wad`, `.zip`, `.7z`, `.rar` or `/`), a WAD is copied, an
  archive goes through libarchive. libarchive seeks with HTTP range requests; a server without them
  is read through or re-read from the start instead;
- writes into the first writable WAD folder (`/app0/wads` on the console), through a `.part` file
  that is checked as an IWAD before it is renamed;
- shows a one-time notice with a checkbox before the first download;
- receives files from a browser on the send screen (*Add games*, *Send from PC or phone*):
  `upload.c` listens only while that screen is open, on port 9666 or the next free one, serves
  `upload.html` (embedded at build time by `tools/embed-file.py`) and stores each `PUT /upload/<name>`
  as `<name>.upload` in the WAD folder. The launcher moves game WADs into place, runs archives
  through the importer and deletes them afterwards, and reports each result on the screen and to the
  page, which polls `GET /status`. The address shown is the one the route to the internet leaves
  from (a connected UDP socket's local address); the QR code is drawn with qrcodegen.

Saves are per game: `<save dir>/<iwad name>sav<slot>.dsg` (for example `doom2sav0.dsg`).

## On the console

`uv run --no-project python tools/deploy.py` uploads `dist/PPSA99666` to `/data/homebrew/PPSA99666`
(PS5Upload helper must be running), where ShadowMountPlus registers it. Uploading adds and replaces
files but never deletes, so files dropped from the package stay on the console until removed.
ShadowMountPlus copies `sce_sys` (icon, `param.json`) into `/user/appmeta/PPSA99666` and
`/user/app/PPSA99666` only when it first installs the title; a new icon has to be copied there too.
`make release` writes `dist/PPSA99666.zip` and its `.sha256`.

The app is sandboxed: it reads and writes `/app0` (its folder; imports land in `/app0/wads`) and
writes `/download0` (config, saves, `launcher.cfg` and `doom.log`). Log lines also go to the kernel
log (`sceKernelDebugOutText`). Fatal errors show as a system notification.

Presentation uses the AGC compute path by default. Hold **L1+R1** while the game starts to force the
CPU scaler. If a GPU frame does not complete within 500 ms, the game switches to the CPU scaler by
itself and logs why.

## Console testing

`tools/console_test.py <plan>` runs the deployed title hands-free: it writes the plan as `test.cfg`
into the title folder, launches through the `doomlaunch` payload (`tools/launcher`, put into
`/data/pldmgr/payloads/doomlaunch/`), pulls captured frames over TCP from the running game (the
app listens on 9119; the PC firewall blocks the other direction), de-tiles them to PNG in
`build/test/run/`, receives `doom.log` over the same connection when the game exits, prints it and
deletes `test.cfg`. Plan lines: `input <steps>`, `capture <frames>`, `frames <N>` (clean exit),
`present cpu`, `game <file>` (skip the launcher), `text <value>` (answer the keyboard prompt
without opening it), `reset` (delete the app's saved data in `/download0` first, for a first-run
state; `console-reset.cfg` does only that). Plans: `console-play.cfg` (menus, play, save, load), `console-cpu.cfg`,
`console-soak.cfg` (6 minutes), `console-import.cfg` (launcher, notice, folder listing and a 7Z
import from `http://192.168.0.10:8666/`), `console-autoimport.cfg`, `console-tnt.cfg`,
`console-ultimate.cfg`, `console-send.cfg` (opens the send screen for a minute; run
`tools/send.py <address> <files>` meanwhile).

## PS5 facts learned on hardware (FW 13.00)

- `downloadDataSize` 64 is rejected (`0x80a40087`, launch fails); 256 works. `/download0` is a
  fixed-size image of that size (`/user/download/PPSA99666/download0.dat`), not a folder the PC can
  read; large data belongs in `/app0`.
- The libc heap of a native app is small: large blocks must come from `mmap` (the zone and the
  archive libraries do).
- The sandbox refuses `access`, `chdir`, `opendir`, `dup`, `dup2` (EPERM). `open`, `rename` and
  `unlink` work, `/app0` is writable, and folders list through `sceKernelOpen` with `O_DIRECTORY`
  plus `sceKernelGetdents` (8-byte records: 32-bit inode, 16-bit length, type, name length). Saves
  use explicit `/download0/...` paths; output is captured by pointing `stdout`/`stderr` at a pipe
  (`fdopen`), and klog wants one line per `sceKernelDebugOutText`.
- A title can listen on TCP ports, but the sandbox refuses some with `EACCES` (8666 and 50000 of
  those tried; 8000, 9000, 9090, 9666, 18666 and 30000 work). BSD sockets come from `libkernel`.
- `sceHttp` with `sceSsl` works in the sandboxed title, plain and HTTPS, including `Range` request
  headers added with `sceHttpAddRequestHeader` (answers 206). `sceHttpReadData` returns only when
  the buffer is full or the body ends. About 12 MB/s from a PC on the same wired network.
- The SDK's FreeBSD headers do not match the console's C library everywhere: `MB_CUR_MAX` expands
  to `___mb_cur_max` (the library has `_Getmbcurmax`), and there is no `localtime_r`, `gmtime_r` or
  `timegm`. `assert` needs `__assert`, which is missing too.
- Scanout must be tiled (`sceVideoOutSetBufferAttribute2` tiling 0; 1 is `0x80290007`), 16 MB per
  1080p buffer. Pixel layout: 512x128 tiles with the bit interleave in `src/ps5/tiling.h`.
- GPU memory: direct memory type 12, protection 0x33, CPU writes flushed with `clflush` before GPU
  use and invalidated before CPU reads of GPU output (`ps5_cache_flush`).
- Compute runs as wave64 whatever the dispatch flag says, and the GPU mishandles the gfx10.3 `null`
  carry-out operand (writes are dropped). The kernel is built for gfx1010, wave64; `embed-kernel.py`
  rejects wave32. GPU page faults appear in klog as `GPU Protection fault ... addr(VA)`.
- After rest mode ShadowMountPlus can lose its install hook ("TitleDir bridge unavailable");
  restarting the ShadowMountPlus payload through the Payload Manager fixes it without a reboot.

## Controls

| Input | In game | In menus |
| --- | --- | --- |
| Left stick | Move and strafe (analog) | Navigate |
| Right stick | Turn (analog; speed follows the Mouse Sensitivity slider) | |
| D-pad | Move and turn (digital) | Navigate (repeats when held) |
| R2 | Fire | |
| Cross / Square | Use | Select (Cross), answers yes to prompts |
| Circle | Strafe modifier | Back, answers no to prompts |
| L1 / R1 | Previous / next weapon (zoom on the automap) | |
| L2 | Run | |
| Triangle / touchpad | Automap (Square toggles follow mode on the map) | |
| Options | Menu | Close menu |

Saving needs no keyboard: an empty slot is pre-filled with the level name, Cross saves.

Text names the face buttons with glyph characters (`GLYPH_CROSS` and the others in
`src/port/glyphs.h`, control characters 1 to 4) that Doom's menu font and the launcher draw as the
button symbols in their PlayStation colours. The first *Read This!* page shows this table instead of
id's keyboard help (`HELP1`, `HELP`); DOOM II keeps its *Read This!* entry whenever the WAD has the
`M_RDTHIS` graphic, which all of them do.

## Changes to id's code

- 64-bit: pointers stored in `int` (config string defaults, save game pointer fields, the
  `columndirectory` field of the on-disk texture struct), pointer arrays sized `count*4`
  (`r_data.c`, `p_setup.c`), table alignment through `int` casts, 16-byte zone alignment.
- Undefined behaviour and latent bugs: the unterminated `sprnames` list, unsequenced event queue
  updates, button array resets that cleared 8 bytes of pointers, save games written into the
  screen buffers (now a dedicated 4 MB buffer), the level-load sky check that compared the game
  mode with mission values (`pack_plut` equals `retail`, so The Ultimate DOOM got DOOM II skies).
- Portability: no `values.h`/`alloca.h`/sound server; config in the working directory (the port
  changes into the save directory at start).
- Game selection: `IdentifyVersion` takes the launcher's choice (`I_ChooseIwad`) instead of probing
  file names, sets `gamemission`, and the TNT and Plutonia level names and finale texts that id left
  behind `FIXME` comments are used.
- Behaviour: 1.9 demos play (the WADs' attract demos); empty save slots get a default name; saves
  are per game; default SFX channels 8 (the DOS default) instead of 3; Freedoom IWADs recognised;
  static limits raised (visplanes, drawsegs, sprites, openings, intercepts, plats, ceilings,
  buttons, scrollers) so limit-removing maps such as Freedoom's run; full-screen pictures wider
  than 320 pixels (the 2024 re-release's widescreen title and intermission screens) are drawn
  centred and clipped instead of being rejected; prompts name DualSense buttons instead of keys
  (`d_englsh.h`, `d_french.h`) and the first help page lists the controller controls.

## External code and tools (pinned)

- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  `b1315a9`: its `ps5-native-tool` (PIE to PS5 module, FSELF signing), its clean-room `libc.prx`,
  its intermediate PIE linker script, and the PS5 payload SDK v0.42 it fetches. Built in
  `.deps/native-app` by `tools/fetch-native-tools.sh`; nothing from it is copied into `src/`.
- [libarchive](https://www.libarchive.org/) 3.8.9, [xz](https://tukaani.org/xz/) 5.8.4 (liblzma),
  [zlib](https://zlib.net/) 1.3.2 and [qrcodegen](https://www.nayuki.io/page/qr-code-generator-library)
  1.8.0, fetched and checked by `tools/fetch-third-party.sh` into `.deps/third-party` and compiled
  into the game for both targets.
- clang/lld/llvm 18 (x86_64-sie-ps5 and amdgcn targets).
