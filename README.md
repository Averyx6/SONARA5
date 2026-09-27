# SONARA v1.6

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v1.6 fixes support-lane role confusion and multi-instance CPU scaling.

Mainstream song generation now uses strict musical roles and register boundaries. CHORDS contains close triad/extension blocks only, one chord block per harmony event, rather than scattered melodic notes and octave doubles. PLUCK is a narrow-register chord-tone support pattern rather than a second melody. BASS is root-first with at most a predictable fifth pickup, SUB stays on the harmonic fundamental, and PAD uses slow upper harmony without random octave layers. These changes specifically target the wide, crowded piano-roll patterns that made CHORDS/PLUCK look and sound like unrelated instruments improvising at once.

The v1.5 songwriter engine remains intact: one cleaned main hook is carried through CHORUS -> DROP -> FINAL HOOK, isolated pitch spikes are repaired, support density is controlled, mainstream counter melody is opt-in, and prompt directions control structure/space/hook strength.

Multi-instance CPU behavior is also changed. Idle SONARA instances now return from the audio callback before synth/post-processing work when there is no preview, MIDI or active voice. The live engine is capped to four polyphonic voices and automatically enters a runtime eco mode when multiple SONARA instances are open, reducing unison and bypassing redundant per-instance chorus/reverb/delay processing. Low-CPU song engines also skip an unnecessary final saturation pass, and non-sine oscillator shapes no longer calculate a sine value they do not use.

MIDI and sound transfer are now explicit. Standard MIDI exports carry editable notes only; MIDI cannot embed SONARA SoundDNA or a VST patch. The UI labels those buttons as MIDI/notes-only and exposes LANE WAV • SONARA SOUND directly on the SONG tab for sound-preserving drag to the FL Playlist. For editable FL Piano Roll MIDI with SONARA sound, selecting a musical lane now loads that lane's SoundDNA into SONARA's live synth engine. Put the exported lane MIDI on that SONARA channel and it plays the selected custom sound. Per-lane custom SoundDNA and AUTO FIT stay synchronized with the live engine.

The FL-style piano roll, prompt suggestion buttons, per-instrument SoundDNA controls, automatic genre-aware sounds, drum build/drop logic, kick ducking, reference/resound tools, MIDI/stem/full-mix export, Randomize Everything and project persistence are preserved.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
