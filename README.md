# MelonDS Android with LuaScript — a melonDS Android side quest

*Repository: `MelonDS_Android_wLuaScript` · unofficial fork*

A personal fork of [melonDS Android](https://github.com/rafaelvcaetano/melonDS-android) that brings **DeSmuME-style Lua
scripting** to melonDS on Android, plus a handful of quality-of-life and UI changes made along the way.

It is shared as a "side quest": not a pull request, not a replacement for the official app, just something the people
working on melonDS Android might find interesting to look at, borrow from, or learn from (including from its mistakes).
The original README is kept in [README-upstream.md](README-upstream.md).

> **Status:** experimental and lightly tested. It has been used on a single device (Samsung Galaxy S25+, Android 16,
> arm64) with Pokémon Platinum. Read [Known issues](#known-issues) before relying on it.

## Contents

- [Base version](#base-version)
- [Summary of changes](#summary-of-changes)
- [1. Lua scripting: melonDS meets DeSmuME](#1-lua-scripting-melonds-meets-desmume)
- [2. Fast-forward frameskip](#2-fast-forward-frameskip)
- [3. Restyled on-screen controls](#3-restyled-on-screen-controls)
- [4. The naughty little button (swept under the rug)](#4-the-naughty-little-button-swept-under-the-rug)
- [5. Diagnostics: crash handler and Settings → Log](#5-diagnostics-crash-handler-and-settings--log)
- [6. Build changes](#6-build-changes)
- [Bugs found along the way](#bugs-found-along-the-way)
- [Known issues](#known-issues)
- [File map](#file-map)
- [Building](#building)
- [Credits, acknowledgements and legal](#credits-acknowledgements-and-legal)

## Base version

| Component | Upstream repository | Commit |
|---|---|---|
| App | `rafaelvcaetano/melonDS-android` | `c42995caff0144f37ad9791dc9bc96d82e8a6f65` |
| Core | `rafaelvcaetano/melonDS-android-lib` | `431ab4bd0003c4356e25fce89640ae1004579b9b` |

The upstream git history is **not** included: the submodules (`melonDS-android-lib`, `oboe`, `enet`, `faad2`) are
vendored as plain folders. To see exactly what changed, diff this tree against upstream at the commits above.

The debug variant uses the package `me.magnum.melonds.dev`, so it installs **next to** the official app.

## Summary of changes

| Area | What | Where |
|---|---|---|
| Lua | DeSmuME-compatible Lua 5.1 engine, drawn on top of the game | `app/src/main/cpp/lua/`, `lua51/`, `ui/emulator/lua/` |
| Lua UI | "Lua scripts" pause-menu entry, collapsible 1–0 keypad, Appearance settings, console | `LuaScriptController.kt` |
| Emulation | Optional frameskip while fast-forwarding | `MelonInstance.cpp`, core `GPU.cpp`, `GPU3D.cpp` |
| Controls | Thin vector buttons, redistributed default layout, pause button in the default layout | `res/drawable/*.xml`, `DefaultLayoutProvider.kt` |
| Controls | Hinge (lid) button hidden from the default layout | `DefaultLayoutProvider.kt` — see [section 4](#4-the-naughty-little-button-swept-under-the-rug) |
| Diagnostics | Native crash handler with breadcrumbs, Settings → Log | `cpp/debug/CrashHandler.*`, `debug/CrashLogger.kt`, `LogPreferencesFragment.kt` |
| Build | Optimized native code in debug builds, optional slim APK | `app/build.gradle.kts`, `app/CMakeLists.txt` |

## 1. Lua scripting: melonDS meets DeSmuME

**Goal:** run Lua scripts written for **DeSmuME** unchanged on melonDS Android. The main use case is the Pokémon
RNG-manipulation overlays (for example Platinum's `Pt_RNG_DeSmuMe.lua`), which read RAM every frame and draw text and
boxes over the screens. In practice it is a small "DeSmuME compatibility layer" grafted onto melonDS.

### Lua runtime

- **Lua 5.1.5** (the version DeSmuME embeds) is vendored in `app/src/main/cpp/lua51/` and built as a static library.
- The sources are renamed to `.cpp` and **compiled as C++**. Lua then reports errors with C++ exceptions
  (`LUAI_THROW`) instead of `longjmp`. This matters because Lua errors unwind through C++ frames of the bindings.
- `_FORTIFY_SOURCE` is disabled for this library only. See [Bugs found along the way](#bugs-found-along-the-way):
  bionic's FORTIFY aborts on perfectly valid Lua string accesses.

### DeSmuME-compatible API (`LuaEngine.cpp`)

| Library | Implemented | Notes |
|---|---|---|
| `memory` | `readbyte`/`word`/`dword` (+ `signed`/`unsigned`/`long` aliases), `readbyterange`, `writebyte`/`word`/`dword` | ARM9 view: ITCM → DTCM → main RAM (+ mirrors). Anything else reads 0 |
| `bit` | `tobit`, `bnot`, `band`, `bor`, `bxor`, `lshift`, `rshift`, `arshift`, `rol`, `ror`, `bswap`, `tohex` | LuaBitOp semantics (wrap modulo 2^32), as embedded by DeSmuME |
| `gui` | `text`, `box`, `line`, `pixel`, `getpixel`, `parsecolor`, `opacity`, `transparency`, `register`, `popup` (+ DeSmuME aliases) | Colours as numbers, names, `#RRGGBB[AA]`, or `{r,g,b,a}` tables |
| `input` / `keyboard` | `get` / `read` | Keys from the on-screen 1–0 keypad and a physical keyboard; touch as `xmouse`/`ymouse`/`leftclick` |
| `joypad` | `get` / `read`, `set` / `write` | `set` uses DeSmuME semantics: `true` presses, `false` forces a release, absent keys are left to the user. It applies to the next frame only, so call it every frame to hold a button |
| `stylus` | `get` / `peek` / `read`, `set` / `write` | `{x, y, touch}` in bottom-screen coordinates; like `joypad.set`, one frame per call |
| `emu` / `desmume` | `frameadvance`, `framecount`, `lagcount`, `emulating`, `reset`, `registerbefore`/`after`/`exit`, `message`, `getdir` | `pause`, `speedmode`... are no-ops |
| globals | `AND`, `OR`, `XOR`, `SHIFT`, `BIT` | FCEUX-style helpers that some DeSmuME scripts still use |
| stubs | `memory.register*`, `savestate.*`, `movie.*` | No effect |

- **`emu.frameadvance()`:** the script's main chunk runs as a coroutine that yields once per emulated frame. Scripts
  written as `while true do ... emu.frameadvance() end` and scripts using `gui.register(fn)` both work.
- **File access:** relative paths in `io.open`, `io.lines`, `dofile`, `require`... resolve to a per-script working
  folder, `Android/data/<package>/files/lua/<script name>/`. `os.execute` only supports `mkdir`, which RNG scripts use
  to create their save folders. Everything else is refused with a message.

### Threading model

- Requests (load, stop, key states, output polling, status) can come from any thread and are guarded by mutexes.
- The Lua state is **only touched on the emulator thread**, inside `MelonInstance::runFrame()`:
  - `onFrameStart()` handles pending load/stop requests and `emu.registerbefore`;
  - `onFrameEnd()` runs `emu.registerafter`, resumes the main coroutine and runs the `gui.register` callback.
- `emu.reset()` is deferred and executed by `runFrame()` before the next frame.

### Drawing

- `LuaCanvas` is a CPU RGBA canvas of 256×384 pixels in **DeSmuME coordinates**: negative `y` is the top screen and
  `0..191` the bottom one. Text uses a public-domain 5×7 font (X11 misc-fixed) with a 1 px outline, like DeSmuME.
- `LuaOverlayRenderer` uploads the canvas to a texture and blends it onto the frame texture through an FBO, after the
  renderer has written the frame. The frontend's frame layout is top screen, 2 empty rows, bottom screen, all scaled.
  It saves and restores every piece of GL state it touches, and works the same with the Software, OpenGL and Compute
  renderers.
- A global opacity (user setting) is applied in the shader (`uOpacity`).

### JNI surface (`MelonEmulator.kt`)

`loadLuaScript(name, ByteArray, workDir)`, `stopLuaScript()`, `setLuaKeyState(key, pressed)`, `pollLuaOutput(): ByteArray`,
`getLuaStatus()`, `setLuaOverlayOpacity(float)`. Source and output travel as raw UTF-8 bytes to avoid JNI's modified UTF-8.

### User interface (`LuaScriptController.kt`)

- **Pause menu → Lua scripts:**
  - **Load script…** uses the Storage Access Framework and keeps a persistent read permission.
  - **Reload *last script***, **Stop script** and **Show/hide script keys**.
  - **Appearance…** opens the appearance settings.
  - **Console** shows `print` output and errors with a traceback. Errors also appear as toasts.
- The entry is hidden in RetroAchievements hardcore mode, because scripts can read and write memory.
- **Script keypad:** a small draggable "1–0 ▼" button.
  - Tapping it opens or closes keys 1–0, which scripts read through `input.get()`.
  - By default the keys form a column that splits into several columns when the screen is too short, for example in
    landscape. It is rebuilt on rotation.
  - Keys use haptic feedback and a press animation.
- **Appearance settings:** key size, opacity, colour theme, layout (column, row or 2×5 grid), quick positions,
  vibration, and the opacity of the script's drawings. Changes are previewed live behind the dialog.

## 2. Fast-forward frameskip

New setting: **Frameskip while fast-forwarding**, on by default. While fast-forwarding, only the frames needed for
about 60 FPS are rendered; the others are emulated without 2D/3D rendering. At normal speed nothing changes.

How it works:

- **Core, `GPU.h`:** two new flags. `SkipRendering` skips 2D drawing and composition of the current frame.
  `SkipNextFrame` tells the GPU that the next frame will be skipped too.
- **Core, `GPU.cpp`:** `DrawScanline`, `DrawSprites` and the accelerated `Blit` are skipped when requested.
- **Core, `GPU3D.cpp`:**
  - `VCount215` doesn't start the 3D render of a frame that will be skipped (`Skipped3DRender`);
  - `VCount144` doesn't wait for a render that never started;
  - `ForceNextRender` prevents reusing a stale 3D output.
- **Invariant:** `SkipNextFrame` of frame N must equal `SkipRendering` of frame N+1. Otherwise the threaded software 3D
  renderer, which synchronizes 2D and 3D per scanline, can desynchronize or deadlock. To keep it, `runFrame()` decides
  one frame in advance, based on the measured real frame interval.
- Frames needed by rewind snapshots are never skipped.

This part is the least tested of the fork.

## 3. Restyled on-screen controls

The original controls are flat grey PNGs: filled shapes and bold letters. They were replaced with **vector drawables**
in a thin outline style: a translucent fill, a ~1.5 dp outline, and Roboto labels converted to paths.

- **Same resource names** as the PNGs they replace (`buttons`, `keypad`, `button_l`, `button_start`, `ic_touch_enabled`...).
  The layout editor, toggle states and the rest of the app work unchanged.
- **Generated, not hand-drawn:** `tools/controls/gen_controls.py` builds every drawable from a few style constants.
  It uses fontTools to turn Roboto glyphs into `pathData`. `preview_controls.py` and `preview_layout.py` render previews
  with matplotlib, so a style change takes minutes.
- **Default layout redistributed** (`DefaultLayoutProvider.buildControls`, shared by portrait and landscape):
  - L/R sit in the top corners of the controls area, with real margins;
  - the utility buttons are centred between L and R, with adaptive spacing;
  - the D-pad and the face buttons are sized to the available space (110–150 dp) and, in portrait, centred vertically
    in the free space below the screens instead of being glued to the bottom edge;
  - Select/Start are centred between the D-pad and the face buttons.
- **Pause button** added to the default utility row. It opens the pause menu, which was otherwise only reachable with
  the system back gesture.
- Custom layouts keep their positions and only get the new graphics. The folding-device layouts only received the
  pause button.

## 4. The naughty little button (swept under the rug)

In fairness to anyone reading this: **one problem was hidden, not fixed.**

- **The symptom:** after the controls restyle, tapping the leftmost utility button (the one right after L) made the
  emulated screens flash with stripes, like a TV with bad reception, and the audio slowed down.
- **The cause of the confusion:** that button is the **hinge / lid toggle**. Closing the lid normally puts DS games to
  sleep, so part of what was seen may be expected. However, the restyled icon showed two stacked rectangles, which
  looks like *swap screens*, so it was very easy to tap by mistake. Earlier in development there was also a real
  slowdown with similar symptoms: native code built at `-O0`, see [Build changes](#6-build-changes).
- **What was not done:** whether the stripes are plain sleep behaviour, an upstream issue, or an interaction with this
  fork's changes (frameskip or overlay) was **never investigated**.
- **The "fix":** `LayoutComponent.BUTTON_HINGE` is **commented out** of the default utility row in
  `DefaultLayoutProvider.buildControls`, with a note explaining why. Nothing else changed:
  - the button still exists, still works the same way, and can still be added from the layout editor;
  - the folding-device layouts still include it.

If you work on lid/sleep handling, this might be worth a look. Un-commenting one line brings the button back.

## 5. Diagnostics: crash handler and Settings → Log

This was added to debug crashes on a phone that was never connected to a PC (no `adb logcat`).

- **Native crash handler** (`cpp/debug/CrashHandler.cpp`):
  - installs `sigaction` handlers for SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE and SIGTRAP;
  - writes the signal, fault address, thread name, the last **breadcrumb**, the registers, and two backtraces: one from
    `_Unwind_Backtrace`, and a frame-pointer walk that reads memory with `process_vm_readv`, so a broken chain cannot
    fault again;
  - prints each frame as `library +offset (symbol)`, so it can be symbolized with `llvm-addr2line` against the
    unstripped `.so` of the same build;
  - then hands the signal back to the previous handler, so debuggerd still produces its tombstone.
- **Breadcrumbs:** `CRASH_BREADCRUMB("...")` marks what the emulator thread is doing, for example "Lua: loading script",
  "Overlay: drawing over the frame" or "runFrame: emulating frame". This is how the FORTIFY crash was pinned down.
- **`CrashLogger.kt`:**
  - keeps a session log (activity lifecycle, script loading, script output);
  - builds a report with the native or Java crash, `ApplicationExitInfo` (API 30+) and the tail of the app's logcat;
  - writes it when a game closes normally, or at the next app start after a crash.
  - Only the **latest** report is kept.
- **Settings → Log:** view, copy, share or delete the latest report. Nothing pops up automatically.

## 6. Build changes

- **Optimized native code in debug builds:** the debug build type passes `-DCMAKE_BUILD_TYPE=RelWithDebInfo` to CMake.
  - With the stock debug configuration, the whole core (CPU, JIT, 2D/3D renderers) is compiled **without
    optimizations**. On a flagship phone that meant running below 1×, slowed-down audio and flickering, striped frames.
  - Anyone testing upstream debug builds on a device may want to be aware of this.
- **`-Pslim`:** an optional Gradle property for the debug build type. It enables R8 and resource shrinking and builds
  arm64-v8a only, giving an APK of ~17 MB instead of ~51 MB.
- **Version suffix:** `versionNameSuffix = "-lua-vN"` on the debug build type.
- **`app/CMakeLists.txt`:**
  - adds the `lua51` static library (with `-U_FORTIFY_SOURCE`);
  - adds the Lua engine, canvas, overlay renderer and crash handler sources;
  - links `lua51`.

## Bugs found along the way

| Bug | Cause | Fix |
|---|---|---|
| Any Lua script crashed on load: `FORTIFY: strchr: prevented read past end of buffer` in `lgc.cpp` `traversetable` | Lua stores string bytes right after the `TString` header and reaches them via `ts + 1`. Bionic's FORTIFY (`pass_object_size`) sees that pointer as past the end of the header and aborts. It never showed up in host testing (glibc) | `-U_FORTIFY_SOURCE` for `lua51` only. Lua's ~50 string calls on its own buffers are valid |
| Emulation below 1×, audio slowdowns, striped frames | Debug variant compiled native code at `-O0` | `RelWithDebInfo` for native code in debug |
| Script keypad showed only "1" and half of "2" | A `MATCH_PARENT` child inside a `WRAP_CONTENT` vertical `LinearLayout` is ignored when measuring width, so the panel was as wide as its toggle button | Explicit `WRAP_CONTENT` layout params |
| APK grew to 53 MB with an 18 MB payload | AGP's incremental packaging reuses the old file and leaves unused space in it | Delete the APK and the `incremental/package*` folder before packaging |

## Known issues

- The **hinge button** issue from [section 4](#4-the-naughty-little-button-swept-under-the-rug) is not investigated.
- **Lua memory access** only covers the ARM9 view of ITCM, DTCM and main RAM. I/O, VRAM and ARM7 memory read 0.
- **Memory writes from Lua** do not invalidate blocks already compiled by the JIT.
- `memory.register*`, `savestate.*` and `movie.*` are stubs.
- Script keys F1–F12 and Shift, used by some RNG scripts for their own save slots, only work with a physical keyboard.
- **Frameskip** is lightly tested. If fast-forward freezes, the frameskip invariant is the first suspect.
- Tested on **one device** only (Galaxy S25+, Android 16), mostly with Pokémon Platinum. The overlay orientation with
  the OpenGL and Compute renderers was not verified as thoroughly as with the Software renderer.
- `Pt_RNG_DeSmuMe.lua`'s F1–F10 save slots use files under the per-script folder, not DeSmuME's directory.

## File map

**New**

```
app/src/main/cpp/lua/            LuaEngine.{h,cpp}, LuaCanvas.{h,cpp}, LuaFont.h, LuaHost.h, LuaOverlayRenderer.{h,cpp}
app/src/main/cpp/lua51/          Lua 5.1.5 sources (as .cpp)
app/src/main/cpp/debug/          CrashHandler.{h,cpp}
app/src/main/java/me/magnum/melonds/ui/emulator/lua/LuaScriptController.kt
app/src/main/java/me/magnum/melonds/debug/CrashLogger.kt
app/src/main/java/me/magnum/melonds/ui/settings/fragments/LogPreferencesFragment.kt
app/src/main/res/xml/pref_log.xml, res/drawable/ic_log.xml, res/values*/strings_lua.xml
tools/controls/                  gen_controls.py, preview_controls.py, preview_layout.py
tools/lua/                       example scripts + host test harness for the Lua engine
```

**Modified**

```
app/CMakeLists.txt, app/build.gradle.kts
app/src/main/cpp/  MelonInstance.{h,cpp}, MelonDS.{h,cpp}, MelonDSAndroidJNI.cpp, Configuration.h, MelonDSAndroidConfiguration.cpp
melonDS-android-lib/src/  GPU.{h,cpp}, GPU3D.{h,cpp}
MelonEmulator.kt, MelonDSApplication.kt, EmulatorActivity.kt, EmulatorViewModel.kt, EmulatorUiEvent.kt,
RomPauseMenuOption.kt, EmulatorConfiguration.kt, SettingsRepository.kt, SharedPreferencesSettingsRepository.kt,
impl/layout/DefaultLayoutProvider.kt
res/xml/pref_general.xml, res/xml/pref_main.xml
res/drawable/  buttons, keypad, button_*, ic_touch_* (PNG → vector XML)
```

## Building

Requirements:

- JDK 21;
- Android SDK platform `android-37.0`;
- NDK `28.0.13004108`;
- CMake `3.22.1`.

```
./gradlew :app:assembleGitHubProdDebug          # all ABIs
./gradlew :app:assembleGitHubProdDebug -Pslim   # arm64 only, R8, ~17 MB
```

Output: `app/build/outputs/apk/gitHubProd/debug/`. Release builds need the original author's keystore and were never
built here.

Example scripts are in `tools/lua/`. `platinum-item-and-nature.lua` shows the wild Pokémon's nature and held item in
Pokémon Platinum, in all languages. `Pt_Autoplay.lua` plays Platinum by itself (wild encounters with auto-flee, or soft
resets for static and gift Pokémon) until it finds a Pokémon with the wanted species, held item, shininess, nature, IVs or
ability. It uses `joypad.set` and `stylus.set`.

## Credits, acknowledgements and legal

### Thanks

- **Arisotura and the melonDS team**, for melonDS, the emulator this whole thing runs on.
- **Rafael Caetano (rafaelvcaetano) and the melonDS Android contributors**, for the Android port this fork is built on.
  Every feature here builds on their frontend, renderers, layout system and settings.
- **The DeSmuME team**, whose Lua scripting API this fork imitates so that existing scripts run unchanged.
- **The Pokémon RNG community**, in particular the authors of `Pt_RNG_DeSmuMe.lua`, whose research (pointers, data
  layout, decryption) the example scripts are based on.
- **Lua.org / PUC-Rio** (Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes) for Lua, and
  **Mike Pall** for the LuaBitOp semantics reproduced in the `bit` library.
- **Google** for the Roboto typeface used in the button labels, and the authors of the X11 misc-fixed fonts.

### Licenses

- **This fork is licensed under the GNU GPL v3**, like melonDS and melonDS Android: see [LICENSE](LICENSE). The
  complete corresponding source is this repository.
- The modification notice required by GPLv3 section 5a (who changed what, and when) is in [NOTICE.md](NOTICE.md).
- **Third-party components added by this fork:**
  - Lua 5.1.5: MIT, see [third_party_licenses/Lua-5.1-MIT.txt](third_party_licenses/Lua-5.1-MIT.txt);
  - Roboto outlines: Apache 2.0, see [third_party_licenses/Apache-2.0.txt](third_party_licenses/Apache-2.0.txt);
  - X11 5×7 font: public domain.
  - Details are in [NOTICE.md](NOTICE.md).
- **Components inherited from melonDS Android** (melonDS-android-lib, oboe, enet, FAAD2...) keep their original
  licenses, unchanged.
- **DeSmuME, LuaBitOp and FCEUX:** only their APIs or behaviour were reimplemented. No code was copied from them.

### Disclaimer

- This is an **unofficial fork**. It is not affiliated with or endorsed by the melonDS team, the melonDS Android
  project, the DeSmuME team, Nintendo or The Pokémon Company.
- Please **do not report bugs of this fork to the upstream projects**. If a problem also happens with the official
  melonDS Android, report it there as usual.
- The debug build still shows the name "melonDS", but it uses a separate package (`me.magnum.melonds.dev`) so it never
  replaces the official app.
- No ROMs, BIOS, firmware or other copyrighted Nintendo content are included. Nintendo DS, Nintendo DSi and Pokémon
  are trademarks of their respective owners.
- Provided **as is, without any warranty**, as stated in the GPL.

### How this was made

This fork was developed by RicksCodes with the help of an AI coding assistant (Claude Code). The code was tested
only as described in [Known issues](#known-issues); please review it with that in mind before reusing any of it.
