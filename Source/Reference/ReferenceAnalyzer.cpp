#include "ReferenceAnalyzer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sonara {

double ReferenceAnalysis::melodyBeats() const noexcept
{
    double end = 0.0;
    for (const auto& n : melody) end = juce::jmax(end, n.beat + n.length);
    return end;
}

juce::String ReferenceAnalysis::summary() const
{
    if (!valid()) return "No reference loaded";
    return fileName + "  •  " + juce::String(durationSeconds, 1) + " s  •  " + juce::String(estimatedBpm, 1)
        + " BPM  •  " + keyName + "  •  melody " + juce::String((int) melody.size()) + " notes"
        + "  •  RMS " + juce::String(rmsDb, 1) + " dB  •  peak " + juce::String(peakDb, 1) + " dB";
}

ReferenceAnalysis ReferenceAnalyzer::analyseAudio(const juce::File& file) const
{
    ReferenceAnalysis out; out.fileName = file.getFileName();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) return out;

    out.sampleRate = reader->sampleRate;
    out.durationSeconds = reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    const int64_t maxSamples = (int64_t) std::llround(reader->sampleRate * 90.0); // bounded, fast analysis
    const int64_t inspectSamples = juce::jmin<int64_t>(reader->lengthInSamples, maxSamples);
    if (inspectSamples <= 0 || inspectSamples > (int64_t) std::numeric_limits<int>::max()) return out;

    const int channels = juce::jlimit(1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> audio(channels, (int) inspectSamples);
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);

    std::vector<float> mono((size_t) audio.getNumSamples());
    double energy = 0.0; float peak = 0.f;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const float l = audio.getSample(0, i);
        const float r = channels > 1 ? audio.getSample(1, i) : l;
        // Mid/side blend deliberately de-emphasises strongly centred material (often vocals)
        // while retaining enough mid signal for mono instruments and bass.
        const float mid = (l + r) * .5f, side = (l - r) * .5f;
        const float x = channels > 1 ? side * .68f + mid * .32f : mid;
        mono[(size_t) i] = x;
        peak = juce::jmax(peak, juce::jmax(std::abs(l), std::abs(r)));
        energy += (double) mid * mid;
    }
    const float rms = mono.empty() ? 0.f : (float) std::sqrt(energy / (double) mono.size());
    out.rmsDb = juce::Decibels::gainToDecibels(rms, -100.f);
    out.peakDb = juce::Decibels::gainToDecibels(peak, -100.f);
    out.estimatedBpm = estimateTempo(mono, reader->sampleRate);
    out.melody = extractPitchContour(mono, reader->sampleRate, out.estimatedBpm, rms);
    out.keyName = estimateKey(out.melody);
    return out;
}

double ReferenceAnalyzer::estimateTempo(const std::vector<float>& mono, double sampleRate)
{
    if (mono.size() < 4096 || sampleRate <= 0.0) return 120.0;
    constexpr int hop = 512;
    const int frames = (int) mono.size() / hop;
    std::vector<float> onset((size_t) frames, 0.f);
    float previous = 0.f;
    for (int f = 0; f < frames; ++f)
    {
        double e = 0.0;
        const int start = f * hop;
        for (int i = 0; i < hop; ++i) e += std::abs(mono[(size_t)(start + i)]);
        const float env = (float)(e / hop);
        onset[(size_t)f] = juce::jmax(0.f, env - previous * .96f);
        previous = env;
    }

    const double frameRate = sampleRate / hop;
    double bestBpm = 120.0, bestScore = -1.0;
    for (int bpm = 70; bpm <= 190; ++bpm)
    {
        const int lag = juce::jmax(1, (int) std::llround(frameRate * 60.0 / bpm));
        double score = 0.0;
        for (int i = lag; i < frames; ++i) score += onset[(size_t)i] * onset[(size_t)(i - lag)];
        if (score > bestScore) { bestScore = score; bestBpm = (double)bpm; }
    }
    // Prefer the musically common half/double equivalent when the raw autocorrelation locks to a harmonic.
    while (bestBpm > 176.0) bestBpm *= .5;
    while (bestBpm < 82.0) bestBpm *= 2.0;
    return juce::jlimit(70.0, 190.0, bestBpm);
}

int ReferenceAnalyzer::estimateMidiPitch(const float* data, int size, double sampleRate, float& confidence) noexcept
{
    confidence = 0.f;
    if (data == nullptr || size < 256 || sampleRate <= 0.0) return -1;
    double energy = 0.0; for (int i = 0; i < size; ++i) energy += (double)data[i] * data[i];
    if (energy < 1.0e-7) return -1;
    const int minLag = juce::jmax(2, (int)std::floor(sampleRate / 1100.0));
    const int maxLag = juce::jmin(size / 2, (int)std::ceil(sampleRate / 70.0));
    double best = -1.0; int bestLag = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0, normA = 0.0, normB = 0.0;
        for (int i = 0; i < size - lag; i += 2)
        {
            const float a = data[i], b = data[i + lag]; corr += (double)a * b; normA += (double)a * a; normB += (double)b * b;
        }
        const double denom = std::sqrt(normA * normB) + 1.0e-12;
        const double score = corr / denom;
        if (score > best) { best = score; bestLag = lag; }
    }
    confidence = (float)best;
    if (bestLag <= 0 || best < .28) return -1;
    const double frequency = sampleRate / bestLag;
    const int midi = (int)std::llround(69.0 + 12.0 * std::log2(frequency / 440.0));
    return juce::isPositiveAndBelow(midi, 128) ? midi : -1;
}

std::vector<ReferenceNote> ReferenceAnalyzer::extractPitchContour(const std::vector<float>& mono, double sampleRate, double bpm, float globalRms)
{
    std::vector<ReferenceNote> notes;
    if (mono.size() < 4096 || sampleRate <= 0.0) return notes;
    constexpr int frame = 2048, hop = 1024;
    const double beatsPerSecond = bpm / 60.0;
    int activeNote = -1; double activeStart = 0.0; int activeFrames = 0; float activeConfidence = 0.f;

    auto finish = [&](double endSeconds)
    {
        if (activeNote < 0 || activeFrames < 2) { activeNote = -1; activeFrames = 0; activeConfidence = 0.f; return; }
        double startBeat = activeStart * beatsPerSecond, endBeat = endSeconds * beatsPerSecond;
        startBeat = std::round(startBeat * 8.0) / 8.0; endBeat = std::round(endBeat * 8.0) / 8.0;
        const double len = juce::jmax(.125, endBeat - startBeat);
        notes.push_back({startBeat, len, activeNote, juce::jlimit(50, 120, 70 + (int)(activeConfidence * 45.f))});
        activeNote = -1; activeFrames = 0; activeConfidence = 0.f;
    };

    for (size_t pos = 0; pos + frame < mono.size(); pos += hop)
    {
        double e = 0.0; for (int i = 0; i < frame; ++i) e += (double)mono[pos + (size_t)i] * mono[pos + (size_t)i];
        const float rms = (float)std::sqrt(e / frame);
        float confidence = 0.f; int midi = rms > juce::jmax(0.002f, globalRms * .16f) ? estimateMidiPitch(mono.data() + pos, frame, sampleRate, confidence) : -1;
        if (midi < 36 || midi > 96 || confidence < .30f) midi = -1;
        const double seconds = (double)pos / sampleRate;
        if (midi < 0) { finish(seconds); continue; }
        if (activeNote < 0) { activeNote = midi; activeStart = seconds; activeFrames = 1; activeConfidence = confidence; continue; }
        if (std::abs(midi - activeNote) <= 1) { activeNote = (int)std::llround((activeNote * activeFrames + midi) / (double)(activeFrames + 1)); ++activeFrames; activeConfidence = juce::jmax(activeConfidence, confidence); }
        else { finish(seconds); activeNote = midi; activeStart = seconds; activeFrames = 1; activeConfidence = confidence; }
    }
    finish((double)mono.size() / sampleRate);

    // Remove tiny duplicates / impossible overlaps from noisy frames.
    std::vector<ReferenceNote> cleaned; cleaned.reserve(notes.size());
    for (const auto& n : notes)
    {
        if (!cleaned.empty() && n.beat < cleaned.back().beat + .08 && std::abs(n.midiNote - cleaned.back().midiNote) <= 1) continue;
        cleaned.push_back(n);
    }
    return cleaned;
}

juce::String ReferenceAnalyzer::estimateKey(const std::vector<ReferenceNote>& notes)
{
    if (notes.empty()) return "Unknown";
    std::array<double,12> chroma{};
    for (const auto& n : notes) chroma[(size_t)(n.midiNote % 12)] += juce::jmax(.125, n.length);
    static constexpr std::array<double,12> major {6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
    static constexpr std::array<double,12> minor {6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};
    static constexpr const char* names[] {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    double best=-1.0; int bestRoot=0; bool bestMinor=true;
    for(int root=0;root<12;++root)for(int mode=0;mode<2;++mode){double s=0.0;const auto& profile=mode?minor:major;for(int pc=0;pc<12;++pc)s+=chroma[(size_t)pc]*profile[(size_t)((pc-root+12)%12)];if(s>best){best=s;bestRoot=root;bestMinor=mode!=0;}}
    return juce::String(names[bestRoot]) + (bestMinor ? " minor" : " major");
}

ReferenceAnalysis ReferenceAnalyzer::importMidi(const juce::File& file) const
{
    ReferenceAnalysis out; out.fileName=file.getFileName(); out.sampleRate=44100.0; out.estimatedBpm=120.0;
    juce::FileInputStream in(file); if(!in.openedOk()) return out;
    juce::MidiFile mf; if(!mf.readFrom(in)) return out;
    const int tpq=mf.getTimeFormat()>0?mf.getTimeFormat():960;
    for(int t=0;t<mf.getNumTracks();++t)
    {
        const auto* sourceSeq=mf.getTrack(t); if(sourceSeq==nullptr)continue;
        auto seq=*sourceSeq; seq.updateMatchedPairs();
        for(int i=0;i<seq.getNumEvents();++i)
        {
            auto* ev=seq.getEventPointer(i); if(ev==nullptr)continue; const auto& m=ev->message;
            if(m.isTempoMetaEvent()){const double sec=m.getTempoSecondsPerQuarterNote();if(sec>0.0)out.estimatedBpm=60.0/sec;}
            if(!m.isNoteOn())continue;
            double off=m.getTimeStamp()+tpq*.5; if(ev->noteOffObject!=nullptr)off=ev->noteOffObject->message.getTimeStamp();
            out.melody.push_back({m.getTimeStamp()/tpq,juce::jmax(.0625,(off-m.getTimeStamp())/tpq),m.getNoteNumber(),(int)std::round(m.getVelocity()*127.f)});
        }
    }
    std::sort(out.melody.begin(),out.melody.end(),[](const ReferenceNote&a,const ReferenceNote&b){return a.beat<b.beat;});
    out.keyName=estimateKey(out.melody); out.durationSeconds=out.melodyBeats()*60.0/out.estimatedBpm; out.rmsDb=out.peakDb=0.f; return out;
}

bool ReferenceAnalyzer::writeMelodyMidi(const ReferenceAnalysis& a,const juce::File& destination) const
{
    if(a.melody.empty())return false;juce::MidiFile mf;mf.setTicksPerQuarterNote(960);juce::MidiMessageSequence seq;
    auto tempo=juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0/juce::jmax(20.0,a.estimatedBpm)));tempo.setTimeStamp(0);seq.addEvent(tempo);
    auto name=juce::MidiMessage::textMetaEvent(3,"SONARA Reference Melody • "+a.keyName);name.setTimeStamp(0);seq.addEvent(name);
    for(const auto& n:a.melody){auto on=juce::MidiMessage::noteOn(1,n.midiNote,(juce::uint8)juce::jlimit(1,127,n.velocity));on.setTimeStamp(n.beat*960.0);seq.addEvent(on);auto off=juce::MidiMessage::noteOff(1,n.midiNote);off.setTimeStamp((n.beat+n.length)*960.0);seq.addEvent(off);}seq.updateMatchedPairs();mf.addTrack(seq);destination.deleteFile();juce::FileOutputStream out(destination);return out.openedOk()&&mf.writeTo(out);
}

} // namespace sonara