# Third-party components in Kicktone

Kicktone is free software by Jei Audio (Jeipster), released under the GNU
General Public License v3 (LICENSE.txt). Whoever receives a Kicktone binary is
entitled to its complete source code under the same licence.

## JUCE 7.0.12
Raw Material Software Limited - https://juce.com
Bundled in ThirdParty/JUCE and used under the GNU GPL v3 option of the JUCE 7
licence (ThirdParty/JUCE/LICENSE.md). The juce_audio_basics, juce_audio_devices,
juce_core and juce_events modules are ISC licensed. The JUCE splash screen and
usage reporting are switched off, as the GPL option allows.

## VST3 SDK 3.7.8
Steinberg Media Technologies GmbH - bundled with JUCE in
ThirdParty/JUCE/modules/juce_audio_processors/format_types/VST3_SDK and used
under its GNU GPL v3 option (LICENSE.txt in that folder).
VST is a registered trademark of Steinberg Media Technologies GmbH. Kicktone
does not use the VST logo.

## r8brain-free-src
Aleksey Vaneev (Voxengo) - https://github.com/avaneev/r8brain-free-src
Copyright (c) 2013-2026 Aleksey Vaneev. MIT licence.
Unmodified core headers and FFT source are bundled in ThirdParty/r8brain with the
upstream LICENSE and SOURCE.txt provenance (master archive retrieved 2026-10-11).
"Sample rate converter designed by Aleksey Vaneev of Voxengo."

## Microsoft C++ runtime
Windows builds link the Microsoft Visual C++ runtime statically (since v0.5.24),
as permitted for the system libraries of the compiler by GPLv3 section 1.

## Test samples
The WAV files in Samples/ are test fixtures supplied by the author for the
regression tests. They are NOT covered by the GPL and are not part of the
public release unless their rights holders allow it. Without them the tests that
need a real kick cannot run.
