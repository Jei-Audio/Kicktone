# Tailtone v0.4.21 — selective upward crunch preservation

## Motivation

Users reported that upward transposition can lose distinctive hardstyle-tail
crunch. The v0.4.20 High Fidelity option continues to use pitch-synchronous
cycle reconstruction and compensates excessive measured 3–14 kHz energy.
Some intentional crunch lives in that region; attenuation and overlap blends
can therefore reduce the texture even when the algorithm has no clicks.

## Implementation

- New append-only automatable `pitchCrunch` bool (default false). Existing
  projects retain exact v0.4.20 render when false.
- In `pt::cycle::render`, only High Fidelity notes with `semitones > 0` and
  `pitchCrunch = true` take the experimental path.
- After stereo r8brain resampling and existing shared phase refinement,
  normalized stereo inter-cycle correlation and mismatch are computed using
  48 phase samples per cycle. Matches narrow the cycle handover smoothstep
  from ~34% toward ~11.5% of a cycle. Mismatched/noisy/short/invalid cycles
  retain the safe ~34% width. Both stereo channels use the same widths.
- When experimental path is active the optional 3–14 kHz excess-brightness
  correction is **bypassed**. No treble boosting, extra low-pass, artificial
  harmonics, fake distortion, time-domain smoothing or new wet/dry comb signal.
- Pitch remains tied to the same cycle phase map and r8brain bandlimiting.
  Output frame count remains exactly `round(source samples * stretch)`.
- All work is offline on the existing sample-preparation worker; the real-time
  callback is not modified. The note bank is rebuilt when `pitchCrunch` changes
  and checked for stale state before publication.

## What this does not promise

It cannot reconstruct ultrasonic harmonics discarded by safe anti-aliasing
nor guarantee restored crunchy timbre on every source. At higher positive
transpositions, harmonic losses from bandlimiting may dominate. The toggle is
experimental, default-off and reversible.

## Validation

- 20/20 portable DSP regressions include a new real-kick `UpwardCrunchPreservation`
  test: OFF sample-identical to v0.4.20 High Fidelity, root/downwards/Legacy
  unchanged, output size/validity, and conservative click/texture guards.
- JUCE processor/editor/related integration source checks pass (C++17).
- Full JUCE Linux configure cannot complete without Xrandr development headers;
  Windows Visual Studio 2026 VST3 build, 24 CTest cases, FL Studio listening
  and GUI tests must still be performed locally.
