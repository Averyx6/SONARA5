# SONARA v0.8

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v0.8 focuses on musicality and production intelligence. The generator now keeps recognizable lead/support motifs instead of rerolling each bar, scores candidate songs for production coherence as well as novelty, understands production-structure requests such as festival/mainstage, early drop, long build, radio edit and cinematic, and uses energy-aware transitions plus kick-triggered low-cost ducking in preview.

SoundDNA now interprets a much wider semantic instrument vocabulary, including festival leads/plucks/chords, reese/growl/psy/acid basses, brass, strings, keys, organ, flute/reed, choir/formant, guitar/harp, mallets/kalimba, chiptune, screech, risers and impacts, with articulation words such as staccato, legato, slow/fast attack, tremolo and detuned.

The project preserves prompt-driven synthesis, controlled mutation/variations and locks, multi-section arrangement generation, generated lane sounds, procedural drums, live arrangement playback, piano-roll/timeline UI, Reference tools, mixer, transport, Randomize Everything, Surprise Me, and MIDI/export workflows.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
