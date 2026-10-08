# v5.2 audio comparison

Baseline: e7bf9c07229a06c373f41584261c63a2595c12a4, immutable v5.0 renderer.
Checkpoint: v5.2 MixPolicy and automatic SoundDNA changes, Linux x64 Release,
44.1 kHz stereo, 512-sample blocks. Version labels do not determine measurements.

Run `SONARA_PlaybackTests --benchmark-check <directory>` to write nine arrangements,
full mix/lead WAVs and RMS measurements. Optionally run
`python Tools/measure_loudness.py <directory>` with FFmpeg for integrated LUFS and
reconstruction true peaks. Windows CI saves its own recordings and measurements.

Prompt indices: 0 progressive house / F minor / 128 BPM; 1 tech house / A minor /
126 BPM / no pads; 2 trance / D minor / 138 BPM. Every prompt requests 64 bars.
Seeds: hexadecimal 510001, 510002, 510003. Excerpts: first four bars of DROP.
Groups: 0 full, 1 drums, 2 CHORDS through COUNTER, 3 LEAD.

Compare the same prompt/seed/group. RMS dBFS measures energy, LUFS uses perceptual
weighting. Short sparse melodies naturally have lower average level. Isolated
groups each pass through the shared master; nonlinear master processing means
their sum does not equal the processed full mix. These excerpts demonstrate the
reported balance improvement, not listening approval or FL Studio acceptance.

The .950 sample ceiling is preserved. R128 reconstruction true peaks exceed zero
for some excerpts; it is not a true-peak limiter. The checkpoint improves this
from baseline +0.9–+1.3 dBFS to +0.2–+0.5 dBFS without raising master gain.
