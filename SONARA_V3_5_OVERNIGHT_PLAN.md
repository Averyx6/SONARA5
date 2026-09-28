# SONARA v2.0 -> v3.5 Overnight Upgrade Plan

## Source of truth
- Repository: Averyx6/SONARA5
- Verified base: v2.0-release @ caf37c5023fdb8630cee49bf22bf3e48d56b53fc
- Development branch: v3.5-development
- Final target: 3.5.0
- Never revert to older branches to make tests pass.
- Never advance a version by changing labels only.
- Every important bug fix should gain a regression test.

## Strict sequence
- v2.1: bug hunt/foundation; remove stale/contradictory behavior, fix structure-preserving drum regeneration, state/export inconsistencies, dead paths and unsafe assumptions.
- v2.2: melody phrase engine 2.0; stable 1/2/4-bar motifs, controlled density, singable contour, purposeful cadence and repetition.
- v2.3: perceptual melody novelty; interval/rhythm/contour/degree fingerprints and recent-song history without random-note novelty.
- v2.4: harmony engine; genre-aware progression families, inversions, suspensions, 7ths/borrowed tension only where musically appropriate.
- v2.5: bass/sub intelligence; independent groove, root support, safe mono sub, stronger kick relationship and no lead-like low lanes.
- v2.6: drums 2.0; section-aware kick/snare/hats/percussion, fills and velocity/groove variation.
- v2.7: transition/FX system; risers, impacts, fills, sweeps and pre-drop tension only around real boundaries.
- v2.8: structure engine 2.0; adaptive SongPlan with multiple genre structures instead of one template.
- v2.9: explicit energy curve controlling lane density, presence, velocity, brightness, FX and transition strength.
- v3.0: producer brain; create one shared production plan before rendering MIDI so all lanes make coherent decisions.
- v3.1: prompt understanding 2.0; parse genre, tempo, key/mode, emotion, density, structure, exclusions and section directions.
- v3.2: SoundDNA 2.0; coherent but unique per-song palette, lane-aware synthesis and large variation without fixed presets.
- v3.3: mix engine 2.0; frequency separation, section-aware gain, sidechain, width, reverb and safe louder master.
- v3.4: FL Studio/UI/performance; clear MIDI vs WAV paths, better timeline/piano-roll visibility, direct section audition, responsive UI and low CPU.
- v3.5: final quality/stress pass; no major risky redesigns, only hardening, regression closure, Windows validation and verified package.

## Non-negotiable quality rules
- A prompt must create a complete song, not a repeated loop.
- Every generation must have a genuinely new musical identity while staying stylistically correct.
- Melody novelty must come from musical ideas, never random notes or octave jumps.
- Mainstream hooks should normally use 2-3 meaningful notes per bar and stable 2-4 bar motifs.
- CHORUS states the identity; DROP uses a stronger related variation; FINAL HOOK restores/develops it.
- Full audible pre-drop gap and beat-one drop impact are mandatory.
- BASS, SUB, CHORDS, PLUCK, PAD, COUNTER and FX must remain inside their musical roles.
- Preview must be audible, finite, controlled and not bass-heavy.
- Preview and WAV export must share SoundDNA, lane mix, section gains, sidechain/reverb/master logic.
- MIDI carries notes, not synth audio. Exact SONARA sound uses SONARA+matching SoundDNA or rendered WAV/stems.
- Project/DAW state must restore arrangement, BPM, lane mix, selected lane and matching live SoundDNA.
- Idle instances should be effectively free; avoid audio-thread allocation and pathological unison/FX combinations.

## Required regression coverage
Verify melody register/density/spikes, hook identity-with-variation, pre-drop gap, drop downbeat, section energy contrast, role-pure support lanes, prompt constraints, repeated-generation novelty, preview safety/loudness/CPU, exact lane/live SoundDNA sync, project restore, LEAD/SELECTED/MULTITRACK MIDI separation, WAV/stem rendering, section audition and Windows VST3 build.

## Definition of done
v3.5 is ready only when the exact final HEAD:
1. builds Windows x64 Release;
2. passes every regression;
3. packages SONARA.vst3;
4. uploads the artifact ZIP;
5. writes BUILD_LOG with version=3.5.0, exact commit, runner=Windows and tests=passed;
6. reports final SHA-256 and implemented features.
