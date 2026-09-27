# SONARA v1.1

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v1.1 is the producer-quality musicality pass. The default EDM architecture now follows a clear INTRO -> VERSE -> BUILD -> CHORUS -> DROP -> BREAKDOWN flow, with the chorus introducing the main hook and the drop developing that same identity. Lead generation uses strict section-specific note budgets, stronger rests, motif continuity, and reduced support-layer density so arrangements read like phrases instead of note clouds.

The song candidate selector now scores production space as well as novelty, melody, harmony, prompt match, section contrast, and hook recall. It penalizes arrangements where too many musical lanes fire on the same fine-grid positions.

Every generated lane still receives an automatic SoundDNA design that fits its role and genre. v1.1 also adds per-instrument overrides: select any lane, describe only that instrument in FX & MIX, and apply a custom SoundDNA without changing its MIDI or other lanes. AUTO FIT restores SONARA's role-aware automatic sound for only the selected lane.

Preview synthesis includes a new CPU optimization pass: expensive unison patches receive complexity-aware caps, static filters avoid unnecessary coefficient updates, and pitch/transient envelopes plus bit-crush/downsample values are cached instead of recomputed in the per-sample hot loop.

The project preserves Harmony DNA, different-song generation, festival drum DSP, kick ducking, semantic SoundDNA families, reference/resound tools, lane mixer, timeline/piano-roll UI, editable MIDI export, stem/full-mix audio export, Randomize Everything, Surprise Me, and project/preset persistence.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
