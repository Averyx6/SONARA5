# Final v5.5 production evidence

Linux x64 Release, 44.1 kHz stereo, 512-sample blocks. Prompts/seeds/groups are the
same as Evidence/v5.2. `checkpoint-rms.csv` has 72 rows: first four DROP bars and
last four FINAL HOOK bars, four isolated/full groups each. `checkpoint-loudness.csv`
has FFmpeg EBU R128 measurements of the 36 full/lead 24-bit WAVs. Windows CI
publishes its own exact-commit recordings as `SONARA-production-measurements`.

Compare DROP rows with v5.0 baseline-rms.csv; final-answer rows have no identical
baseline excerpt in that file. Musical structure is independently checked across
48 production seeds without sound/seed IDs substituting for actual notes.

Full thirteen-suite Linux gate passed in 76.92 seconds. The release gate requires
the final exact Windows HEAD and downloaded ZIP validation; its BUILD_LOG and
SHA256SUMS are the immutable run/commit/test evidence. Listening and FL Studio
acceptance remain user-machine checks. See DEVELOPMENT_V5_5.md for measurements,
fixes, limitations and the next task. The .950 ceiling is a sample ceiling.
