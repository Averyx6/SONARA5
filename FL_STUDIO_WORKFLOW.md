# SONARA in FL Studio

Install the entire `SONARA.vst3` folder in `C:\Program Files\Common Files\VST3\`,
then verify/rescan it in FL Studio's Plugin Manager. Open SONARA as an instrument.

## Create and audition

1. In SONG, describe genre, mood, BPM, key, bars, energy, density, groove,
   instruments, drop and sound character. For example:
   `Emotional progressive house, 128 BPM F minor, 80 bars, memorable simple hook,
   powerful melodic drop, warm chords, clean deep bass, sparse verses, no pads`.
2. GENERATE creates a new plan, harmony, melody, grooves, arrangement and SoundDNA.
   RANDOMIZE makes a new song from the brief. SURPRISE ME supplies a new brief.
3. Click a section header to audition its start; click a lane to select its piano
   roll. PAUSE/RESUME preserves playback state; STOP/restart clears DSP history.
4. The inspector shows BPM/key, the full hexadecimal seed, plan and SoundDNA.
   REPRODUCE rebuilds the prompt and seed. Keep an explicit BPM/key in the brief.
   To change an existing song's tempo, update the prompt and generate/reproduce.
5. FX & MIX edits a selected lane's sound and level/pan/width/send. Bass/sub stay
   controlled; the sub stays centered. Unsupported lane mix controls are disabled.

## Move the result into FL Studio

| Transfer | What it contains | Use |
| --- | --- | --- |
| Full song MIDI | Named tracks, notes, velocities, tempo/meter, section markers and full song timing | Import the MIDI as separate FL channels/patterns, or save through MIDI/EXPORT |
| Selected MIDI | One selected lane at its original song position | Drag to the target Piano Roll or save the selected MIDI |
| Full mix WAV | The complete rendered SONARA sound and mix | Drag to the Playlist or save through EXPORT |
| Selected WAV / stems | Individual rendered lanes with common bar-1 start and decay tail | Place every file at the same Playlist start, at the printed song tempo |

For audio, the first gesture prepares the file in the background. After AUDIO
READY appears, drag the same WAV/stems button into FL Studio. DRAGGING indicates
the OS file transfer has started; SONARA cannot confirm whether FL accepted it.
If an OS drag is rejected, save/export the file and import it from FL's browser.

MIDI contains notes and timing. It cannot embed SONARA's custom synth audio or
reverb. Use WAV/stems for the exact rendered sound, or keep SONARA loaded with its
saved project/SoundDNA. FL's MIDI import settings determine channel/pattern placement.
Set FL's tempo to SONARA's tempo before aligning audio; avoid unintended stretching.

All audio exports are stereo 24-bit WAV at SONARA's prepared host sample rate.
Stems include a four-second decay tail and keep full song position, including
silence before a lane enters. A saved project, full MIDI and timing instructions
are included with a stem set. Each stem has its own safe master processing; summing
them does not reproduce the nonlinear mastered full mix bit for bit.

## Save and resume

EXPORT can save/load a `.sonaraproject` with the prompt draft, full seed, producer
plan, sections, notes, SoundDNA, lane mixer and selected lane. FL project state
also stores these values. Saving preserves edits; REPRODUCE rebuilds the seeded
composition rather than restoring subsequent manual sound/drum changes.

A failed or cancelled song generation clears its output and exposes the failure.
The interface never presents the previous song as a successful new generation.
