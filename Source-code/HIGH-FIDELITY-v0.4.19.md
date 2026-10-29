# Tailtone v0.4.19 — High Fidelity Pitch

## What changed

- Tail page: **PITCH ENGINE** selector, **Legacy (original)** or **High Fidelity**.
- High Fidelity keeps the anti-aliased r8brain resampling stage, then uses
  phase-refined stereo-linked source cycles and shorter, phase-coherent cycle
  handovers instead of a continuous broad cycle morph. Cycle-phase adjustment
  uses a constrained high-harmonic similarity search in the preparation worker.
  Ambiguous matches are rejected rather than forcibly adjusted.
- The first 30 ms uses a smoothly varying, especially short cycle handover
  to reduce transient smearing. This is not a promise of transparent pitch
  shifting at large transpositions; listening tests matter.
- Existing Legacy renderer is preserved and can be selected for immediate A/B.
- New plugin instances default to High Fidelity. Loading older project states
  with no `pitchQuality` parameter restores Legacy, preserving their sound.
- The `pitchQuality` choice is appended after existing parameters, is host
  automatable and is saved in plugin project state.
- Worker-prepared D#1..D#3 MIDI note bank updates when pitch quality changes;
  any previously prepared active voice retains its snapshot until note-off.
  No phase searches or audio allocations were added in `processBlock`.
- Phase alignment (per-note), 80–150% stretch, fixed note-bank duration,
  untouched source at root with 100% stretch, independent punch/reverb,
  waveform dragging, EQ, distortion, transient shaper and UI scaling remain.

## Validation / limits

- Linux portable CMake suite: **18/18 passed** including the new High Fidelity
  pitch engine test and difficult kick pitching/stretch regression.
- New High Fidelity tests check output duration, stable low-bass pitch across
  24 pitch/stretch combinations, finite stereo output, all 25 prepared notes,
  original root identity, preserved default Legacy output, and high-frequency
  activity on the bundled `virus-kick-3.wav` fixture.
- C++17 syntax-only JUCE checks: **6/6 passed** (processor, editor, 4 tests).
- **Not verified here:** full Windows VS2026 VST3 build, 22 Windows CTest cases,
  Windows FL Studio playback, perceptual quality or performance for every kick.
- The High Fidelity output has not undergone a blind listening test: measured
  additional high-frequency activity may or may not sound better on an
  individual tail. Compare Legacy and High Fidelity with loudness matched.

Build on Windows: extract archive, run `BUILD-WINDOWS.cmd`, check all 22 tests,
copy `Tailtone v0.4.19.vst3` to `%CommonProgramFiles%\VST3`, rescan FL Studio.
