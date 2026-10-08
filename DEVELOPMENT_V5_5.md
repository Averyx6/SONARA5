# SONARA v5.5 development

Baseline: e7bf9c07229a06c373f41584261c63a2595c12a4, v3.5-development.
Baseline Windows run 198 / 37475148433 succeeded. The downloaded official ZIP
matches the user's uploaded v5.0 ZIP exactly:
SHA-256 3a964825fc140406db5261e2cf825da6f781c14c8e0592b5bd21cf9b3c1cfe34.
Archive CRC, all five manifest hashes, strict 5.0.0 instrument metadata, x64 PE
and exact-commit tests=passed BUILD_LOG were verified. No concurrent newer run
or uncommitted work existed when this pass started.

## v5.1 — Piano Roll and incoming MIDI

- Single-lane exports use one track on channel 1; full-song MIDI remains available
  through an explicit multitrack save action, not the Piano Roll drag row.
- Dedicated lead drag selects LEAD's SoundDNA and single-lane routing.
- Selected-lane MIDI is omni so FL note colours cannot select drums accidentally.
  Explicit song-channel mode routes each stored melodic channel and channel-10 kit.
- Host notes use SongRenderEngine and the same SoundDNA, lane mixing, section
  automation, FX and .950 master ceiling as song preview/WAV/stems.
- Preserve DSP history over leading rests; reset seeks without injecting internal
  arrangement notes into host playback. Save routing mode and sound mode in state.
- Opening extra instances no longer caps unison or bypasses patch FX based on
  instance count. Preserve the existing four-voice live cap and unloaded idle path.

Local production routing tests passed: multitrack MIDI versus preview has zero
sample error; seven melodic lanes preserve channels 1/10/16, pitch wheel, mute,
single-track note counts and exports. Identical persisted standalone SoundDNA
produces identical samples with one instance and five instances. Existing playback
tests also passed before the final routing additions, including CPU and 185-second
stability. Final v5.1 Linux Release build and all nine CTest suites passed (70.33 seconds),
including the expanded playback acceptance, all seven UI tabs/three sizes and native
VST3 discovery. Exact v5.1 Windows run 37776043170 passed all nine suites and
packaging. Its downloaded ZIP was verified against cce1415c1f34c45ed004d6396ad79a39020b92f0:
SHA-256 848657c557a43de1ead82ca462f27283d7c9a4d62af8a7f2118fe8cbd9f42b08.

## Deterministic baseline measurements

Tests/ProductionBenchmark.h records four-bar DROP audio for progressive house,
tech house and trance, seeds 0x510001–0x510003. Group 0 is full mix, 1 drums,
2 melody/harmony (CHORDS through COUNTER), 3 LEAD. Metrics are RMS dBFS, not LUFS.
Recorded WAVs, metrics.csv and arrangement XML are under
/workspace/.sonara-environment/measurements-v5.0 (development evidence, not releases).

Baseline drums: -11.65 to -11.32 dBFS; melody/harmony: -24.00 to -14.79 dBFS;
lead: -34.16 to -15.86 dBFS; full drops: -10.84 to -8.84 dBFS.
Thus the principal complaint is musical balance and quiet individual lead patches,
not simply low full-mix peaks. The quiet tech-house lead uses a triangle and a
777.6 Hz lowpass; automatic 'airy' flavour can also select an inappropriate highpass.

Next concrete task: inspect the v5.1 Windows gate, then fix automatic role-safe
lead articulation/material/filter range and rebalance melody/drums using the same
seeds and recorded audio. Keep .950 ceiling and every existing assertion unchanged.
Do not claim musical quality or FL Studio acceptance from technical CI alone.

## v5.2 — measured musical balance

- Automatic LEAD retains a lowpass melodic body unless an explicit highpass/thin
  lead is requested; bounded attack/sustain keeps short genre articulations audible.
  Triangle LEAD/CHORDS use one unison voice to avoid seeded phase cancellation.
  Custom lane designs and explicit dark/bright directions remain supported.
- Increase CHORDS/PLUCK/LEAD/COUNTER mixer balance and reduce drop drum gain.
  Master drive and .950 sample ceiling remain unchanged across host/preview/exports.
- Add required `sonara_production_balance`: nine deterministic four-bar drops,
  each with full/drum/harmonic/lead render measurements and full/lead 24-bit WAVs.
  Require full >= -15, harmonic >= -20, lead >= -24 RMS dBFS and harmonic within
  6 dB of drums. Keep all existing assertions. Windows saves the recorded audio
  as a separate measurement artifact; BUILD_LOG counts actual registered tests.
- Add exact-run ZIP verification and optional FFmpeg EBU R128 measurement tools.

The same nine seeds now measure: lead -22.15 to -14.78 dBFS (baseline worst
-34.16); harmonic -19.01 to -13.30 (baseline worst -24.00); drum -13.20 to
-12.88; full -11.65 to -9.24. Worst harmonic/drum deficit improves from 12.53
to 5.98 dB. Quiet tech-house seed lead improves 12.01 dB RMS and 11.9 LUFS
(-30.5 to -18.6). Progressive quiet lead improves 3.92 dB RMS; trance 3.27.
These are fixed benchmark excerpts, not universal quality guarantees.

FFmpeg R128 measures full mixes -9.5 to -6.9 LUFS. Sample peak stays <= .950,
but reconstruction true peaks are +0.2 to +0.5 dBFS (baseline +0.9 to +1.3).
The sample ceiling is not a true-peak limiter; do not describe it as one.
Recorded comparison evidence: measurements-v5.0 versus build-v5/production-benchmark.
Full v5.2 Linux x64 Release built and all ten registered suites passed (108.66 s),
including unchanged long playback/CPU, UI and native VST3 discovery checks.
The comparison CSVs are committed in Evidence/v5.2. v5.2 commit
454bad11d36188e77630710f366e860b974eceea was pushed after the v5.1 run finished.
Exact v5.2 Windows run 37778599458 is building; do not cancel it with a new push.

Next concrete task: preserve tech-house's short syncopated articulation while
bounding its accidental high register and developing the answer in four-bar
final hooks. Verify actual note/rhythm/harmony diversity, not only seed IDs.

## v5.3 — hook memory, short final answers and groove variety

- The new production composition regression initially failed because tech-house
  changed its hook between drops. Preserve its two-bar call in middle register,
  keep short syncopated articulation, and develop the final response with a
  controlled displacement and a harmony-guided ending. Intentional repeated
  pedal notes no longer trigger arbitrary octave jumps.
- Four-bar mainstream final hooks now develop their answer half; longer finals
  retain their full initial statement and develop later blocks. Explicit small
  final evolution still controls development; preserve every existing hook test.
- A seeded two-bar trance percussion cell adds audible rhythmic variety while
  retaining the four-on-floor and sixteen-hat skeleton and protected pre-drop gap.
- Required production composition test covers 16 seeds each of progressive,
  tech-house and trance: matching primary calls in DROP/DROP 2/FINAL, different
  final answers, playable pitches, tech articulation and no-pad exclusions, plus
  actual relative note/onset/length, actual chord pitch/onset and drum patterns.

Measured production fingerprints / 16 seeds:

| Genre | Melodies | Harmonic plans | Actual chord-note patterns | Drum patterns |
|---|---:|---:|---:|---:|
| Progressive | 16 | 16 | 16 | 13 |
| Tech-house | 16 | 14 | 16 | 16 |
| Trance | 16 | 16 | 16 | 15 |

Trance drum patterns were 9 before the percussion change, failing the fixed
12-pattern requirement. The fix passes without relaxing it. Targeted composition,
balance, arrangement and producer tests pass. Full v5.3 Linux Release built and
all eleven suites passed (101.74 s), including expanded playback, UI and discovery.
Quiet tech-house lead now measures -21.24 dBFS versus baseline -34.16 (12.92 dB
improvement); harmonic/drum deficit is 5.29 dB versus baseline 12.53. The complete
nine-seed RMS evidence is in Evidence/v5.3. Windows v5.2 remains in flight; v5.3 is
locally committed without replacing/cancelling that run.
These are structural/recorded measurements, not listening or FL Studio approval.

Next concrete task: incoming MIDI transport lifecycle, repeat playback/state sound
reproducibility, generated-arrangement idle cost and honest UI control visibility.

## v5.4 — reproducible performance and honest controls

- JUCE calls inactive voices too. Guard their render path so stopped filter residue
  cannot leak over leading rests; reset stored channel pitch wheels on restart/seek.
  Regression first reproduced 0.027–0.031 sample error on restart and 0.417 after
  a previous pitch bend. Both now reproduce the fresh saved performance exactly.
- Detect host seeks by expected musical position using host tempo. Do not confuse
  a saved-song/host tempo difference with a seek on every callback. Stop transitions
  clear notes/FX; seeks never chase notes from the internal song into host MIDI.
- Persisted project versus host state renders identical incoming audio (zero error),
  including the selected custom sound, channel mode and lane mix. Five loaded
  arrangements remain silent while idle: 0.247 s against the existing 1.5 s limit.
- Remove the unused whole-song drag and false AI headline. Keep explicit full MIDI
  save. Hide prompt/style controls on transfer/mix tabs where they do not apply.
  Show saved song tempo as text; sound-preview tempo is editable only in INSTRUMENT.
  Inspector style reports the generated song, not an ungenerated prompt draft.
- Prepared audio cache hashes actual persisted notes/sounds/macros/mix/reference,
  replacing pointer/seed checks that could reuse stale WAVs after same-seed edits.
  Existing working reference, SoundDNA, mutation, mix and save/export features stay.
- Required lifecycle suite covers restart, bend, different host tempo, seek/stop,
  loaded idle cost and actual project/state audio. UI checks mode actions, control
  visibility and all seven layouts at three sizes, with unchanged existing checks.

Full v5.4 Linux x64 Release built and all twelve suites passed (57.16 s), including
the unchanged 185-second stability/CPU, state/export, UI and native discovery gates.
This checkpoint is locally committed while the v5.2 Windows build finishes.
Next concrete task: final stabilization of explicit routing mode transitions, GM
aliases and bounded host MIDI; record final-answer audio and rerun every gate.
