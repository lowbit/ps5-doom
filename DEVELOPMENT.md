# DOOM for PS5 (native)

id Software's original `linuxdoom-1.10` source running as a native PS5 title: its own home-screen
icon, launched like a game, drawing through Sony's VideoOut and AGC (GPU) drivers, reading the
DualSense through ScePad and playing sound through AudioOut. Title ID `PPSA99666`.

All PS5, platform, audio and test code here is written for this project. The only external code
is id's game source and the build tools listed at the end.

## Layout

| Path | What |
| --- | --- |
| `src/doom/` | id's original game code with the 64-bit and portability fixes listed below |
| `src/port/` | Doom's `i_*` layer (main, system, video, sound, network, controller mapping) on top of the platform API |
| `src/platform/platform.h` | The platform API: time, log, files, video present, pad, rumble, audio output |
| `src/audio/` | Sound engine: SFX mixer, MUS sequencer with DMX-style voice allocation, OPL FM synth |
| `src/ps5/` | PS5 backend: startup (`crt0.c`), system, VideoOut, AGC compute presenter, pad, AudioOut |
| `src/ps5/present.cl` | GPU kernel: palette lookup, sharp-bilinear scaling to 1080p, tiled scanout writes (gfx1010, wave64) |
| `src/host/` | Headless Linux backend for testing on the PC: PNG frames, scripted pad, WAV audio |
| `test/` | `render_music` (MUS lump to WAV) and `present_preview` (runs the GPU kernel's math on the CPU) |
| `title/` | `param.json` and the icon |
| `tools/` | Tool fetch, shareware WAD fetch, kernel embedding, deploy |

## Build

Everything builds in the `ps5-doom-build` image (`docker/Dockerfile`: clang/lld/llvm 18, ninja, gdb).

```bash
docker build -t ps5-doom-build docker/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -w /src ps5-doom-build make -j16 ps5 host
```

`make ps5` fetches the pinned tools and the shareware WAD on first use, then:

1. compiles the game and the PS5 layer for `x86_64-sie-ps5` against the PS5 payload SDK headers;
2. compiles `present.cl` for `gfx1010` (wave64), links it and embeds it with `tools/embed-kernel.py`, which
   reads the register values from the compiler's kernel descriptor and fails the build if the
   kernel needs anything the presenter does not set up;
3. generates `libSceAgc` / `libSceAgcDriver` import stubs from `src/ps5/stubs/*.txt` (the SDK has none);
4. links a PIE, converts it to a PS5 module, signs the FSELF and assembles `dist/PPSA99666/`.

## Testing on the PC

`build/host/bin/doom` is the same game on the headless backend. Environment variables:
`DOOM_WADDIR`, `DOOM_SAVEDIR`, `DOOM_OUT` (frame output dir), `DOOM_CAPTURE` (frame numbers to
save as PNG plus raw indices and palette), `DOOM_FRAMES` (exit after N frames), `DOOM_AUDIO`
(absolute WAV path), `DOOM_INPUT` (pad script: `frame:BUTTON+BUTTON+LX=-32000:frames;...`).

```bash
DOOM_OUT=out DOOM_CAPTURE=120,900 build/host/bin/doom -timedemo demo1
```

For memory errors, build with AddressSanitizer:
`make host HOST_DIR=build/asan HOST_FLAGS="-O1 -g -fsanitize=address" HOST_CC="clang -fsanitize=address"`.

## On the console

`uv run --no-project python tools/deploy.py` uploads `dist/PPSA99666` to `/data/homebrew/PPSA99666`
(PS5Upload helper must be running), where ShadowMountPlus registers it. The package bundles Freedoom
(`tools/fetch-freedoom.sh`). id's IWADs (`DOOM.WAD`, `DOOM2.WAD`, `PLUTONIA.WAD`, `TNT.WAD`) dropped
into `/data/homebrew/PPSA99666/wads/` take priority; order: DOOM II-style id WADs, DOOM-style id
WADs, Freedoom 2, Freedoom 1; holding L2 at launch prefers the DOOM-style game. `make release` writes
`dist/PPSA99666.zip` and its `.sha256`.

The app is sandboxed: it reads `/app0` (its folder) and writes `/download0` (config, saves and
`doom.log`). Log lines also go to the kernel log (`sceKernelDebugOutText`), readable with klogsrv.
Fatal errors show as a system notification.

Presentation uses the AGC compute path by default. Hold **L1+R1** while the game starts to force the
CPU scaler. If a GPU frame does not complete within 500 ms, the game switches to the CPU scaler by
itself and logs why.

## Console testing

`tools/console_test.py <plan>` runs the deployed title hands-free: it writes the plan as `test.cfg`
into the title folder, launches through the `doomlaunch` payload (`tools/launcher`, put into
`/data/pldmgr/payloads/doomlaunch/`), pulls captured frames over TCP from the running game (the
app listens on 9119; the PC firewall blocks the other direction), de-tiles them to PNG in
`build/test/run/`, prints the log from klogsrv (port 3232, captured to `build/test/klog.txt`), then
deletes `test.cfg`. Plan lines: `input <steps>`, `capture <frames>`, `frames <N>` (clean exit),
`present cpu`. Plans: `test/console-play.cfg` (menus, play, save, load), `console-cpu.cfg`,
`console-soak.cfg` (6 minutes).

## PS5 facts learned on hardware (FW 13.00)

- `downloadDataSize` 64 is rejected (`0x80a40087`, launch fails); 256 works.
- The libc heap of a native app is small: large blocks must come from `mmap` (the zone does).
- The sandbox refuses `access`, `chdir`, `opendir`, `dup`, `dup2` (EPERM). `open` works. Saves use
  explicit `/download0/...` paths; output is captured by pointing `stdout`/`stderr` at a pipe
  (`fdopen`), and klog wants one line per `sceKernelDebugOutText`.
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

## Changes to id's code

- 64-bit: pointers stored in `int` (config string defaults, save game pointer fields, the
  `columndirectory` field of the on-disk texture struct), pointer arrays sized `count*4`
  (`r_data.c`, `p_setup.c`), table alignment through `int` casts, 16-byte zone alignment.
- Undefined behaviour and latent bugs: the unterminated `sprnames` list, unsequenced event queue
  updates, button array resets that cleared 8 bytes of pointers, save games written into the
  screen buffers (now a dedicated 4 MB buffer).
- Portability: no `values.h`/`alloca.h`/sound server; IWAD search through `I_GetWadDirs`; config in
  the working directory (the port changes into the save directory at start).
- Behaviour: 1.9 demos play (the WADs' attract demos); empty save slots get a default name;
  default SFX channels 8 (the DOS default) instead of 3; Freedoom IWADs recognised; static limits
  raised (visplanes, drawsegs, sprites, openings, intercepts, plats, ceilings, buttons, scrollers)
  so limit-removing maps such as Freedoom's run.

## External tools (pinned)

- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  `b1315a9`: its `ps5-native-tool` (PIE to PS5 module, FSELF signing), its clean-room `libc.prx`,
  its intermediate PIE linker script, and the PS5 payload SDK v0.42 it fetches. Built in
  `.deps/native-app` by `tools/fetch-native-tools.sh`; nothing from it is copied into `src/`.
- clang/lld/llvm 18 (x86_64-sie-ps5 and amdgcn targets).
