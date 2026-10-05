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
