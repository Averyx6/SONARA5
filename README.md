# SONARA v2.0

SONARA is a Windows x64 JUCE VST3 + Standalone AI music-production instrument for FL Studio.

v2.0 is the melody clarity, drop identity, preview fidelity and FL workflow release.

## What v2.0 changes

- Mainstream EDM melodies are simpler and easier to read: tighter register, lower note density, fewer random passing tones, no random verse octave-down behavior, and a hard clarity gate that rejects note soup.
- Song sections have clearer jobs. CHORUS states the hook, DROP uses a tighter driving variation of that identity, BREAKDOWN releases energy, and FINAL HOOK restores/develops the fuller phrase.
- A full-beat pre-drop breathing gap and beat-one drop anchor make the drop boundary obvious.
- Drop drums are louder than build/breakdown drums without changing the core SoundDNA.
- Preview output is louder while remaining soft-limited and finite.
- Full-mix WAV export follows the same section balance/master gain decisions as preview.
- After generation, LEAD SoundDNA is loaded into the live SONARA instrument so editable FL Piano Roll MIDI on a SONARA channel uses the generated lead patch.
- Project save/load preserves the selected lane and restores the matching live SoundDNA.
- New PLAY DROP transport jumps directly to the first DROP.
- New LEAD MIDI drag always exports only the main LEAD notes, never BASS/SUB/support notes.
- New LEAD WAV drag always renders the LEAD SoundDNA as audio.
- Selected-lane MIDI/WAV, multitrack MIDI, exact preview WAV, rendered stems, reference RESOUND/REBUILD, SoundDNA mutation, lane mixing and Cyanoryx interchange remain available.

## Important MIDI behavior

Standard MIDI stores notes, timing, velocity and metadata; it does not contain SONARA's custom synth audio. Use LEAD MIDI / selected-lane MIDI on a SONARA channel when you want editable notes with the matching SoundDNA loaded. Use LEAD WAV, selected-lane WAV, full-mix WAV or stems when you need the rendered SONARA sound preserved exactly.

## v2 quality rules

SONARA rejects candidates that recycle the previous song, create isolated melody spikes, put mainstream lead notes in the wrong low register, overload hook bars, lose section contrast, miss the pre-drop gap, or fail to produce a clear drop arrival. Novelty is treated as a constraint, not the goal: a strong musical song that is genuinely different is preferred over random complexity.

See `SONARA_V2_MASTER_PROMPT.md` for the complete release specification and regression requirements.

## Windows build

Run `BUILD_SONARA.bat` from a Visual Studio 2022 developer environment, or use the GitHub Actions workflow. A release is considered verified only after Windows VST3 + Standalone compilation, every regression test, VST3 packaging and artifact upload succeed. The packaged artifact is `DIST/SONARA-Windows-x64-VST3.zip`.
