# SONARA v1.7

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v1.7 is the song-clarity pass. It focuses on making all pitched instruments behave like one arrangement instead of separate generators.

For mainstream EDM, progressive house, melodic house, pop, trance, festival and future-rave prompts, CHORDS is now one sustained triad per bar rather than a stream of short retriggers. PLUCK is limited to two predictable chord-tone onsets per bar. SUB uses one root onset per bar and BASS uses at most one or two root/fifth support notes. PAD is slow upper harmony only. These support lanes no longer create extra melodic information.

A shared register map is applied after the lead is generated. LEAD establishes the musical center per bar; CHORDS, PLUCK, PAD and optional COUNTER are octave-folded around that same center while preserving pitch class and chord shape. BASS and SUB remain intentionally lower. This fixes the visual/audio problem where each instrument lived in unrelated octaves even when the notes were technically in key.

The v1.5/v1.6 songwriter behavior remains: one cleaned four-bar hook is reused through CHORUS -> DROP -> FINAL HOOK, isolated pitch spikes are repaired, mainstream counter melody is opt-in, prompt directions control structure and space, and support-note density is constrained.

Preview and full-mix WAV export now use the same production behavior: matching lane gains, section-aware energy, kick-driven ducking, FX sends, reverb level, drum balance and master shaping. The music level is raised, the lead is more forward, and the drum kit is mixed lower so it no longer masks the melody. Full-mix export is regression-tested for healthy RMS/peak.

v1.6 multi-instance CPU optimizations are preserved: idle instances sleep, live polyphony is bounded, runtime eco mode engages with multiple SONARA instances, low-CPU engines skip unnecessary post-processing, and the five-instance regression remains in the test suite.

MIDI exports are still notes-only because standard MIDI cannot contain a VST patch. Selecting a musical lane loads its SoundDNA into SONARA's live synth engine so editable FL Piano Roll MIDI can play the selected SONARA sound when placed on a SONARA channel. LANE WAV • SONARA SOUND remains the sound-preserving drag path.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
