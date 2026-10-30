# Changelog

Kicktone was called Tailtone before 0.5.29. Since v0.5.0 every version is the
same plugin (plugin code `TaA2`).
A new version replaces the installed one, and projects from earlier versions
open unchanged.

## 0.5.33
- With Punch 2 on, the PUNCH waveform shows Punch 2 in purple where it plays:
  from ATTACK END on, moved by PUNCH 2 OFFSET. Punch 1 is drawn only up to
  ATTACK END (inside TRANSITION you see both), so the picture matches what you
  hear. It follows ATTACK END, TRANSITION, PUNCH 2 OFFSET and the Punch END trim.

## 0.5.32
- The Final Kick waveform now follows ATTACK GAIN and DECAY GAIN while you drag
  them. The same goes for the other Punch-only controls: Punch Attack / Decay
  (split, transition, EQs, Norm Attack / Norm Decay), Punch EQ, the transient
  shaper and the Punch distortion settings. Only the Punch is redrawn, so it keeps up with the
  mouse; the picture and Export WAV are exactly what a full redraw gives.

## 0.5.31
- PUNCH 2 OFFSET now moves the way you drag: right (+) moves Punch 2 later,
  left (-) moves it earlier. Projects saved with an earlier version are
  converted when they open, so they sound the same as before.

## 0.5.30
- Every EQ starts neutral in a new Kicktone, also when you have saved your own
  default settings: SAVE CURRENT on the Defaults page no longer stores EQ bands
  or EQ on/off switches, and EQ values saved by earlier versions are ignored.
  Your other saved defaults keep working. Projects keep their own EQ settings.

## 0.5.29
- Renamed from Tailtone to **Kicktone** (same plugin ID and settings format).
  The installer removes the old Tailtone.vst3 so the DAW lists the plugin only
  once. Your theme, custom defaults and Background rendering setting are copied
  over the first time.

## 0.5.28
- The Final Kick waveform follows the mouse at once while you drag the Punch or
  Tail volume.
- Public source release: the tests that need the private sample recordings are
  reported as skipped when the recordings are missing.

## 0.5.27
- Every tooltip rewritten in plain language.

## 0.5.26
- Extreme v5 prepares notes about twice as fast, with identical sound.

## 0.5.25
- GLIDE always uses the stretched slide.

## 0.5.24
- Windows installer. Runs without a separate Visual C++ runtime. The window
  shrinks to fit smaller screens.

## 0.5.23
- Experimental stretched glide: the tail stays stretched while it slides to
  the new note.

## 0.5.22
- UI size shown relative to the default (the default size reads 100 %).
- FINE PHASE SHIFT hidden.

## 0.5.21
- Separate normalize for the punch's attack and decay.

## 0.5.20
- Tail EQ and Punch EQ can each sit before or after the distortion.

## 0.5.19
- Progress bar with time left for the 37 tail keys.

## 0.5.18
- "Background rendering" setting (Eco / Balanced / Fast) on the About page.

## 0.5.17
- Fairer sharing of preparation between several Tailtone instances; exports
  never wait for background work.
- Prepared notes stay valid across updates that don't change the sound.

## 0.5.16
- One shared preparation limit for every Tailtone on the computer. Unplayed
  keys wait a few seconds after a change.

## 0.5.15
- The keys you play are prepared first. Prepared notes are saved in the
  project, so it reopens almost instantly.

## 0.5.14
- Contact e-mail in the beta notice.

## 0.5.13
- Experimental Reverb panel can be folded. Attack Layer END frequency.
  AUTO-SPLIT removed.

## 0.5.12
- Background preparation uses one core by default.

## 0.5.11
- Build fix for a crash caused by stale compiled files.

## 0.5.10
- Punch Harmony (third or fifth above the played note).
- Glide click fix.
- Lower CPU use during playback (identical sound) and a preparation CPU setting.

## 0.5.9
- Double-click near the left/right edge of an EQ adds a shelf.

## 0.5.8
- Arrow between PUNCH FOLLOWS TAIL PITCH and ONLY PUNCH 2 FOLLOWS.

## 0.5.7
- CRUNCH LOST ONLY: the crunch layer fills only the brightness lost on lower
  notes.

## 0.5.6
- Much faster incremental Windows builds.

## 0.5.5
- Shows the note of the punch decay; USE DECAY NOTE tunes the punch to it.

## 0.5.4
- ONLY PUNCH 2 FOLLOWS: only the Punch 2 decay follows the tail pitch.

## 0.5.3
- Your own default settings for new instances (About page).

## 0.5.1
- Linear-phase MONO BASS. Tail wet linear phase and mono bass switch live
  without a restart.
- Remembers the open pages and zoom levels in the project.
- Pitch Drop on Extreme v5. Attack Layer up to 50 ms.

## 0.5.0
- Permanent plugin identity: new versions replace the old one and open its
  projects.
- MONO BASS (90-300 Hz, with a mix).

## 0.4.x (development history, summarised)
- **0.4.69:** Punch 2 drag and drop; "Turn off band" on the EQ right-click menu.
- **0.4.68:** Extreme v5 tail engine.
- **0.4.67:** Punch follows tail pitch; Punch 2 tune.
- **0.4.65-0.4.66:** spectral join view, mouse-wheel zoom, parallel note
  rendering, LOCK SPLIT.
- **0.4.62-0.4.64:** Export WAV / drag out, colour themes, Attack Layer,
  Highest Fidelity punch pitch, new envelope / pitch drop column.
- **0.4.61:** About page; the older pitch engines were replaced by the v4
  engines.
- **0.4.58-0.4.60:** Pitch Drop, reverb engine V2, -/+ step buttons, output
  limiter.
- **0.4.48-0.4.57:** Kick Character (Highest Fidelity v4) engine, low punch
  lane, glide, Punch 2 layer, crunch layer, Root Render reverb matching, Tail
  END cut.
- **0.4.41-0.4.47:** Adaptive Stretch, linear-phase wet EQ, Punch Attack/Decay
  EQ, whole-kick ADSR, manual tail root fallback.
- **0.4.32-0.4.39:** experimental generated reverb with separate punch and tail
  networks, sub ducking and Root Render.
- **0.4.5-0.4.31:** recorded reverb lane, distortion buses, 8-band EQs,
  scalable UI, phase alignment at the join, punch pitch modes, join gain and
  silence trimming, final waveform output trims.
