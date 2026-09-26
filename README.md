# SONARA

SONARA is a Windows x64 JUCE VST3 AI music-creation instrument for FL Studio.

Current source includes prompt-driven SoundDNA synthesis, controlled mutation/variations and locks, a 72-bar multi-section arrangement generator with generated lane sounds, procedural drums, live arrangement playback, piano-roll/timeline UI, and standard MIDI drag/export for both full arrangements and selected lanes.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A successful release produces `DIST/SONARA-Windows-x64-VST3.zip` only after regression tests pass.