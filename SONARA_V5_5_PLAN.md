# SONARA v5.5 checkpoints

- v5.1: normalize Piano Roll exports to one lane/channel, explicit melodic transfer,
  route incoming arrangement MIDI through the shared mixer, verify note/audio parity.
- v5.2: deterministic multi-seed audio benchmarks, measured melody/drum balance
  and useful loudness; preserve the master ceiling and CPU budgets.
- v5.3: diagnose and improve phrase/final-drop development and actual musical
  novelty across seeds and genre briefs, with recorded comparisons.
- v5.4: remove ambiguous/unsupported controls and verify all retained actions,
  exact project/state reproducibility, exclusions and stale-output handling.
- v5.5: stabilize; full local regression and exact-commit Windows Release/discovery;
  inspect and deliver the exact-run artifact. No speculative late features.

Use the existing checkout. Preserve uncommitted work and check remote HEAD before
each push. Inspect existing CI and resume it; do not cancel an unrelated active run.
