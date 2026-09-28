# SONARA v2.0 MASTER BUILD / FIX PROMPT

## Mission
Finish SONARA as a usable Windows x64 JUCE VST3 for FL Studio. Continue from the latest verified source; never restart architecture or revert to older branches to make tests pass. Fix root causes, preserve working features, and ship only after Windows Release build + regression tests + VST3 packaging pass.

## Non-negotiable user goals
1. A prompt must generate a complete song arrangement, not one repeated loop.
2. Every generation must produce a genuinely different musical identity while staying inside the requested genre/style.
3. Melody must be memorable, simple enough to understand, musical, singable, and section-aware. Novelty must never be achieved by random note spam.
4. No accidental isolated notes far above or below the melody. Mainstream EDM lead stays in one readable register family.
5. INTRO / VERSE / BUILD / CHORUS / DROP / BREAKDOWN / BUILD 2 / FINAL HOOK must have obvious musical jobs.
6. The DROP must be instantly recognizable: breathing gap before it, impact on beat one, stronger drum/bass energy, and a related but more driving hook variation. Do not copy the chorus forever.
7. Support lanes must support the lead:
   - BASS: low root/groove role only.
   - SUB: fundamental low end only.
   - CHORDS: sustained harmonic blocks.
   - PLUCK: sparse support, never a second random melody.
   - PAD: slow harmony only.
   - COUNTER: optional answer phrases with real space.
   - FX: transitions/impacts only.
8. Preview must be loud enough, controlled, finite, and not bass-heavy or speaker-dangerous.
9. Preview and exported WAV must use the same SoundDNA, lane mix, section gains, sidechain behavior, reverb path, and master gain.
10. FL Studio workflow:
    - Selected/Lead MIDI = editable notes for Piano Roll.
    - Multitrack MIDI = separate arrangement lanes; never imply one merged Piano Roll.
    - MIDI cannot contain synth audio. To hear SONARA SoundDNA from MIDI, drop it on a SONARA channel with the matching lane loaded.
    - Lead/selected-lane WAV and full-mix WAV preserve the actual SONARA sound.
    - Drag operations must be visibly labelled so the user knows which path preserves sound.
11. Keep CPU/memory reasonable. Idle instances must be near-free. Avoid expensive per-block allocation and unnecessary synthesis while silent.
12. Project save/load must restore arrangement, BPM, lane mix, selected lane, and matching live SoundDNA.

## v2 quality gates
Reject a candidate rather than publish it when any of these happen:
- recycled melody skeleton / near-identical harmony / familiar whole-song fingerprint;
- mainstream lead has isolated pitch spikes;
- mainstream lead falls below the intended lead register;
- hook bars become note soup;
- sections have no meaningful density/energy contrast;
- DROP has no clear downbeat arrival;
- no pre-drop breathing gap;
- support lanes detach by an octave from the lead without being BASS/SUB;
- prompt requirements (key, mode, genre structure) are materially ignored.

Novelty is a constraint, not the creative target. Prefer a strong song that is sufficiently different over a strange song that is merely different.

## Melody design rules
- Use stable 2–4 bar motifs and recognizable repetition.
- Mainstream EDM hooks: usually 2–3 notes per bar, occasionally 4 only when musically justified.
- Strong beats favor chord tones; weak beats may use controlled scale passing tones.
- Avoid random octave changes.
- Limit unexpected leaps; repair one-off spikes.
- Chorus states the main identity.
- Drop simplifies/strengthens that identity into a more driving cell.
- Final hook can restore/develop the fuller phrase.
- Verse/build/breakdown use more space and partial motifs.
- Build pickups should occur near the transition, not clutter every bar.

## Structure / production rules
- Preserve an audible pre-drop gap.
- Make drop kick/bass/lead energy higher than build/breakdown.
- Use transition FX and drum-roll tension before the drop.
- Keep breakdown clearly lower energy.
- Do not let every section use the same full instrumentation.
- Keep drums punchy and stylistically appropriate; no bizarre preview-only drum behavior.

## Sound / preview / export parity
- Generated lane SoundDNA must be loaded into the corresponding preview engines.
- Selecting a non-drum lane must load its SoundDNA into the live SONARA instrument.
- After generation, default selected lane should be LEAD and live SONARA patch must equal generated LEAD SoundDNA.
- Full-mix export must mirror preview mix decisions.
- Lane WAV must render its own SoundDNA.
- Use soft limiting/saturation for safety but do not make preview quiet.
- Keep SUB mono/low-passed and BASS narrow enough to avoid low-end smear.

## FL Studio drag features
- PLAY CHORUS button: jump directly to the first CHORUS for fast hook comparison.
- PLAY DROP button: jump directly to the first DROP for audition.
- LEAD MIDI: always exports only LEAD notes, regardless of current lane selection.
- LEAD WAV: always renders LEAD with its SONARA SoundDNA.
- SELECTED MIDI/WAV: current lane workflow remains available.
- MULTITRACK MIDI: includes section markers and separate tracks.
- EXACT PREVIEW WAV: full rendered mix with SONARA sounds preserved.
- STEMS: separate rendered lanes.

## Regression requirements
Tests must verify:
- no mainstream LEAD note below the allowed floor;
- no isolated high/low melody spike;
- hook note-density ceiling;
- full pre-drop breathing gap;
- drop downbeat anchor;
- chorus and drop share identity but are not exact four-bar clones;
- LEAD SoundDNA is synchronized into the live SONARA instrument after generation and project load;
- preview is finite, safe, audible, and within CPU budget;
- repeated GENERATE TRACK calls produce materially different melodies/harmony/whole-song fingerprints;
- exports exist and contain real audio/MIDI data;
- Windows VST3 package is created only after every test passes.

## Definition of done
Do not call v2 ready because it compiles. It is ready only when:
1. Windows x64 Release build succeeds.
2. All regression tests pass.
3. Verified SONARA.vst3 is packaged.
4. Artifact ZIP is downloadable.
5. Build log identifies the exact commit and says tests passed.
6. The final report lists implemented features, fixed root causes, any unavoidable MIDI limitation, and the verified commit.

## Current release target
Branch: `v2.0-release`
Version: `2.0.0`
All code and regression work must be validated from the branch HEAD, never from an older queued workflow run.
