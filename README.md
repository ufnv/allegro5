# ICBM modifications for Allegro 5

Everything ICBM needs from a patched Allegro, packaged so that a new Allegro
release only needs **copy `overlay/` + apply the small `hooks.patch`**.

- `overlay/` – new files (all ICBM code). Copied verbatim, never conflict.
- `hooks.patch` – small edits to upstream files: `#include "*_icbm.inc"`
  lines, prototypes in public headers, and the few lines that must sit inside
  upstream functions. Every hook line is marked `ICBM`.
- `BASE.txt` – the Allegro release the package was last made against.
- `tools/apply_icbm.py` – applies the package and verifies it.
- `tools/make_package.py` – regenerates `overlay/` + `hooks.patch` from a git branch.
- `tools/configure.cmd`, `tools/build.sh` – x64 static MSVC build like ICBM's.
- `selftest/` – runtime test of every change (Direct3D; see below).
- `deps/` – the third-party headers and static x64 `/MT` libraries Allegro is
  built with (zlib, png, jpeg, webp, freetype, ogg, vorbis, theora, FLAC, opus,
  dumb, physfs). The configure scripts, the self-test and `stage_for_icbm.py`
  find it next to themselves; staging copies it into `ICBM\allegro5\deps`.

Current state: made against **5.2.11.3** (`9e58c5e03`). The same changes are
committed topic by topic on branch `icbm-hdr` of `../allegro5_latest/allegro5`
(the older branch `icbm` lacks topics 11-14).

## Where it lives

Both are branches of the fork https://github.com/ufnv/allegro5 (`origin`;
liballeg/allegro5 is `upstream`):

- `icbm-hdr` – Allegro 5.2.11.3 with every topic, what ICBM is built from;
  checked out in `XCode\allegro5_latest\allegro5`.
- `icbm-package` – this folder (an orphan branch, a worktree of the same
  repository at `XCode\allegro5_icbm`). Stored byte for byte (`* -text`).

On another computer:

```bash
git clone https://github.com/ufnv/allegro5.git allegro5_latest/allegro5
git -C allegro5_latest/allegro5 switch icbm-hdr
git -C allegro5_latest/allegro5 worktree add ../../allegro5_icbm icbm-package
```

Then build and stage as below (needs VS 2026 and the DirectX SDK in
`C:\dxsdk`). To build ICBM only, copying a staged `ICBM\allegro5` folder is
enough.

## Apply to a new Allegro release

```bash
git -C <allegro> switch -c icbm-<version> <version-tag>
python tools/apply_icbm.py <allegro>
# conflicts? fix them (see topics below), then:
python tools/apply_icbm.py <allegro> --verify
```

Then build (`tools/configure.cmd`, `tools/build.sh`), run `selftest/`,
commit, and regenerate the package from the new branch:
`python tools/make_package.py <allegro> <version-tag> icbm-<version>`.

The `.inc` files are `#include`d into the upstream `.c` file that owns the
code, so they can use its `static` helpers and need no CMake changes.

## Topics

| # | Topic | ICBM uses it in | New file | Hooks in |
|---|-------|-----------------|----------|----------|
| 1 | NULL-tolerant font functions | fonts released on D3D reset | – | `addons/font/text.c` |
| 2 | `al_load_existing_bitmap_flags_f` (PNG into an existing bitmap, optional alpha map) | `graph/BigTexture.cpp`, `LAZYGIF.CPP`, `GIF.CPP`, `webp.cpp`, `ICBM/wrk.cpp` | `src/bitmap_io_icbm.inc`, `addons/image/png_icbm.inc` | `bitmap_io.h/.c`, `png.c`, `iio.c`, `aintern_image.h` |
| 3 | `al_{lock,unlock,destroy}_{vertex,index}_buffer_TS` (worker threads); FLOAT_4 position, FLOAT_3 texcoord | `World/Occupation.cpp`, `MissileTrajectory.cpp`, `EmulatedExplosionBuffer.cpp`, `graph/BigTexture.cpp`, `BorderDrawing.cpp` | `addons/primitives/primitives_icbm.inc` | `allegro_primitives.h`, addon `primitives.c`, `src/primitives.c` |
| 4 | TTF: 4 px glyph border, `StoreTUTVForGlyph` + `DrawGlyphCallback`, `GlyphAlignCenter` | `graph/font.cpp`, `World/Map.cpp` | `addons/ttf/ttf_icbm.inc` | `addons/ttf/ttf.c` |
| 5 | `ALLEGRO_MIN` / `ALLEGRO_MAX` blend ops | `World/Map.cpp` (MAX) | `include/allegro5/internal/aintern_blend_icbm.h` | `blender.h`, `aintern_blend.h`, `ogl_draw.c`, `d3d_disp.cpp` |
| 6 | `ALLEGRO_PRIMITIVE_STD_Z` (Z of rotated/scaled bitmaps) | `graph/Vertex.cpp`, `LAZYGIF.CPP` | – | `src/bitmap_draw.c` |
| 7 | `ALLEGRO_SHADER_AUTO` → HLSL SM 3.0, reported as `ALLEGRO_SHADER_HLSL` | `graph/Vertex.cpp` (`InitShader`) | – | `src/shader.c` |
| 8 | `SEND_REPEAT_EVENTS` (drop auto-repeat KEY_CHAR) | `win/KEYB.CPP` | – | `src/win/wkeyboard.c` |
| 9 | D3D device reset hooks `ReleaseAllVideoMemoryPossible()` / `DisplayResetFinished()`; display-thread message pump | `graph/Graph.cpp` | `src/win/d3d_disp_icbm.inc` | `src/win/d3d_disp.cpp` |
| 10 | Adapter/monitor 0 = primary monitor (matches Direct3D) | `ICBM/ScreenCfg.cpp`, `graph/VIDEO.CPP` | `src/win/wsystem_icbm.inc` | `src/win/wsystem.c` |
| 11 | HDR output: `al_set_new_display_hdr` (scRGB/HDR10) switches to Direct3D 9Ex, renders into shared FP16 targets and presents them through a D3D11 swap chain; D3D9Ex has no managed pool, so `D3DPOOL_MANAGED` → `_al_d3d_icbm_managed_pool()` | `graph/VIDEO.CPP` (`Config::UseHDR`), `World/explosion.cpp`, `World/EmulatedExplosionElements.cpp`, `World/Range.cpp` | `src/win/d3d_hdr_icbm.inc` | `allegro_direct3d.h`, `aintern_direct3d.h`, `aintwin.h`, `d3d_disp.cpp`, `d3d_bmp.cpp`, `wwindow.c` |
| 12 | bstrlib `_al_bvcformata`: terminate the output after MSVC `_vsnprintf`, which leaves it unterminated on overflow, so the following `strlen` read past the buffer (crash in `al_ustr_newf` of a >4 KB shader source in `hlsl_attach_shader_source`) | `graph/Vertex.cpp` (`InitShader`), any `al_ustr_newf` | – | `src/misc/bstrlib.c` |
| 13 | Direct3D indexed draws: the index range (`DrawIndexedPrimitiveUP`'s lowest and highest vertex). `al_draw_indexed_prim_range(..., num_vertices)`: the caller gives it (vertices 0 .. num_vertices - 1), no scan; a per-thread hint set around `al_draw_indexed_prim` (`_al_icbm_set/get_indexed_vertex_count`), ignored by OpenGL. Otherwise found four indices at a time with SSE2 (`icbm_index_range`); upstream's loop branched on every index, 3.5% of ICBM's main thread with its big vertex batches. Same result | `graph/Vertex.cpp` (`DrawVertexBatch`: the range call), every `al_draw_indexed_prim` (the scan) | `src/win/d3d_disp_icbm.inc`, `src/primitives_icbm.inc` | `src/win/d3d_disp.cpp`, `src/primitives.c`, `aintern_primitives.h`, `allegro_primitives.h` |
| 14 | Direct3D object made again: a display created after all displays were destroyed gets a new IDirect3D9(Ex) (9Ex if the process is on it), because an IDirect3D9 knows only the adapters of its creation; after a remote desktop reconnect the old device stays lost for good (D3DERR_DEVICELOST) and no device can be made on the old object | `win/EVENTS.CPP` (lost-device watchdog, `ChangeDisplay` in `ICBM/ScreenCfg.cpp`) | `src/win/d3d_hdr_icbm.inc` (`icbm_d3d_refresh_object`) | `src/win/d3d_disp.cpp` |

Conflict hints: topic 4 (`ttf.c`) is the usual one. Take upstream's new code,
then turn every 2 px border literal into `BORDER_SIZE` (`+ 2` → `+ BORDER_SIZE`,
`± 4` → `± 2 * BORDER_SIZE`) and route `render_glyph`'s draw call through
`icbm_draw_glyph()`. `apply_icbm.py` lists any leftover literals. For the other
topics, re-add the marked `ICBM` lines at the equivalent place.

## Errors fixed compared with the original hand-made patch

- PNG alpha map was written once per Adam7 pass → buffer overrun on
  interlaced PNGs. Now written on the last pass only.
- PNG larger than the existing bitmap overran it → now rejected.
- `al_load_*_existing_*` crashed for formats without that loader → returns NULL.
- Failed bitmap lock in the PNG loader crashed → reported, returns NULL.
- `al_get_*_dimensions(NULL font)` left the outputs uninitialised → zeroed.
- TTF glyph callback and D3D hooks were hard external symbols, so every
  program linking Allegro had to define them (the examples needed stubs) →
  weak no-op defaults (`/alternatename` on MSVC, `weak` on GCC/Clang). The
  game's own definitions still win.
- D3D display thread: removing `al_rest(0.001)` made it spin a CPU core at
  100 % → now drains all messages and waits with `MsgWaitForMultipleObjects`
  (≤ 1 ms). Measured: 0.03 s CPU for 2 s idle.
- `ALLEGRO_MIN/MAX` only worked on GL/D3D → the software blender (memory
  bitmaps) supports them too, and no longer skips reading the destination.
- `_TS` functions: upstream moved buffer backends behind the display vtable,
  so they now use the backend that created the buffers (captured at creation).
- Faux-fullscreen `temp &= ~WS_EX_TOPMOST` cleared a bit in `GWL_STYLE` (an
  extended style in the wrong field; the window is never topmost anyway) → dropped.
- Primary monitor is detected with `DISPLAY_DEVICE_PRIMARY_DEVICE` instead of
  "the monitor at (0,0)".

## Dropped (not used by ICBM, or already fixed upstream)

- `ALLEGRO_PIXEL_FORMAT_GR_F16` + converters (`convert.c`,
  `aintern_convert.h`, `make_converters.py`, `pixels.c`, GL/D3D tables):
  never used by ICBM.
- `al_load_alpha_bitmap_flags_f`, `al_register_bitmap_loader_alpha_f`,
  `al_load_existing_bitmap_f`: unused.
- CMake 3.5 minimum: fixed upstream. (The bstrlib `vsnprintf` truncation
  fix was dropped here too as "fixed upstream", but it is not: back as topic 12.)
- `*_MINIMAL` shader platforms turned into full ones: unused, and wrong.
- `EXT_ALLEGRO_STORE_TEX_COORDS` / `TU_TV` blocks in `ogl_bitmap.c` and
  `d3d_bmp.cpp`: were `#if 0`.
- `prim_directx.cpp` min/max index loop: equivalent rewrite, no effect.
- Example/test edits and link stubs: no longer needed.
- **Video addon fork**: ICBM uses its own player, `graph/OGV_video.c`
  (Windows `graph/Graph.vcxproj` and Xcode). Its API is renamed to `icbm_*`
  (`ICBM_VIDEO`, `icbm_open_video_f`, …, declared in `graph/OGV_video.h`), so
  the stock `allegro_video` addon stays original and can be linked alongside.
  The old fork is kept in `../allegro5/addons/video_icbm_fork_backup/`.

## Using the build in ICBM

ICBM's projects (everything in `ICBMmber.sln`, plus e1p and LPNG; x64 only)
look for Allegro only in `ICBM\allegro5\include` and `ICBM\allegro5\lib`, and
for its third-party libraries (zlib, png, jpeg, webp, freetype, ogg, vorbis,
theora, FLAC, opus, dumb, physfs) only in `ICBM\allegro5\deps\{include,lib}`.
`ICBM\allegro5` is generated and git-ignored (`/allegro5/` in ICBM's `.gitignore`):

```
python tools/stage_for_icbm.py ../allegro5_latest/allegro5 ../ICBM
python tools/set_icbm_allegro_paths.py ../ICBM      # only if the paths were changed
```

## Notes for ICBM

- macOS: `graph/stub.c` defines `ALLEGRO_PRIMITIVE_STD_Z`. Remove it once
  the Mac build links a patched Allegro (topic 6), or the link fails with a
  duplicate symbol.
- `allegro_video-static.lib` / `liballegro_video-static.a` no longer needs to be linked.
- `_TS` buffers on **OpenGL** (Windows OpenGL mode, macOS): a write-only
  `al_lock_*_buffer_TS` just returns CPU memory, so filling from a worker is
  fine. The unlock uploads with `glBufferSubData`, which needs the GL context,
  and only the display's thread has it. So unlock on the drawing thread.
  ICBM already does this: workers fill, and `Swap()` / `AquireDrawBuffer()` /
  `AquireEEDrawingBuffer()` unlock on the main thread. An unlock on a worker
  silently loses the upload on GL (verified by the self-test); on Direct3D it works.
- HDR (topic 11), Direct3D only, windowed or `ALLEGRO_FULLSCREEN_WINDOW`:
  - Everything drawn keeps its SDR meaning: 0..1 is sRGB, shown at Windows'
    "SDR content brightness". Values above 1 (additive blending, colours or
    uniforms > 1) are extra light: `max_nits / sdr_white_nits` reaches the peak
    (`Video::HDRHeadroom` in ICBM). Needs `ALLEGRO_PROGRAMMABLE_PIPELINE`
    (without it Allegro packs vertex colours into 8 bits); with it, colours
    above 1 survive both COLOR and TEXCOORD interpolators.
  - `ALLEGRO_HDR_AUTO` (ICBM's default, `UseHDR auto`) checks through DXGI
    before the display is created whether Windows HDR is on for any monitor
    (the window may start on an SDR monitor and be moved to the HDR one; the
    output is SDR there); if not, nothing changes (plain Direct3D 9, no 9Ex).
    `ALLEGRO_HDR_SCRGB` (`UseHDR true`) always switches to 9Ex, so HDR can
    start when Windows HDR is switched on later.
  - Once set up, the HDR path (FP16 targets, D3D11 present) stays for the
    display's life: after a flip-model swap chain has presented to a window,
    D3D9 Present no longer shows there (the picture froze when we went back).
    When Windows HDR is off for the monitor showing the window, or after
    `al_set_display_hdr_enabled(false)`, the output is SDR: white = scRGB 1.0,
    nothing above it, and `al_get_display_hdr_levels()` reports equal levels
    (no headroom). The check runs on `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`,
    window moves/DPI changes and activation (hook in `wwindow.c`), again a
    second later, and every 5 s as a safety net. ICBM follows it after each
    flip (`Video::UpdateHDRState()`).
  - D3D11/d3dcompiler_47 missing or any setup/present failure: normal D3D9
    path for good; the device stays 9Ex then. ICBM code creating D3D
    resources itself must not use `D3DPOOL_MANAGED` on a 9Ex device (ICBM has
    no such code any more).
  - Three rotating targets, so neither GPU waits. A flipped frame is queued
    and presented as soon as the GPU has finished it: by the flip if it is
    done by then, otherwise by the display thread (every millisecond; this
    also shows a loading screen drawn once). The flip does not wait for the
    GPU, only when the game is two frames ahead (it waited for the previous
    frame before 2026-10-05, which made the frame pacing follow every slow GPU
    frame: a stepping inertial scroll in ICBM). Without `ALLEGRO_VSYNC` 1 it presents
    with tearing allowed; otherwise the flip model caps a borderless
    full-screen window at the refresh rate.
    Locking or drawing from the backbuffer bitmap is unsupported while HDR is on.
  - Monitors report unreliable peaks (6095 and 128000 nits seen on one
    monitor), so anything above 4000 nits becomes 1000; override with
    `al_set_display_hdr_levels()`.

## Self-test

```bat
python selftest\make_png.py selftest
selftest\build_selftest.cmd <allegro source dir> <allegro build dir>
selftest\icbm_selftest.exe selftest <allegro>\examples\data\DejaVuSans.ttf
```

Checks that need no display run once. These cover PNGs (plain and interlaced)
loaded into an existing bitmap with the alpha map and an overrun canary,
software MIN/MAX, and monitor 0 = primary. They also check that, for every adapter number,
Direct3D's monitor (`GetAdapterMonitor`) is the same one as `al_get_monitor_info()`.
This is the "window opens on the wrong monitor" bug, which is still present in
stock Allegro 5.2.11.3, so run the self-test on a multi-monitor machine.

The display checks then run twice, on a **Direct3D** display and on an
**OpenGL** (WGL) display:
- MIN/MAX on video bitmaps
- `_TS` buffers filled from a thread without a display, unlocked on the main
  thread; also unlocked in the worker (must work on D3D, reported on GL)
- FLOAT_4/FLOAT_3 vertex decls
- NULL fonts
- the game's `DrawGlyphCallback` overriding the default
- text rendering
- the AUTO shader platform (HLSL on D3D, GLSL on GL), building and using it
- `ALLEGRO_PRIMITIVE_STD_Z` reaching the depth test
- idle CPU use

Last, an HDR Direct3D display (topic 11; it switches the process to Direct3D
9Ex, so it runs after the others). It repeats MIN/MAX, `_TS` buffers and
shaders on the 9Ex device, creates and locks a DXT5 bitmap, and, when Windows
HDR is on for the monitor, checks the FP16 backbuffer, additive blending and
colours above 1 (vertex colours, COLOR0 and TEXCOORD1 uniforms) reaching it,
target rotation on flip, 60 presented frames, an idle present, switching the
output to SDR and back (`al_set_display_hdr_enabled`, with the screen pixel
the window shows in each state), level overrides surviving that, a resize, and that
`ALLEGRO_HDR_AUTO` picks scRGB. With Windows HDR off those checks are skipped
with an `info` line. Run it with the desktop unlocked: on the lock screen
plain Direct3D 9 devices cannot be created and screen pixels cannot be read.
`set ICBM_SELFTEST_HDR_ONLY=1`
runs only this part.
