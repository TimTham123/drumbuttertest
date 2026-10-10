# DrumSpread

DrumSpread is a character-focused drum processor (VST3 + Standalone). Put it on a
drum bus, loop, drum machine or single hit, turn a few knobs, and get punch,
weight, grit, space and glue. It is an original design: the name, module names,
UI, presets, noise layers and reverbs were all made for this project, and no
third-party samples, IRs or presets are used.

```
In -> LAYER -> [ WARP | ROOM | PRESS | HEAT  - drag to reorder ] -> SMASH -> MASTER -> Out
```

| Module | What it does |
|---|---|
| **LAYER** (fixed first) | Kick-triggered sub oscillator (pitch drop, phase-aligned, optional growl) and snare/hat-triggered noise layer (white/pink/crackle/band), with trigger LEDs |
| **WARP** | Independent pitch and formant shift (phase vocoder with transient phase reset; HQ or low-latency Lo-Fi) |
| **ROOM** | Room, Plate (Dattorro tank), Spring, Gated and Vintage ambience; mono in gives stereo out |
| **PRESS** | Six compressor characters: FET (with All-Buttons), Opto, VCA, Diode, Vari-Mu, Glue |
| **HEAT** | Six saturators: Tape, Tube, Transformer, Diode, Fuzz, Crush |
| **SMASH** (fixed last) | Waveshaper (5 curves, drive, bias) into a soft/hard clipper |
| **MASTER** | Transient shaper, 3-band EQ, HPF/LPF, Air, Output, Dry/Wet, Master Clip, in/out meters, spectrum/waveform view |

Every module has the same shared strip:
- **Lo/Hi** focus filters. Only that band is processed; the rest bypasses the
  module.
- **Target** (Transient <-> Sustain).
- **Mix**.
- A click-free **on/off** switch.

**INTENSITY** (the spec's "Magnitude" control) scales the whole plugin from
fully neutral (0%) to beyond the as-set settings (200%). DESIGN.md §5.10 gives
the exact mapping.

43 factory presets ship in 7 categories (Polish, Punch, Glue, Space, Lo-Fi,
Heavy, Wild). The plugin starts on **"Golden Spread"**.

Detailed documents:
- [DESIGN.md](DESIGN.md): signal flow, algorithms, the full parameter table,
  decisions, references and unverified items.
- [TEST_REPORT.md](TEST_REPORT.md): measured results.
- [`renders/`](renders): before/after demo WAVs.

---

## Build status: what was compiled and tested, and where

The development environment for this delivery was a **Linux container** (GCC 13,
CMake 3.28) with no Windows toolchain. Windows binaries therefore come from CI.

| Item | Linux (dev container) | Windows x64 / MSVC (GitHub Actions, `windows-latest`) |
|---|---|---|
| DSP core + offline harness (`dscore_tests`) | built and run: all checks pass (see TEST_REPORT.md) | built and run in CI: **117/117 checks pass** (the CPU check is informational on shared runners) |
| Plugin VST3 + Standalone | built (`.so` VST3 bundle, ELF standalone) | **built in CI**; artefact **DrumSpread-Windows-x64** contains `DrumSpread.vst3` and `DrumSpread.exe` |
| Plugin-level tests (`plugin_tests`: state, presets, A/B, layouts, latency, RT allocations, UI snapshots) | built and run under Xvfb: all pass | run in CI: **22/22 pass** |
| Host validation with pluginval (strictness 10: scan, editor, processing, state, automation, thread safety, fuzzing) | **SUCCESS** for the Linux VST3 (log in `docs/`) | **SUCCESS** for the Windows VST3 in CI |
| Steinberg VST3 SDK validator, extended mode | **537/537 passed** for the Linux VST3 (log in `docs/`) | not run |
| Loading in a commercial DAW | **not tested** (none available here) | **not tested** |

Windows evidence: run [38042364854](https://github.com/TimTham123/drumbuttertest/actions/runs/38042364854)
(commit `5ff6796`, the same code as the current branch head; later commits only
change documentation). The Windows binaries are under that run's
**Artifacts**. You need to be signed in to GitHub to download them, and GitHub
keeps artefacts for 90 days by default.

The code is plain C++17 with no compiler-specific extensions, and builds
warning-free under GCC with `-Wall -Wextra -Wshadow` and JUCE's recommended
warning set. The MSVC build was compiled and tested only in CI, never locally.

---

## Building

Requirements:
- CMake >= 3.22.
- A C++17 compiler.
- Git, because CMake fetches **JUCE 8.0.15** via `FetchContent`; the tag is
  pinned in `CMakeLists.txt`.

### Windows (Visual Studio 2022 or newer, x64)

```bat
git clone <this repo> DrumSpread
cd DrumSpread
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

Outputs:
- `build\DrumSpread_artefacts\Release\VST3\DrumSpread.vst3`: copy this folder to
  `C:\Program Files\Common Files\VST3\`.
- `build\DrumSpread_artefacts\Release\Standalone\DrumSpread.exe`.
- `build\Release\dscore_tests.exe` and
  `build\plugin_tests_artefacts\Release\plugin_tests.exe`.

The MSVC runtime is linked statically, so no VC++ redistributable is needed.

### Linux

```bash
sudo apt install ninja-build libasound2-dev libfreetype-dev libfontconfig1-dev \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### macOS

`cmake -S . -B build -G Xcode` then `cmake --build build --config Release`. This
path is untested.

### Options

| CMake option | Default | Meaning |
|---|---|---|
| `DS_BUILD_PLUGIN` | ON | Build the JUCE plugin targets. Set OFF to build only the JUCE-free DSP core and its harness; this needs no JUCE download and builds in seconds. |
| `DS_BUILD_TESTS` | ON | Build `dscore_tests` (and `plugin_tests` when the plugin is on). |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | — | Use a local JUCE checkout instead of downloading it. |

---

## Running the verification

```bash
# DSP harness: ~70 s; writes TEST_REPORT.md and demo WAVs
build/dscore_tests test_output test_output/TEST_REPORT.md
#   --no-renders     skip WAV output
#   --no-perf-gate   report CPU without pass/fail (shared CI machines)
#   --perf-only      CPU benchmark only

# plugin-level tests (needs a display; on headless Linux use xvfb-run)
build/plugin_tests_artefacts/Release/plugin_tests test_output
```

---

## Using it

- **Reorder:** drag a reorderable panel by its header grip (⠿), or use its
  `<` `>` buttons. The order is a host-automatable parameter (*Chain Order*) and
  is saved with the session. Switching is a 40 ms crossfade, with no gap and no
  click.
- **Presets:**
  - `<` / `>` step through the presets; the centre button opens the
    categorised browser.
  - **SAVE** writes a user preset (XML, `.dspreset`). User presets live in
    `%APPDATA%\DrumSpread\Presets` on Windows, `~/.config/DrumSpread/Presets` on
    Linux, and `~/Library/DrumSpread/Presets` on macOS.
- **A/B:** two full-state slots. **COPY** duplicates the active slot into the
  other one.
- **Oversampling:** 1×/2×/4× (default 2×). This applies to PRESS, HEAT, SMASH
  and the LAYER sub/growl. Changing it changes the reported latency, with a
  short fade.
- **Latency** (48 kHz, 2×): 1098 samples (22.9 ms) with WARP in HQ mode, 330
  samples (6.9 ms) in Lo-Fi mode. The latency is the same whether modules are
  bypassed or not, and the host bypass is latency-compensated.

---

## Source layout

```
CMakeLists.txt              JUCE 8.0.15 via FetchContent; dscore, plugin, tests
DESIGN.md                   design document (step 0) + decisions
TEST_REPORT.md              harness + plugin test results
renders/                    before/after WAVs of the synthesized drum loop
Source/dsp/                 JUCE-free DSP core (dscore)
  Params.*                  single parameter table (plugin, presets, tests)
  ModuleSlot.h              focus split, target, mix/bypass, latency compensation
  Layer/Warp/Room/Press/Heat/SmashModule.*, MasterSection.*
  Engine.*                  chain, reorder crossfade, config fades, telemetry
  Oversampler.*, FFT.*, Filters.h, Detectors.h, DelayLine.h, DspCommon.h
  FactoryPresets.*          43 authored presets
Source/plugin/              JUCE AudioProcessor (APVTS) + PresetManager (XML, A/B)
Source/ui/                  editor, module panels, look-and-feel, meters, scope
tests/                      dscore_tests (offline DSP harness) + plugin_tests (JUCE)
.github/workflows/build.yml Windows/MSVC + Linux CI: build, run tests, upload binaries
```

## License

The project code is original. JUCE is used under its own license (the AGPLv3 /
JUCE commercial dual license). Distributing a closed-source binary requires a
suitable JUCE license.
