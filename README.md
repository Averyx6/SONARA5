# SONARA v1.5

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-creation instrument for FL Studio.

v1.5 is the songwriter-producer pass. It focuses on making generated tracks feel like songs rather than experiments.

Mainstream EDM, progressive house, melodic house, pop, trance, festival and future-rave prompts now create one cleaned four-bar main hook and deliberately reuse that musical identity through CHORUS -> DROP -> FINAL HOOK. The production changes around the hook; the lead does not randomly reinvent itself every section. A section-aware melody cleanup removes isolated high/low pitch spikes, octave outliers and tiny accidental grace notes while preserving key and phrase direction.

Support parts are cleaner too. Mainstream arrangements cap chord repetition, bass movement, SUB movement, pluck density and random passing-note/octave jumps. Counter melody is OFF by default in mainstream song modes unless the user explicitly requests counter melody, secondary lead, call-and-response or an answer melody.

SONARA now understands direct songwriter instructions such as strong hook, clear hook, simple melody, clean melody, no random notes, short intro, big chorus, long drop, short breakdown, radio structure, drum build, more space, less busy, powerful drop and no counter melody. The SONG tab exposes these as one-click prompt-direction buttons.

Candidate selection is song-first: pitch-spike penalties, chorus/drop pitch+rhythm recall, section hierarchy, arrangement space, melody quality, harmony, prompt match and production contrast outweigh raw novelty. Novelty remains a constraint so repeated generations are still different without rewarding strange structure just because it is different.

Preview mixing is section-aware. Intro/verse/build/chorus/drop/breakdown/final-hook use different lead, bass, SUB, chord, pluck and pad balances so the energy arc is audible. The main hook is foregrounded while pads/plucks/counter are pushed back.

The piano-roll UI has been upgraded toward an FL-style production view with adaptive pitch range, semitone/key shading, stronger bar/beat grid, clearer note blocks, pitch labels when space allows and a visible playhead. v1.1 per-instrument SoundDNA editing and AUTO FIT remain available so every lane can be individually redesigned while automatic role-aware sound selection still works by default.

CPU optimizations, strong drum DSP, chorus-to-drop drum tension, kick ducking, editable MIDI, stem/full-mix audio export, reference/resound tools, project persistence, SoundDNA families, Randomize Everything and Surprise Me are preserved.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after the Windows VST3 + Standalone targets compile, all regression/acceptance tests pass, packaging succeeds, and the workflow publishes `DIST/SONARA-Windows-x64-VST3.zip`.
