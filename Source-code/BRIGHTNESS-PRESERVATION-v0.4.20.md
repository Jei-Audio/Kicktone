# Tailtone v0.4.20 — High Fidelity brightness preservation

## Implementation

The existing High Fidelity cycle engine remains the source of pitched tail
waveforms. This release adds **selective spectral-envelope compensation**
after note-bank resynthesis, prior to playback and effects. The original
unpitched tail is the reference. A 2048-point FFT analyzes steady-state
frequencies in stereo and compares the pitched tail's body-normalized
3–5 kHz, 5–9 kHz and 9–14 kHz spectral energy against its original.

- Compensate **only** bands with measured excess >0.8 dB. Cap each correction
  at **3 dB**; do not boost missing harmonics or change the kick below 2.5 kHz.
- Multiplicative, real-valued FFT gains preserve spectral complex phase in
  each analyzed frame. One curve is shared across channels; no stereo
  widening or independent left/right pitch correction is introduced.
- No blanket high-cut, no low-pass slope, no dynamic treble saturation, and
  **no smoothing effect**. Above 15 kHz the response returns to unity. The
  75%-overlap Hann-window inverse FFT reconstructs only corrected bands.
- Protect the onset: keep the first 25 ms of the *tail* exactly intact and
  fade correction into the body over the following 25 ms; the punch itself is
  completely untouched.
- Applies only to **High Fidelity** pitched notes. Legacy engine and the
  exact 0-semitone/100%-stretch source pass through without any processing.
- Operates on the **existing background MIDI bank preparation worker**;
  no FFT, allocations, locks or CPU-intensive work occur in playback callback.
- Pitch and stretch settings, the per-note phase-map, EQ, reverb, distortion,
  waveforms, UI controls and saved host parameter IDs are unchanged.

## Limitations and verification

Spectral envelope comparison is a conservative, per-note *global* correction:
if an individual kick does not show excess against its root reference, it
remains essentially unchanged. It cannot undo all resampling, aliasing or
cycle-discontinuity artifacts, and it does not guarantee a subjectively
better tone. The change should be judged in a level-matched host listening
comparison on representative source kicks.

Portable Debug regression suite: 19/19, including new deterministic tests for
selective frequency correction, untouched attack/bass/root, retained 17 kHz,
stereo ratio and Windows min/max macros. Full Windows VST3/FL Studio tests
must be run locally using the provided build script.
