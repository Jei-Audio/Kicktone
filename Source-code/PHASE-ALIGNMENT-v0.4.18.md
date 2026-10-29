# Tailtone v0.4.18 — phase alignment

New optional controls are on the **TAIL** settings page: **AUTO MATCH**,
**INVERT TAIL**, and **FINE PHASE SHIFT** (-3 to +3 ms, 0.01 ms resolution).
The overlap-only **CORRELATION** label reports a *pre-EQ/pre-distortion*
normalized estimate for the displayed root note (not a live post-effects meter).
Higher/positive values generally indicate more reinforcement *during the overlap*,
but do not guarantee phase alignment throughout the full kick.

AUTO MATCH is OFF by default to preserve older sessions and identical dry playback.
The sample-preparation worker analyzes individual pitched MIDI note-bank outputs
and searches +/-3.0 ms in 0.1 ms increments for better correlation. Scans run
outside the real-time audio thread. If matching is ambiguous, the join is too
short, or audio is silent, the automatic correction remains zero. In any case,
existing JOIN SHIFT and CROSSFADE controls remain available. Automatic offsets
are added to FINE PHASE SHIFT; INVERT TAIL polarity applies only to the pitched
tail (never the punch or recorded reverb).

Because fine phase shifts adjust *source playback alignment within the fixed
note envelope*, the start/end markers do not move and note duration remains
unchanged. Extreme offsets may shorten the leading/trailing tail audio by up to
3 ms. Distortion, transient processing, and EQ can further change phase; recheck
by listening and in a DAW correlation/oscilloscope meter after processing.

Audio parameter IDs are appended; older saved projects default to phase OFF,
fine = 0 ms, polarity normal. No expensive scanning is done in `processBlock`.
Use `Build-Windows.cmd` with Visual Studio 2026 on Windows and confirm all tests
before installation; a Linux portable test run does not verify FL Studio.
