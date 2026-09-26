#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

SonaraAudioProcessor::SonaraAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    sessionSalt = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks());
    patchHistory.push_back(engine.patch());
    historyIndex = 0;
}

void SonaraAudioProcessor::prepareToPlay(double sr, int bs)
{
    previewSampleRate = juce::jmax(8000.0, sr);
    maximumBlockSize = juce::jmax(1, bs);
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * 16.0);

    engine.prepare(sr, bs, getTotalNumOutputChannels());
    for (auto& e : songEngines) e.prepare(sr, bs, getTotalNumOutputChannels());
    drumSynth.prepare(sr);

    const int channels = juce::jmax(1, getTotalNumOutputChannels());
    for (auto& b : songScratch)
    {
        b.setSize(channels, maximumBlockSize, false, false, true);
        b.clear();
    }
    for (auto& m : songMidi) m.ensureSize(16384);
}

bool SonaraAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SonaraAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals noDenormals;
    b.clear();

    if (songPlaying.load(std::memory_order_acquire))
    {
        m.clear();
        renderSongBlock(b, b.getNumSamples());
        return;
    }

    injectPreviewMidi(m, b.getNumSamples());
    engine.render(b, m);

    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(c, i, std::tanh(b.getSample(c, i) * .98f));
}

void SonaraAudioProcessor::startPreview()
{
    stopSongPreview();
    const double beats = referenceMelodyPreview && referenceLoaded && !reference.melody.empty() ? juce::jmax(1.0, reference.melodyBeats()) : 16.0;
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * beats);
    previewSample.store(0);
    previewPlaying.store(true);
    generationStatus = referenceMelodyPreview ? "RESOUND preview playing" : "Sound preview playing";
}

void SonaraAudioProcessor::stopPreview()
{
    previewPlaying.store(false);
    previewSample.store(0);
    engine.allNotesOff();
    generationStatus = "Sound preview stopped";
}

void SonaraAudioProcessor::injectPreviewMidi(juce::MidiBuffer& m, int numSamples)
{
    if (!previewPlaying.load() || numSamples <= 0) return;
    const int64_t start = previewSample.load(), end = start + numSamples;

    if (referenceMelodyPreview && referenceLoaded && !reference.melody.empty())
    {
        const double spb = previewSampleRate * 60.0 / previewBpm;
        const double startBeat = (double)start / spb, endBeat = (double)end / spb;
        auto it = std::lower_bound(reference.melody.begin(), reference.melody.end(), startBeat - 4.5,
                                   [](const sonara::ReferenceNote& n, double beat){ return n.beat < beat; });
        for (; it != reference.melody.end() && it->beat <= endBeat; ++it)
        {
            const int64_t on = (int64_t)std::llround(it->beat * spb);
            const int64_t off = (int64_t)std::llround((it->beat + it->length) * spb);
            if (on >= start && on < end) m.addEvent(juce::MidiMessage::noteOn(1,it->midiNote,(juce::uint8)juce::jlimit(1,127,it->velocity)),(int)(on-start));
            if (off >= start && off < end) m.addEvent(juce::MidiMessage::noteOff(1,it->midiNote),(int)(off-start));
        }
    }
    else
    {
        const int notes[] = {60,63,67,70,72,70,67,63,60,63,67,75,74,70,67,63};
        const int64_t step = juce::jmax<int64_t>(1, previewLengthSamples / 16);
        for (int i = 0; i < 16; ++i)
        {
            const int64_t on = (int64_t) i * step;
            const int64_t off = juce::jmin<int64_t>(previewLengthSamples - 1, on + (step * 3) / 4);
            if (on >= start && on < end) m.addEvent(juce::MidiMessage::noteOn(1, notes[i], (juce::uint8)108), (int)(on - start));
            if (off >= start && off < end) m.addEvent(juce::MidiMessage::noteOff(1, notes[i]), (int)(off - start));
        }
    }

    previewSample.store(end);
    if (end >= previewLengthSamples) previewPlaying.store(false);
}

bool SonaraAudioProcessor::writePreviewMidiFile(const juce::File& destination) const
{
    if(referenceMelodyPreview && referenceLoaded && !reference.melody.empty())
        return referenceAnalyzer.writeMelodyMidi(reference,destination);

    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    auto tempo = juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / previewBpm));tempo.setTimeStamp(0);seq.addEvent(tempo);
    auto name=juce::MidiMessage::textMetaEvent(3,"SONARA Sound Preview");name.setTimeStamp(0);seq.addEvent(name);
    const int notes[] = {60,63,67,70,72,70,67,63,60,63,67,75,74,70,67,63};
    for (int i = 0; i < 16; ++i){const double on=i*960.0,off=on+720.0;auto a=juce::MidiMessage::noteOn(1,notes[i],(juce::uint8)108);a.setTimeStamp(on);seq.addEvent(a);auto z=juce::MidiMessage::noteOff(1,notes[i]);z.setTimeStamp(off);seq.addEvent(z);}
    seq.updateMatchedPairs();mf.addTrack(seq);destination.deleteFile();juce::FileOutputStream out(destination);return out.openedOk()&&mf.writeTo(out);
}

void SonaraAudioProcessor::setPatchWithHistory(const sonara::SoundDNA& d)
{
    engine.setPatch(d);
    if (historyIndex + 1 < (int) patchHistory.size()) patchHistory.erase(patchHistory.begin() + historyIndex + 1, patchHistory.end());
    patchHistory.push_back(d);
    if (patchHistory.size() > 32) patchHistory.erase(patchHistory.begin());
    historyIndex = (int) patchHistory.size() - 1;
}

void SonaraAudioProcessor::generatePatch(const juce::String& p)
{
    stopPreview(); referenceMelodyPreview=false;
    const auto seed = (uint64_t) p.hashCode64()
                    ^ sessionSalt
                    ^ (++generationCounter * 0xd6e8feb86659fd93ULL)
                    ^ 0x534f554e445f444eULL;
    auto d = generator.generate(p, seed, [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = s; });
    d.name = "AI • " + p.substring(0, 26);
    setPatchWithHistory(d);
    generationStatus = "Sound ready • unique SoundDNA";
}

void SonaraAudioProcessor::mutatePatch()
{
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter), .45f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = s; });
    setPatchWithHistory(d); generationStatus = "Mutation ready";
}

void SonaraAudioProcessor::generateSimilarPatch()
{
    const auto source = engine.patch();
    const auto seed = source.seed + (++generationCounter * 0x517cc1b727220a95ULL);
    auto d = generator.mutate(source, seed, .18f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = "Similar: " + s; });
    d.sourcePrompt = source.sourcePrompt;
    setPatchWithHistory(d); generationStatus = "Similar variation ready";
}

void SonaraAudioProcessor::generateVariation(int index)
{
    const float amount = .12f + .12f * (float) juce::jlimit(1, 4, index);
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter * 0x94d049bb133111ebULL), amount, locks);
    d.name = "Variation " + juce::String(index);
    setPatchWithHistory(d); generationProgress.store(1.f); generationStatus = "Variation " + juce::String(index) + " ready";
}

void SonaraAudioProcessor::randomizePatch()
{
    const auto sourcePrompt = engine.patch().sourcePrompt.isNotEmpty() ? engine.patch().sourcePrompt : "experimental wide synth";
    generatePatch(sourcePrompt + " randomized texture");
}

void SonaraAudioProcessor::undoPatch(){ if(historyIndex>0){ --historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Undo"; } }
void SonaraAudioProcessor::redoPatch(){ if(historyIndex+1<(int)patchHistory.size()){ ++historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Redo"; } }
void SonaraAudioProcessor::captureA(){ patchA=engine.patch();hasA=true;generationStatus="A captured"; }
void SonaraAudioProcessor::captureB(){ patchB=engine.patch();hasB=true;generationStatus="B captured"; }
void SonaraAudioProcessor::recallA(){ if(hasA){ engine.setPatch(patchA);generationStatus="A recalled"; } }
void SonaraAudioProcessor::recallB(){ if(hasB){ engine.setPatch(patchB);generationStatus="B recalled"; } }

void SonaraAudioProcessor::generateTrack(const juce::String& prompt)
{
    stopPreview(); stopSongPreview();
    generationProgress.store(.05f); generationStatus = "Understanding full-track prompt";

    const uint64_t seed = (uint64_t) prompt.hashCode64()
                        ^ sessionSalt
                        ^ (++generationCounter * 0x9e3779b97f4a7c15ULL)
                        ^ 0x534f4e475f4d454cULL;
    lastSongSeed.store(seed, std::memory_order_relaxed);
    auto made = std::make_shared<sonara::SongArrangement>();
    made->generate(prompt, previewBpm, seed);
    generationProgress.store(.62f); generationStatus = "Loading generated SoundDNA into arrangement lanes";

    const auto& lanes = made->getLanes();
    for (int i = 0; i < musicalLaneCount; ++i)
    {
        const int laneIndex = firstMusicalLane + i;
        if (juce::isPositiveAndBelow(laneIndex, (int)lanes.size())) songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);
    }
    // Make the generated lead immediately playable from the instrument page too.
    if (lanes.size() > 8) setPatchWithHistory(lanes[8].sound);

    previewBpm = made->getBpm();
    std::atomic_store_explicit(&arrangement, std::shared_ptr<const sonara::SongArrangement>(made), std::memory_order_release);
    selectedLane.store(lanes.size() > 8 ? 8 : 0);
    generationProgress.store(1.f); generationStatus = "Full track ready • fresh drums + harmony + bass + synths + melody + FX";
}

void SonaraAudioProcessor::regenerateDrums(const juce::String& prompt)
{
    stopSongPreview();
    generationProgress.store(.08f);
    generationStatus = "Understanding drum prompt only";

    const uint64_t seed = (uint64_t) prompt.hashCode64()
                        ^ sessionSalt
                        ^ (++generationCounter * 0xa24baed4963ee407ULL)
                        ^ 0x4452554d535f4f4eULL;

    auto fresh = std::make_shared<sonara::SongArrangement>();
    fresh->generate(prompt + " drums only tight punchy fills transitions", previewBpm, seed);

    auto current = arrangementSnapshot();
    auto updated = current
        ? std::make_shared<sonara::SongArrangement>(*current)
        : std::make_shared<sonara::SongArrangement>(*fresh);

    auto& dst = updated->editLanes();
    const auto& src = fresh->getLanes();
    const int drumLanes = juce::jmin(4, juce::jmin((int)dst.size(), (int)src.size()));
    for (int i = 0; i < drumLanes; ++i)
        dst[(size_t)i] = src[(size_t)i];

    std::atomic_store_explicit(&arrangement,
        std::shared_ptr<const sonara::SongArrangement>(updated),
        std::memory_order_release);
    selectedLane.store(0);
    generationProgress.store(1.f);
    generationStatus = current
        ? "Drums regenerated only • melody, chords and bass preserved"
        : "Drums ready • base arrangement created because no song existed";
}

void SonaraAudioProcessor::startSongPreview()
{
    auto a = arrangementSnapshot();
    if (!a || a->isEmpty()){ generationStatus = "Generate a full track first"; return; }
    stopPreview();
    songSample.store(0); drumSynth.reset();
    for (auto& e : songEngines) e.allNotesOff();
    songPlaying.store(true); generationStatus = "Playing generated arrangement";
}

void SonaraAudioProcessor::stopSongPreview()
{
    songPlaying.store(false); songSample.store(0); drumSynth.reset();
    for (auto& e : songEngines) e.allNotesOff();
}

double SonaraAudioProcessor::songPosition01() const noexcept
{
    auto a = arrangementSnapshot(); if (!a) return 0.0;
    const double samplesPerBeat = previewSampleRate * 60.0 / a->getBpm();
    const double total = juce::jmax(1.0, a->getTotalBeats() * samplesPerBeat);
    return juce::jlimit(0.0, 1.0, (double)songSample.load() / total);
}

void SonaraAudioProcessor::injectSongLaneMidi(const sonara::ArrangementLane& lane, juce::MidiBuffer& dest, int64_t startSample, int numSamples, double bpm) noexcept
{
    dest.clear();
    const double spb = previewSampleRate * 60.0 / bpm;
    const double startBeat = (double)startSample / spb;
    const double endBeat = (double)(startSample + numSamples) / spb;
    auto it = std::lower_bound(lane.notes.begin(), lane.notes.end(), startBeat - 4.2,
                               [](const sonara::ArrangementNote& n, double beat){ return n.beat < beat; });
    for (; it != lane.notes.end() && it->beat <= endBeat; ++it)
    {
        const double onS = it->beat * spb, offS = (it->beat + it->length) * spb;
        if (onS >= startSample && onS < startSample + numSamples)
            dest.addEvent(juce::MidiMessage::noteOn(lane.midiChannel, it->note, (juce::uint8)it->velocity), (int)(onS - startSample));
        if (offS >= startSample && offS < startSample + numSamples)
            dest.addEvent(juce::MidiMessage::noteOff(lane.midiChannel, it->note), (int)(offS - startSample));
    }
}

int SonaraAudioProcessor::collectDrumTriggers(const sonara::SongArrangement& a, int64_t startSample, int numSamples) noexcept
{
    const auto& lanes = a.getLanes();
    const double spb = previewSampleRate * 60.0 / a.getBpm();
    const double startBeat = (double)startSample / spb;
    const double endBeat = (double)(startSample + numSamples) / spb;
    int count = 0;

    for (int laneIndex = 0; laneIndex < juce::jmin(firstMusicalLane, (int)lanes.size()); ++laneIndex)
    {
        const auto& lane = lanes[(size_t)laneIndex];
        auto it = std::lower_bound(lane.notes.begin(), lane.notes.end(), startBeat,
                                   [](const sonara::ArrangementNote& n, double beat){ return n.beat < beat; });
        for (; it != lane.notes.end() && it->beat <= endBeat && count < (int)drumTriggers.size(); ++it)
        {
            const int offset = juce::jlimit(0, numSamples - 1, (int)std::llround(it->beat * spb - startSample));
            drumTriggers[(size_t)count++] = { offset, it->note, it->velocity / 127.f };
        }
    }

    std::sort(drumTriggers.begin(), drumTriggers.begin() + count,
              [](const sonara::DrumTrigger& x, const sonara::DrumTrigger& y){ return x.sampleOffset < y.sampleOffset; });
    return count;
}

void SonaraAudioProcessor::renderSongBlock(juce::AudioBuffer<float>& out, int numSamples)
{
    auto a = arrangementSnapshot();
    if (!a || numSamples <= 0){ songPlaying.store(false); return; }

    const auto& lanes = a->getLanes();
    const int64_t start = songSample.load();
    const double spb = previewSampleRate * 60.0 / a->getBpm();
    const int64_t total = (int64_t) std::llround(a->getTotalBeats() * spb);

    out.clear();
    for (int i = 0; i < musicalLaneCount; ++i)
    {
        const int laneIndex = firstMusicalLane + i;
        if (!juce::isPositiveAndBelow(laneIndex, (int)lanes.size())) continue;
        auto& midi = songMidi[(size_t)i]; auto& scratch = songScratch[(size_t)i];
        injectSongLaneMidi(lanes[(size_t)laneIndex], midi, start, numSamples, a->getBpm());
        scratch.clear(); songEngines[(size_t)i].render(scratch, midi);
        const float gain = laneIndex == 4 ? .86f : laneIndex == 8 ? .78f : laneIndex == 10 ? .45f : .60f;
        for (int c = 0; c < out.getNumChannels(); ++c) out.addFrom(c, 0, scratch, c, 0, numSamples, gain);
    }

    const int drumCount = collectDrumTriggers(*a, start, numSamples);
    drumSynth.render(out, drumTriggers.data(), drumCount);

    for (int c = 0; c < out.getNumChannels(); ++c)
        for (int i = 0; i < numSamples; ++i)
            out.setSample(c, i, std::tanh(out.getSample(c, i) * .76f));

    const int64_t next = start + numSamples;
    songSample.store(next);
    if (next >= total)
    {
        songPlaying.store(false); songSample.store(total);
        for (auto& e : songEngines) e.allNotesOff();
    }
}

bool SonaraAudioProcessor::writeArrangementMidiFile(const juce::File& destination) const
{
    auto a = arrangementSnapshot(); return a ? a->writeMidiFile(destination) : false;
}

bool SonaraAudioProcessor::writeSelectedLaneMidiFile(const juce::File& destination) const
{
    auto a = arrangementSnapshot(); if (!a) return false;
    const auto& lanes = a->getLanes(); const int laneIndex = selectedLane.load();
    if (!juce::isPositiveAndBelow(laneIndex, (int)lanes.size())) return false;
    const auto& lane = lanes[(size_t)laneIndex];

    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    auto tempo = juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0 / a->getBpm())); tempo.setTimeStamp(0); seq.addEvent(tempo);
    auto name = juce::MidiMessage::textMetaEvent(3, lane.name); name.setTimeStamp(0); seq.addEvent(name);
    for (const auto& n : lane.notes)
    {
        auto on=juce::MidiMessage::noteOn(lane.midiChannel,n.note,(juce::uint8)n.velocity); on.setTimeStamp(n.beat*960.0); seq.addEvent(on);
        auto off=juce::MidiMessage::noteOff(lane.midiChannel,n.note); off.setTimeStamp((n.beat+n.length)*960.0); seq.addEvent(off);
    }
    seq.updateMatchedPairs(); mf.addTrack(seq); destination.deleteFile(); juce::FileOutputStream out(destination);
    return out.openedOk() && mf.writeTo(out);
}


bool SonaraAudioProcessor::analyseReferenceFile(const juce::File& file)
{
    stopPreview(); stopSongPreview(); generationProgress.store(.05f); generationStatus="Analyzing reference audio";
    auto result=referenceAnalyzer.analyseAudio(file);
    if(!result.valid()){generationProgress.store(0.f);generationStatus="Reference analysis failed";return false;}
    reference=std::move(result);referenceLoaded=true;referenceMelodyPreview=false;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);generationStatus="Reference ready • "+reference.keyName+" • "+juce::String(reference.melody.size())+" melody notes";return true;
}

bool SonaraAudioProcessor::importMidiFile(const juce::File& file)
{
    stopPreview(); stopSongPreview(); generationProgress.store(.1f); generationStatus="Importing MIDI";
    auto result=referenceAnalyzer.importMidi(file);
    if(result.melody.empty()){generationProgress.store(0.f);generationStatus="MIDI import failed";return false;}
    reference=std::move(result);referenceLoaded=true;referenceMelodyPreview=true;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);generationStatus="MIDI imported • ready to RESOUND";return true;
}

void SonaraAudioProcessor::resoundReference(const juce::String& prompt)
{
    if(!referenceLoaded||reference.melody.empty()){generationStatus="Load a reference audio or MIDI first";return;}
    generatePatch(prompt+" resound instrument clean expressive");referenceMelodyPreview=true;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);startPreview();generationStatus="RESOUND • generated SoundDNA playing extracted melody";
}

void SonaraAudioProcessor::rebuildInstrumentalFromReference(const juce::String& prompt)
{
    if(!referenceLoaded||reference.melody.empty()){generationStatus="Load a reference audio or MIDI first";return;}
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generateTrack(prompt+" "+reference.keyName+" rebuild instrumental from reference melody");
    auto current=arrangementSnapshot();if(!current)return;auto rebuilt=std::make_shared<sonara::SongArrangement>(*current);auto& lanes=rebuilt->editLanes();
    int leadIndex=-1;for(int i=0;i<(int)lanes.size();++i)if(lanes[(size_t)i].name=="LEAD"){leadIndex=i;break;}if(leadIndex<0)return;
    auto& lead=lanes[(size_t)leadIndex];lead.notes.clear();const double loop=juce::jmax(4.0,reference.melodyBeats());const double total=rebuilt->getTotalBeats();
    for(double offset=0.0;offset<total;offset+=loop)for(const auto& n:reference.melody){const double beat=offset+n.beat;if(beat>=total)break;lead.notes.push_back({n.midiNote,n.velocity,beat,juce::jmin(n.length,total-beat)});}std::sort(lead.notes.begin(),lead.notes.end(),[](const sonara::ArrangementNote&a,const sonara::ArrangementNote&b){return a.beat<b.beat;});
    lead.sound=generator.generate(prompt+" emotional lead resounded from reference",(uint64_t)prompt.hashCode64()^(++generationCounter*0x94d049bb133111ebULL));
    for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(rebuilt),std::memory_order_release);selectedLane.store(leadIndex);generationProgress.store(1.f);generationStatus="Reference instrumental rebuilt • new drums/bass/chords/synths + extracted melody";
}

bool SonaraAudioProcessor::writeReferenceMidiFile(const juce::File& file) const
{
    return referenceLoaded&&referenceAnalyzer.writeMelodyMidi(reference,file);
}

bool SonaraAudioProcessor::saveSound(const juce::File& file) const
{
    auto xml=engine.patch().toValueTree().createXml();return xml&&file.replaceWithText(xml->toString());
}

bool SonaraAudioProcessor::loadSound(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);if(!xml)return false;auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||tree.getType().toString()!="SoundDNA")return false;setPatchWithHistory(sonara::SoundDNA::fromValueTree(tree));referenceMelodyPreview=false;generationStatus="Sound loaded";return true;
}

bool SonaraAudioProcessor::saveProject(const juce::File& file) const
{
    juce::ValueTree root("SONARA_PROJECT");root.setProperty("schema",1,nullptr);root.setProperty("bpm",previewBpm,nullptr);root.addChild(engine.patch().toValueTree(),-1,nullptr);root.addChild(locks.toValueTree(),-1,nullptr);if(auto a=arrangementSnapshot())root.addChild(a->toValueTree(),-1,nullptr);auto xml=root.createXml();return xml&&file.replaceWithText(xml->toString());
}

bool SonaraAudioProcessor::loadProject(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);if(!xml)return false;auto root=juce::ValueTree::fromXml(*xml);if(!root.isValid()||root.getType().toString()!="SONARA_PROJECT")return false;auto dna=root.getChildWithName("SoundDNA");if(dna.isValid())setPatchWithHistory(sonara::SoundDNA::fromValueTree(dna));auto lockTree=root.getChildWithName("MUTATION_LOCKS");if(lockTree.isValid())locks=sonara::MutationLocks::fromValueTree(lockTree);previewBpm=juce::jlimit(60.0,200.0,(double)root.getProperty("bpm",128.0));auto arr=root.getChildWithName("SONARA_ARRANGEMENT");if(arr.isValid()){auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));const auto& lanes=made->getLanes();for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);}generationStatus="Project loaded";generationProgress.store(1.f);return true;
}

bool SonaraAudioProcessor::exportFullMix(const juce::File& file)
{
    auto a=arrangementSnapshot();if(!a){generationStatus="Generate a track first";return false;}generationStatus="Rendering 24-bit full mix";generationProgress.store(.01f);const bool ok=audioExporter.renderFullMix(*a,file,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationStatus=ok?"24-bit full mix ready":"Full mix export failed";return ok;
}

bool SonaraAudioProcessor::exportSelectedLaneAudio(const juce::File& file)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderSelectedLane(*a,selectedLane.load(),file,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationStatus=ok?"Selected lane WAV ready":"Lane export failed";return ok;
}

bool SonaraAudioProcessor::exportAllStems(const juce::File& directory)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderAllStems(*a,directory,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationStatus=ok?"All 24-bit stems ready":"Stem export failed";return ok;
}

void SonaraAudioProcessor::setMacro(Macro macro, float normalized)
{
    const float x = juce::jlimit(0.f,1.f,normalized); auto d = engine.patch();
    switch(macro)
    {
        case Macro::brightness: d.macroBrightness=x; d.cutoff=juce::jlimit(30.f,22000.f,80.f*std::pow(250.f,x)); break;
        case Macro::movement: d.macroMovement=x; d.lfoRate=.08f+7.92f*x*x; d.lfoCutoff=(x-.5f)*1.5f; d.lfoMorphA=(x-.5f)*1.4f; d.lfoMorphB=-(x-.5f)*1.2f; break;
        case Macro::space: d.macroSpace=x; d.chorus=juce::jlimit(0.f,1.f,x*.75f); d.reverb=juce::jlimit(0.f,1.f,x*.85f); d.delay=juce::jlimit(0.f,1.f,juce::jmax(0.f,(x-.18f)*.9f)); break;
        case Macro::impact: d.macroImpact=x; d.drive=juce::jlimit(0.f,1.f,.04f+x*.62f); d.subLevel=juce::jlimit(0.f,1.f,x*.52f); break;
    }
    engine.setPatch(d); generationStatus = "Macro adjusted";
}

juce::String SonaraAudioProcessor::exportProjectForCyanoryx() const
{
    auto a=arrangementSnapshot();
    return cyanoryx.serializeInterchange(engine.patch(), a ? a->toValueTree() : juce::ValueTree{}, previewBpm, referenceLoaded ? reference.keyName : juce::String("Unknown"));
}

bool SonaraAudioProcessor::importPatchFromCyanoryx(const juce::String& payload)
{
    sonara::SoundDNA imported; if(!cyanoryx.deserializePatch(payload,imported))return false;
    setPatchWithHistory(imported); generationProgress.store(1.f); generationStatus="Cyanoryx patch received"; return true;
}

void SonaraAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("SONARA_STATE"); state.setProperty("schema",4,nullptr); state.setProperty("generationCounter",juce::String(generationCounter),nullptr); state.setProperty("bpm",previewBpm,nullptr);
    state.addChild(engine.patch().toValueTree(),-1,nullptr); state.addChild(locks.toValueTree(),-1,nullptr); if(auto a=arrangementSnapshot()) state.addChild(a->toValueTree(),-1,nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,dest);
}

void SonaraAudioProcessor::setStateInformation(const void* data,int bytes)
{
    if(data==nullptr||bytes<=0)return;
    if(auto xml=getXmlFromBinary(data,bytes))
    {
        const auto state=juce::ValueTree::fromXml(*xml); if(!state.isValid())return;
        if(state.getType().toString()=="SoundDNA"){setPatchWithHistory(sonara::SoundDNA::fromValueTree(state));locks={};generationCounter=1;generationStatus="Legacy preset restored";return;}
        if(state.getType().toString()!="SONARA_STATE")return;
        const auto dna=state.getChildWithName("SoundDNA"); if(!dna.isValid())return;
        engine.setPatch(sonara::SoundDNA::fromValueTree(dna));
        const auto lockState=state.getChildWithName("MUTATION_LOCKS"); locks=lockState.isValid()?sonara::MutationLocks::fromValueTree(lockState):sonara::MutationLocks{};
        generationCounter=(uint64_t)juce::jmax<juce::int64>(1,state.getProperty("generationCounter","1").toString().getLargeIntValue());
        previewBpm=juce::jlimit(60.0,200.0,(double)state.getProperty("bpm",128.0));
        auto arr=state.getChildWithName("SONARA_ARRANGEMENT");if(arr.isValid()){auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));const auto& lanes=made->getLanes();for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);}
        patchHistory.clear();patchHistory.push_back(engine.patch());historyIndex=0;generationProgress.store(1.f);generationStatus="Session restored";
    }
}

juce::AudioProcessorEditor* SonaraAudioProcessor::createEditor(){return new SonaraAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SonaraAudioProcessor();}