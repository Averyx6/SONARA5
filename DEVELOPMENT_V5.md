# SONARA v5 development

Base: `dcce4f3387d038e856377948372b84094c4ae64b`, branch `v3.5-development`.
Windows run 193 passed the original seven regressions, host discovery, Release
build and packaging before this work started.

## v4.1 — producer constraints

- Resolve negative language and instrument exclusion lists before any writer.
- Support duration in seconds/minutes and explicit bar counts through 512 bars.
- Make explicit density, space, aggression and final evolution authoritative.
- Publish canonical ordered events, validate plans and enforce event budgets.
- Add deterministic generation from a full-width seed.
- Clear displayed/exportable song state before generation; failed requests cannot
  retain an old song as their output.
- Add producer intelligence regressions; preserve all existing musical thresholds.
- Use portable integer min/max calls where JUCE's SIMD overloads fail on Linux.

Validation: local Release arrangement and producer intelligence suites passed.
Final release remains blocked on the exact Windows HEAD and verified ZIP.

## v4.2 — developed arrangements

- Eleven planned sections include a second verse/build/drop and a deconstructed outro.
- Drops retain the chorus motif; second and final statements develop its rhythm/register.
- Planned chord inversions now reach chord writing.
- Endings leave a final bar for decay; low end withdraws before the final silence.
- Preserve all original quality/novelty assertions and add hook-continuity/ending tests.

Validation: local Release arrangement, producer and processor playback suites passed,
including the unchanged real-time and multi-instance CPU budgets.

## v4.3 — audible SoundDNA identities

- Seeded oscillator materials, harmonic FM, phase, envelopes and modulation now
  vary instruments within their musical roles. Explicit tone/space/aggression wins.
- Remove hidden low-CPU patch rewrites; stored SoundDNA is what voices receive.
- Pure sub contains no hidden FM, pitch sweeps, morphing or transient/noise layer.
- Restore pitch-wheel state correctly on each voice start; schedule MIDI at one-sample precision.

Validation: local Release producer, arrangement and full processor acceptance passed;
original CPU limits and musical safety assertions unchanged.

## v4.4 — one playback/render path

- Extract the existing musical mixer into a single bounded renderer shared by
  processor preview, full mix and isolated stems; preserve SoundDNA and mix state.
- Cache complete sample-timed on/off schedules off the audio thread: long pads/FX
  no longer lose note-offs through different preview/export lookback windows.
- Use independent deterministic drum voices per lane and apply drum level/pan.
- Share seek fade, section-boundary automation, host sample rate and four-second
  decay tail; pause preserves state and restart clears voices and all FX buffers.
- Serialize renderer reconfiguration/seek with the JUCE callback lock.
- Verify actual host preview against exported PCM, isolated sub against its stem,
  held note termination and repeat-start DSP reset.

Validation: local Release processor acceptance passed, including original CPU,
DSP safety and loudness assertions and new sample parity checks (2e-6 tolerance).

## v4.5 — FL Studio transfer workflow

- Guard external drag once per gesture, show DRAGGING and handle OS drag failure
  without claiming a DAW imported the file. Audio preparation runs off the GUI
  thread; cached transfers are ready on the next drag with progress/cancellation.
- Keep transferred audio in SONARA/Transfers so closing the plugin does not
  invalidate DAW file references. Add full/selected MIDI save controls.
- Canonicalise same-pitch overlaps and duplicate hits in the shared note plan,
  fixing JUCE MIDI pairing that previously changed exported drum lengths.
- Export section markers, meter and complete outro duration; verify actual MIDI
  lane names, pitches, positions, lengths and velocities against the plan.
- Make generation/render status and SoundDNA GUI reads thread safe.

Validation: local Release plugin UI compiled; arrangement, producer and full
processor suites passed, including stronger MIDI round-trip and WAV parity tests.

## v4.6 — usable interface and faithful state

- Keep the existing seven tabs; fit every interactive control at 1180x720,
  1320x820 and 1900x1200. Remove duplicate song drag controls and display the
  full hexadecimal seed with deterministic REPRODUCE. Section clicks audition.
- Save edited prompt drafts independently from the current song, and restore
  the seed domains, selected lane, mix, arrangement and SoundDNA.
- Preserve same-beat note ordering on restore: an unstable sort reordered chord
  voices and changed playback of otherwise identical saved MIDI.
- Route reference audition and RESOUND WAV through the common renderer at the
  host sample rate; compare their PCM at a 2e-6 tolerance.
- Align genre/mood scoring with the harmony planner, fixing impossible mixed
  tech-house/cinematic briefs without reducing quality or novelty thresholds.
- Tighten the master ceiling to .950 after Windows run 196 exposed .955 against
  the unchanged .951 playback assertion. Invalid/cancelled generation tests
  explicitly verify that no old arrangement or seed remains published.
- Add a ninth regression suite for all seven UI tabs, three sizes and section
  audition; serialize state operations and guard UI sound/status reads.

Validation: UI snapshots inspected; final Windows release gate remains pending.
