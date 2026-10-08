# SONARA v5.5 implementation brief

Authorized repository: Averyx6/SONARA5, branch v3.5-development.
Verified v5.0 baseline: e7bf9c07229a06c373f41584261c63a2595c12a4.

Address the reported drum-heavy Piano Roll transfers, quiet/weak songs and
misleading controls through production code and measured regressions. Preserve
the working v4/v5 planning, seed domains, shared preview/export renderer, SoundDNA,
reference tools, cancellation, state and FL transfer systems.

Priorities:

1. Single melodic lane MIDI for Piano Roll, correct incoming channels and sounds.
2. Shared mixer/SoundDNA for incoming MIDI, preview, WAV and stems; no instance-count sound changes.
3. Audible melody/harmony and useful measured loudness under the existing .950 ceiling.
4. Memorable developed hooks, genre grooves, coherent harmony, deliberate seed variety.
5. Working, clear controls and reproducible saved projects.

MIDI carries notes, not synth audio. Exact audio transfers via WAV/stems or SONARA
with saved SoundDNA. Technical CI does not prove musical quality or acceptance by
the user's FL Studio installation.

Never weaken existing assertions. Each checkpoint must carry production-path
tests and measurements. A final v5.5 package requires exact-HEAD Windows Release,
all regressions and VST3 discovery, and a downloaded ZIP with verified bundle,
strict metadata, x64 PE, checksums and exact-commit tests=passed build record.
