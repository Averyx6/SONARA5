# SONARA v1.3

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v1.3 is the songwriting-engine pass. It is designed to stop generated tracks from feeling like one long intro or an experiment made of technically-valid random notes.

The main song flow stays INTRO -> VERSE -> BUILD -> CHORUS -> DROP -> BREAKDOWN. Section roles are now deliberately different: the intro only teases the melody, verse leaves breathing bars, build introduces melody only in its second half, chorus presents the full memorable hook, drop develops the same hook with stronger production, and breakdown strips the melody back to sparse replies.

Mainstream EDM/progressive/pop/trance prompts use a curated group of song-like melody archetypes instead of the full experimental grammar pool. Chorus/drop/final hook use stable four-bar hook phrases, preserve the motif contour over changing chords, avoid random micro-timing, and use tighter note budgets. Counter melody is held back until drop/final-hook answer bars so it does not compete with the main hook.

The chorus-to-drop transition now has a dedicated escalating snare/clap roll, tom fill, kick removal in the final beat, and a half-beat breathing gap before the drop crash/impact. Build-to-chorus uses a smaller lift so the real drop still feels like the main payoff.

Candidate selection now prioritizes songwriting quality and section shape over novelty. Verse/build/breakdown space, chorus/drop arrival, hook recall, arrangement collisions, harmony quality, prompt fit and novelty are scored together, with novelty used as a constraint rather than the main creative goal.

Preview mixing is lead-forward so the main melody sits above pluck/pad/counter layers. v1.1 per-instrument SoundDNA editing, AUTO FIT, CPU optimizations, automatic genre-aware sound selection, strong drum DSP, kick ducking, editable MIDI, stems/full mix export, reference/resound and project persistence are preserved.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
