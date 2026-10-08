# SONARA

SONARA is a Windows x64 JUCE C++ VST3 and standalone music-production
instrument for FL Studio. It plans a song before writing its independent lanes
and generates their SoundDNA from reproducible seeds.

## v5 production workflow

- Prompts control genre, mood, tempo, key, exact bar count, energy, density,
  groove, instrument selection/exclusions, melodic character and sound direction.
- Full songs develop an intro, verse, build, chorus/drop, second verse/build/drop,
  final hook and outro. Short requests use a compact complete arc.
- Harmony, melody, bass, sub, drums, supporting parts, arrangement and sounds have
  separate seed domains. Later drops develop the existing hook.
- Preview, full-mix WAV, isolated WAVs and stems use the same cached note schedule,
  SoundDNA, section automation and mixer. MIDI exports that same arrangement.
- The SONG view shows the complete seed, sections, playhead, lanes, selected-lane
  piano roll and sound plan. Click a section to audition it; REPRODUCE uses the seed.
- Save/restore retains the prompt draft, producer plan, full arrangement, SoundDNA,
  mix and selected lane. Failed/cancelled generation clears the old output.
- Reference analysis, RESOUND, REBUILD, SoundDNA mutation, lane mixing, patch locks,
  A/B history and Cyanoryx interchange remain available.

See [FL_STUDIO_WORKFLOW.md](FL_STUDIO_WORKFLOW.md) for installation, MIDI/audio
transfers, stems and project restoration. MIDI contains notes and metadata;
SONARA's exact synth audio travels through WAV/stems or SONARA with saved SoundDNA.

## Build and release verification

Build with Visual Studio 2022, CMake 3.22 or newer and JUCE 8.0.12:

```sh
cmake -S . -B build-win64 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build-win64 --config Release --parallel
ctest --test-dir build-win64 -C Release --output-on-failure
```

The GitHub Actions Windows workflow builds Release, runs every regression
suites including VST3 host discovery, validates the x64 bundle and uploads
`SONARA-Windows-x64-VST3`. Its downloadable ZIP includes the complete VST3 folder,
installation/workflow instructions, `BUILD_LOG.txt` and `SHA256SUMS.txt`.

A release is verified only when those checks pass for the exact branch HEAD and
the downloaded artifact's contents, commit, version and checksums are inspected.
Use its `BUILD_LOG.txt` to identify the commit and CI run that produced the binary.
Development checkpoints and validation history are in
[DEVELOPMENT_V5.md](DEVELOPMENT_V5.md).
