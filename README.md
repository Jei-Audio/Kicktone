# Tailtone

**A kick instrument for hardstyle, hardcore and other distorted kicks.**
Load a punch and a tail. Tailtone pitches the tail across the keyboard, lines
it up with the punch at every note and gives you one finished kick that you can
play, shape and export.

By **Jei Audio** (Jeipster). Free software under the GNU GPL v3.

> **Beta.** Tailtone is still in development. Please report crashes or
> anything that sounds wrong to **Jeiaudio.project@gmail.com** or open an
> issue on GitHub.

## Download

Get the latest version from the **Releases** page:

- `Tailtone-<version>-Windows-Setup.exe`: installer (VST3 plugin and an optional standalone app)
- `Tailtone-<version>-Windows-VST3.zip`: the plugin on its own, for a manual install

**Requirements:** Windows 10 or 11 (64-bit) and a VST3 host (FL Studio, Ableton
Live, Reaper, Bitwig, Cubase, Studio One, ...). There are no macOS or Linux
downloads yet.

The installer is not code-signed yet, so Windows may show "Windows protected
your PC". Click **More info**, then **Run anyway**.

New versions replace the installed one. Projects saved with older versions open
unchanged.

## Features

**Pitch and timing**
- 37 pitched tail keys (D#2 to D#5). Tail engines: Highest Fidelity v4
  (default), Adaptive Stretch v4 and Extreme v5. Stretch from 80 % to 150 %.
- Detects the tail's root note automatically; you can set it by hand.
- The punch can stay put, follow the tail's pitch, or play a third or a fifth
  above it (Punch Harmony). Punch pitch modes: Stretch Aligned, Resample and
  Highest Fidelity.
- Each note's punch-to-tail join is phase-aligned, with JOIN SHIFT and
  CROSSFADE controls.
- Pitch Drop, Attack Layer, glide between held notes (the tail stays
  stretched while it slides) and a second punch layer for the decay (Punch 2).

**Sound shaping**
- Separate distortion for the punch and for the tail + reverb (4x oversampled),
  transient shaper and 8-band EQs. Each EQ can sit before or after the
  distortion.
- Separate EQ and gain for the punch's attack and decay.
- Crunch layer that brings back the brightness lost on lower notes.
- Recorded reverb lane and an experimental generated reverb (separate punch
  and tail sends, sub ducking, Root Render).
- ADSR on the whole kick, linear-phase MONO BASS and an output limiter.

**Workflow**
- Final Kick waveform with output trims, a spectral view of the join, and
  EXPORT WAV / DRAG to take the finished kick out.
- Scalable UI, five colour themes and your own default settings for new
  instances.
- Background note preparation with a progress bar; the keys you play are
  prepared first.

## Building from source

Everything Tailtone needs is in this repository (JUCE 7.0.12 and r8brain are
bundled in `Source-code/ThirdParty`).

On Windows, some bundled JUCE paths are long. Turn on long paths in git and
clone to a short folder, otherwise checkout or the JUCE tool build can fail:

```bash
git config --global core.longpaths true
git clone <repository-url> C:\src\Tailtone
```

### Windows (Visual Studio 2026)

Requirements: Visual Studio 2026 with "Desktop development with C++" and
CMake 4.2 or newer.

```bat
BUILD-WINDOWS.cmd          :: quick: VST3 + standalone, only what changed, then installs the VST3
BUILD-WINDOWS.cmd full     :: also builds and runs all tests
BUILD-WINDOWS.cmd clean    :: wipe the build folder first, then full
```

The script mirrors the source to `%LOCALAPPDATA%\JeiAudio\Tailtone-src` and
builds in `%LOCALAPPDATA%\JeiAudio\Tailtone`. Installing to
`C:\Program Files\Common Files\VST3` needs a prompt started as administrator.

To make the installer, install [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php)
and run `BUILD-INSTALLER-WINDOWS.cmd`. It runs a clean build and every test, then writes
the installer and the VST3 zip to `Release\`.

### Plain CMake

```bash
cmake -S Source-code -B build
cmake --build build --config Release --target Tailtone_VST3 Tailtone_Standalone
ctest --test-dir build -C Release --output-on-failure
```

Use `-DPT_CORE_ONLY=ON` to build only the portable DSP tests (no JUCE).

### Tests and the sample files

The test suite has 62 tests. 17 of them check the pitch engines on real kick
recordings in `Samples/`. These recordings are **not** part of the public
source (they are not GPL), so in a fresh checkout those 17 tests are reported
as **Skipped**. All other tests run normally.

The tests' limits are tuned to those exact recordings, so replacing them with
other WAV files of the same names can make the tests fail. Point
`-DTAILTONE_SAMPLES_DIR=<folder>` at a folder with the original files if you
have them, and re-run CMake after adding them.

## Licence

Tailtone is free software: you can redistribute it and/or modify it under the
terms of the **GNU General Public License version 3** ([LICENSE](LICENSE)).
It comes with no warranty.

Third-party components and their licences are listed in
[Source-code/THIRD-PARTY.md](Source-code/THIRD-PARTY.md): JUCE (GPLv3 option),
the Steinberg VST3 SDK (GPLv3 option) and r8brain-free-src by Aleksey Vaneev
(MIT). VST is a registered trademark of Steinberg Media Technologies GmbH.

Created with the help of different AI models. Created and distributed by Jei
Audio aka Jeipster.

See [CHANGELOG.md](CHANGELOG.md) for the version history.
