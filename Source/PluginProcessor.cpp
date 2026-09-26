#include "PluginProcessor.h"
#ifndef SONARA_HEADLESS_TEST
#include "PluginEditor.h"
#endif
#include <algorithm>
#include <cmath>

namespace {
uint64_t scrambleSongSeed(uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

const sonara::ArrangementLane* laneNamed(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& lane:a.getLanes())if(lane.name==name)return &lane;
    return nullptr;
}

float melodySimilarity(const sonara::SongArrangement& a,const sonara::SongArrangement& b)
{
    const auto* x=laneNamed(a,"LEAD");
    const auto* y=laneNamed(b,"LEAD");
    if(x==nullptr||y==nullptr||x->notes.empty()||y->notes.empty())return 0.f;

    const int n=juce::jmin(64,juce::jmin((int)x->notes.size(),(int)y->notes.size()));
    if(n<8)return 0.f;

    float score=0.f,weight=0.f;
    for(int i=0;i<n;++i)
    {
        const auto& aN=x->notes[(size_t)i];
        const auto& bN=y->notes[(size_t)i];

        weight+=1.f;
        if((aN.note%12)==(bN.note%12))score+=.52f;
        if(std::abs(aN.beat-bN.beat)<.08)score+=.28f;
        if(std::abs(aN.length-bN.length)<.08)score+=.20f;

        if(i>0)
        {
            const int aInt=aN.note-x->notes[(size_t)i-1].note;
            const int bInt=bN.note-y->notes[(size_t)i-1].note;
            weight+=.45f;
            if(aInt==bInt)score+=.32f;
            else if((aInt>0)==(bInt>0)&&aInt!=0&&bInt!=0)score+=.13f;
        }
    }

    const float countRatio=(float)juce::jmin(x->notes.size(),y->notes.size())
                          /(float)juce::jmax<size_t>(1,juce::jmax(x->notes.size(),y->notes.size()));
    const float normalized=weight>0.f?score/weight:0.f;
    return juce::jlimit(0.f,1.f,normalized*.82f+countRatio*.18f);
}

std::vector<int> melodyFingerprint(const sonara::SongArrangement& a)
{
    std::vector<int> fp;
    const auto* lead=laneNamed(a,"LEAD");
    if(lead==nullptr||lead->notes.empty())return fp;
    fp.reserve(96*7);

    const int limit=juce::jmin(96,(int)lead->notes.size());
    int prev=lead->notes.front().note;
    double prevBeat=lead->notes.front().beat;

    for(int i=0;i<limit;++i)
    {
        const auto& n=lead->notes[(size_t)i];
        const int interval=i==0?0:juce::jlimit(-12,12,n.note-prev);
        const int contour=interval>0?1:(interval<0?-1:0);
        const int pitchClass=((n.note%12)+12)%12;
        const int octave=n.note/12;
        const int onset16=(int)std::llround(std::fmod(juce::jmax(0.0,n.beat),16.0)*4.0);
        const int gap8=i==0?0:juce::jlimit(0,64,(int)std::llround((n.beat-prevBeat)*8.0));
        const int length8=juce::jlimit(1,32,(int)std::llround(n.length*8.0));

        fp.push_back(pitchClass);
        fp.push_back(interval+12);
        fp.push_back(contour+1);
        fp.push_back(octave);
        fp.push_back(onset16);
        fp.push_back(gap8);
        fp.push_back(length8);

        prev=n.note;
        prevBeat=n.beat;
    }
    fp.push_back((int)lead->notes.size());
    return fp;
}

float fingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.empty()||b.empty())return 0.f;
    constexpr int stride=7;
    const int aNotes=((int)a.size()-1)/stride;
    const int bNotes=((int)b.size()-1)/stride;
    const int n=juce::jmin(aNotes,bNotes);
    if(n<8)return 0.f;

    float score=0.f,maxScore=0.f;
    for(int i=0;i<n;++i)
    {
        const int ai=i*stride,bi=i*stride;
        maxScore+=1.f;
        if(a[(size_t)ai]==b[(size_t)bi])score+=.22f;
        if(a[(size_t)ai+1]==b[(size_t)bi+1])score+=.22f;
        if(a[(size_t)ai+2]==b[(size_t)bi+2])score+=.10f;
        if(a[(size_t)ai+3]==b[(size_t)bi+3])score+=.08f;
        if(std::abs(a[(size_t)ai+4]-b[(size_t)bi+4])<=1)score+=.16f;
        if(std::abs(a[(size_t)ai+5]-b[(size_t)bi+5])<=1)score+=.12f;
        if(std::abs(a[(size_t)ai+6]-b[(size_t)bi+6])<=1)score+=.10f;
    }

    const float countRatio=(float)juce::jmin(aNotes,bNotes)/(float)juce::jmax(1,juce::jmax(aNotes,bNotes));
    return juce::jlimit(0.f,1.f,(maxScore>0.f?score/maxScore:0.f)*.88f+countRatio*.12f);
}

juce::ValueTree makeLaneMixTree(const SonaraAudioProcessor& p)
{
    juce::ValueTree root("LANE_MIX");
    root.setProperty("schema",1,nullptr);
    for(int i=0;i<12;++i)
    {
        const auto m=p.getLaneMix(i);
        juce::ValueTree lane("MIX");
        lane.setProperty("index",i,nullptr);
        lane.setProperty("level",m.level,nullptr);
        lane.setProperty("pan",m.pan,nullptr);
        lane.setProperty("width",m.width,nullptr);
        lane.setProperty("fx",m.fxSend,nullptr);
        root.addChild(lane,-1,nullptr);
    }
    return root;
}

void applyLaneMixTree(SonaraAudioProcessor& p,const juce::ValueTree& root)
{
    if(!root.isValid()||root.getType().toString()!="LANE_MIX")return;
    for(int i=0;i<root.getNumChildren();++i)
    {
        const auto lane=root.getChild(i);
        const int index=juce::jlimit(0,11,(int)lane.getProperty("index",i));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::level,(float)lane.getProperty("level",1.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::pan,(float)lane.getProperty("pan",0.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::width,(float)lane.getProperty("width",1.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::fxSend,(float)lane.getProperty("fx",1.f));
    }
}
}

SonaraAudioProcessor::SonaraAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    sessionSalt = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks());
    for(auto& e:songEngines)e.setLowCpuMode(true);
    // Lane order after drums: BASS, SUB, CHORDS, PLUCK, PAD, LEAD, COUNTER, FX.
    static constexpr int voiceBudget[musicalLaneCount]={2,1,4,2,4,4,2,1};
    for(int i=0;i<musicalLaneCount;++i)songEngines[(size_t)i].setVoiceLimit(voiceBudget[i]);
    for(int i=0;i<12;++i){laneMixLevel[(size_t)i].store(1.f);laneMixPan[(size_t)i].store(0.f);laneMixWidth[(size_t)i].store(1.f);laneMixFx[(size_t)i].store(1.f);}
    patchHistory.push_back(engine.patch());
    historyIndex = 0;
}

void SonaraAudioProcessor::prepareToPlay(double sr, int bs)
{
    previewSampleRate = juce::jmax(8000.0, sr);
    maximumBlockSize = juce::jmax(8192, bs);
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * 16.0);

    engine.prepare(sr, maximumBlockSize, getTotalNumOutputChannels());
    for (auto& e : songEngines) e.prepare(sr, maximumBlockSize, getTotalNumOutputChannels());
    drumSynth.prepare(sr);
    songReverb.reset();
    juce::Reverb::Parameters rp;
    rp.roomSize=.31f;rp.damping=.52f;rp.wetLevel=.22f;rp.dryLevel=0.f;rp.width=.82f;
    songReverb.setParameters(rp);

    const int channels = juce::jmax(1, getTotalNumOutputChannels());
    for (auto& b : songScratch)
    {
        b.setSize(channels, maximumBlockSize, false, false, true);
        b.clear();
    }
    songFxBus.setSize(channels,maximumBlockSize,false,false,true);
    songFxBus.clear();
    for (auto& m : songMidi) m.ensureSize(16384);
    for (auto& x : laneHpX) x.fill(0.f);
    for (auto& y : laneHpY) y.fill(0.f);
    for (auto& x : laneLpState) x.fill(0.f);
    masterHpX.fill(0.f); masterHpY.fill(0.f);
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
    stopPreview();stopSongPreview();
    generationProgress.store(.03f);
    generationStatus="Creating a completely new song from scratch";

    const auto previous=arrangementSnapshot();
    const auto previousFingerprint=previous?melodyFingerprint(*previous):std::vector<int>{};
    std::shared_ptr<sonara::SongArrangement> made;
    std::vector<int> acceptedFingerprint;
    uint64_t seed=0;
    float maxSimilarity=0.f;

    for(int attempt=0;attempt<12;++attempt)
    {
        const uint64_t entropy=static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                             ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks())
                             ^ ((uint64_t)(attempt+1)*0xd1342543de82ef95ULL);
        seed=scrambleSongSeed((uint64_t)prompt.hashCode64()
                           ^ sessionSalt
                           ^ (++generationCounter*0x9e3779b97f4a7c15ULL)
                           ^ entropy
                           ^ 0x534f4e475f465245ULL);

        auto candidate=std::make_shared<sonara::SongArrangement>();
        candidate->generate(prompt,previewBpm,seed);
        auto fingerprint=melodyFingerprint(*candidate);

        maxSimilarity=previousFingerprint.empty()?0.f:fingerprintSimilarity(previousFingerprint,fingerprint);
        for(const auto& historic:melodyHistory)
            maxSimilarity=juce::jmax(maxSimilarity,fingerprintSimilarity(historic,fingerprint));

        generationProgress.store(.10f+.04f*attempt);

        // Reject anything that still looks like a recent lead at the structural level.
        if(!fingerprint.empty()&&(maxSimilarity<.31f||attempt==11))
        {
            made=std::move(candidate);
            acceptedFingerprint=std::move(fingerprint);
            break;
        }

        generationStatus="Melody fingerprint too similar • rerolling composition "+juce::String(attempt+2);
    }

    if(!made)
    {
        generationProgress.store(0.f);
        generationStatus="Song generation failed to create a valid melody";
        return;
    }

    if(!acceptedFingerprint.empty())
    {
        melodyHistory.push_back(acceptedFingerprint);
        while(melodyHistory.size()>6)melodyHistory.pop_front();
    }

    lastSongSeed.store(seed,std::memory_order_relaxed);
    lastMelodyNovelty.store(juce::jlimit(0.f,1.f,1.f-maxSimilarity),std::memory_order_relaxed);
    generationProgress.store(.66f);
    generationStatus="Loading new SoundDNA palette into fresh arrangement";

    const auto& lanes=made->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);
    for(int i=0;i<musicalLaneCount;++i)
    {
        const int laneIndex=firstMusicalLane+i;
        if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))
        {
            songEngines[(size_t)i].allNotesOff();
            songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);
        }
    }

    previewBpm=made->getBpm();
    std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);
    selectedLane.store(lanes.size()>9?9:0);
    generationProgress.store(1.f);
    generationStatus="NEW SONG READY • melody novelty "+juce::String((1.f-maxSimilarity)*100.f,0)
                   +"% • drums + bass + sub + harmony + melody + FX regenerated";
}


void SonaraAudioProcessor::randomizeEverything(const juce::String& prompt)
{
    locks={};
    auto q=prompt.trim();
    auto lower=q.toLowerCase();

    const bool hasBpm=lower.contains(" bpm");
    const bool hasKey=lower.contains(" minor")||lower.contains(" major");

    auto& rng=juce::Random::getSystemRandom();
    static constexpr int bpms[]={122,124,126,128,130,132,136,140,150,174};
    static constexpr const char* keys[]={"C minor","D minor","E minor","F minor","G minor","A minor","C major","D major","G major","A major"};

    if(!hasBpm)q+=", "+juce::String(bpms[rng.nextInt((int)(sizeof(bpms)/sizeof(bpms[0])))])+" BPM";
    if(!hasKey)q+=", "+juce::String(keys[rng.nextInt((int)(sizeof(keys)/sizeof(keys[0])))]);
    q+=", completely fresh composition, new harmony, new groove, new melody contour, new drum kit, new bass and sub";

    const juce::String soundBrief=q+" • completely new standalone signature synth, unique oscillator character, polished transient, controlled low end";
    generatePatch(soundBrief);
    generateTrack(q);
    generationStatus="EVERYTHING RANDOMIZED • new song + melody + structure + drums + bass + sub + SoundDNA";
}


juce::String SonaraAudioProcessor::makeSurprisePrompt()
{
    static constexpr const char* genres[]={
        "emotional progressive house","future rave","melodic EDM pop","dark tech house",
        "drum and bass","electro house","cinematic EDM","tropical progressive house",
        "festival trance EDM","experimental melodic bass"
    };
    static constexpr const char* moods[]={
        "euphoric and emotional","dark and mysterious","uplifting and nostalgic","aggressive and futuristic",
        "dreamy and cinematic","melancholic but energetic","warm and hopeful","tense then explosive"
    };
    static constexpr const char* keys[]={
        "F minor","D minor","A minor","C minor","G minor","E minor","A major","D major","G major","C major"
    };
    static constexpr int bpms[]={122,124,126,128,130,132,138,140,150,174};
    static constexpr const char* hooks[]={
        "huge memorable lead hook","short addictive synth hook","anthemic octave melody","syncopated pluck hook",
        "wide emotional supersaw theme","minimal dark vocal-like synth hook","cinematic call-and-response melody"
    };
    static constexpr const char* arrangements[]={
        "short intro, verse, build, huge drop, breakdown, chorus, second build, final hook",
        "fast intro, early drop, breakdown, melodic chorus, harder final hook",
        "atmospheric intro, restrained verse, rising build, euphoric drop, emotional breakdown, massive final hook",
        "club intro, groove verse, tension build, punchy drop, hook chorus, stripped break, explosive ending"
    };

    auto& r=juce::Random::getSystemRandom();
    const auto pick=[&](const auto& arr)->juce::String{
        constexpr int n=(int)(sizeof(arr)/sizeof(arr[0]));
        return juce::String(arr[r.nextInt(n)]);
    };

    const juce::String genre=pick(genres);
    const juce::String mood=pick(moods);
    const juce::String key=pick(keys);
    const int bpm=bpms[r.nextInt((int)(sizeof(bpms)/sizeof(bpms[0])))];
    const juce::String hook=pick(hooks);
    const juce::String arrangement=pick(arrangements);

    return genre+", "+juce::String(bpm)+" BPM, "+key+", "+mood+", "+hook+
           ", completely fresh chord progression and bass groove, distinct drums, "+arrangement;
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
    if(dst.size()>=4) drumSynth.configureKit(dst[0].sound,dst[1].sound,dst[2].sound,dst[3].sound);

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
    startSongPreviewAtBar(0);
}

void SonaraAudioProcessor::startSongPreviewAtBar(int bar)
{
    auto a=arrangementSnapshot();
    if(!a||a->isEmpty()){generationStatus="Generate a full track first";return;}

    stopPreview();
    const auto& lanes=a->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);

    const int safeBar=juce::jlimit(0,juce::jmax(0,a->getBars()-1),bar);
    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t start=(int64_t)std::llround((double)safeBar*sonara::SongArrangement::beatsPerBar*spb);

    drumSynth.reset();songReverb.reset();
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);
    for(auto& y:laneHpY)y.fill(0.f);
    for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    if(songFxBus.getNumSamples()>0)songFxBus.clear();
    songFadeRemaining.store(128,std::memory_order_release);

    songSample.store(start);
    songPlaying.store(true);

    juce::String section="SONG";
    for(const auto& s:a->getSections())
        if(safeBar>=s.startBar&&safeBar<s.startBar+s.bars){section=s.name;break;}
    generationStatus="Playing "+section+" • bar "+juce::String(safeBar+1);
}

void SonaraAudioProcessor::pauseSongPreview()
{
    if(!songPlaying.exchange(false))return;
    drumSynth.reset();songReverb.reset();
    for(auto& e:songEngines)e.allNotesOff();
    songFadeRemaining.store(0,std::memory_order_release);
    generationStatus="Song preview paused • bar "+juce::String(currentSongBar()+1);
}

void SonaraAudioProcessor::resumeSongPreview()
{
    auto a=arrangementSnapshot();
    if(!a||a->isEmpty()){generationStatus="Generate a full track first";return;}

    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t total=(int64_t)std::llround(a->getTotalBeats()*spb);
    auto pos=songSample.load();
    if(pos<=0||pos>=total){startSongPreviewAtBar(0);return;}

    const auto& lanes=a->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);
    drumSynth.reset();songReverb.reset();
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);for(auto& y:laneHpY)y.fill(0.f);for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    songFadeRemaining.store(128,std::memory_order_release);
    songPlaying.store(true,std::memory_order_release);
    generationStatus="Song preview resumed • "+currentSectionName()+" • bar "+juce::String(currentSongBar()+1);
}

void SonaraAudioProcessor::stopSongPreview()
{
    songPlaying.store(false);songSample.store(0);drumSynth.reset();songReverb.reset();
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);
    for(auto& y:laneHpY)y.fill(0.f);
    for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    songFadeRemaining.store(0,std::memory_order_release);
}

double SonaraAudioProcessor::songPosition01() const noexcept
{
    auto a=arrangementSnapshot();if(!a)return 0.0;
    const double samplesPerBeat=previewSampleRate*60.0/a->getBpm();
    const double total=juce::jmax(1.0,a->getTotalBeats()*samplesPerBeat);
    return juce::jlimit(0.0,1.0,(double)songSample.load()/total);
}

int SonaraAudioProcessor::currentSongBar() const noexcept
{
    auto a=arrangementSnapshot();if(!a)return 0;
    return juce::jlimit(0,juce::jmax(0,a->getBars()-1),
        (int)std::floor(songPosition01()*a->getBars()));
}

juce::String SonaraAudioProcessor::currentSectionName() const
{
    auto a=arrangementSnapshot();if(!a)return {};
    const int bar=currentSongBar();
    for(const auto& s:a->getSections())
        if(bar>=s.startBar&&bar<s.startBar+s.bars)return s.name;
    return {};
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
        for (; it != lane.notes.end() && it->beat < endBeat && count < (int)drumTriggers.size(); ++it)
        {
            const int offset = juce::jlimit(0, numSamples - 1, (int)std::llround(it->beat * spb - startSample));
            drumTriggers[(size_t)count++] = { offset, it->note, it->velocity / 127.f };
        }
    }

    std::sort(drumTriggers.begin(), drumTriggers.begin() + count,
              [](const sonara::DrumTrigger& x, const sonara::DrumTrigger& y){ return x.sampleOffset < y.sampleOffset; });
    return count;
}

void SonaraAudioProcessor::renderSongBlock(juce::AudioBuffer<float>& out,int numSamples)
{
    auto a=arrangementSnapshot();
    if(!a||numSamples<=0){songPlaying.store(false);return;}

    const int renderSamples=juce::jmin(numSamples,maximumBlockSize);
    const auto& lanes=a->getLanes();
    const int64_t start=songSample.load();
    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t total=(int64_t)std::llround(a->getTotalBeats()*spb);

    out.clear();
    songFxBus.clear(0,renderSamples);

    // Lane order: BASS, SUB, CHORDS, PLUCK, PAD, LEAD, COUNTER, FX.
    static constexpr float laneGain[musicalLaneCount]={.52f,.34f,.30f,.27f,.22f,.50f,.23f,.16f};
    static constexpr float hpHz[musicalLaneCount]={28.f,18.f,120.f,125.f,160.f,120.f,150.f,110.f};
    static constexpr float fxSend[musicalLaneCount]={0.f,0.f,.14f,.10f,.18f,.12f,.08f,.15f};

    for(int i=0;i<musicalLaneCount;++i)
    {
        const int laneIndex=firstMusicalLane+i;
        if(!juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))continue;

        auto& midi=songMidi[(size_t)i];
        auto& scratch=songScratch[(size_t)i];
        injectSongLaneMidi(lanes[(size_t)laneIndex],midi,start,renderSamples,a->getBpm());

        const bool active=songEngines[(size_t)i].hasActiveVoices();
        if(midi.isEmpty()&&!active)continue;

        scratch.clear(0,renderSamples);

        // Non-owning view: render exactly the number of samples FL Studio asked for.
        // This avoids advancing 8192 samples per small host block and avoids allocation.
        juce::AudioBuffer<float> scratchView(scratch.getArrayOfWritePointers(),
                                             scratch.getNumChannels(),
                                             0,renderSamples);
        songEngines[(size_t)i].render(scratchView,midi);

        const float rc=1.f/(juce::MathConstants<float>::twoPi*hpHz[i]);
        const float dt=1.f/(float)previewSampleRate;
        const float hpAlpha=rc/(rc+dt);

        for(int ch=0;ch<scratchView.getNumChannels()&&ch<2;++ch)
        {
            auto* d=scratchView.getWritePointer(ch);
            float x1=laneHpX[(size_t)i][(size_t)ch];
            float y1=laneHpY[(size_t)i][(size_t)ch];
            for(int s=0;s<renderSamples;++s)
            {
                const float x=std::isfinite(d[s])?d[s]:0.f;
                const float y=hpAlpha*(y1+x-x1);
                x1=x;y1=y;d[s]=y;
            }
            laneHpX[(size_t)i][(size_t)ch]=x1;
            laneHpY[(size_t)i][(size_t)ch]=y1;
        }

        const auto mix=getLaneMix(laneIndex);

        // BASS low end stays near-mono.
        if(i==0&&scratchView.getNumChannels()>=2)
        {
            auto* l=scratchView.getWritePointer(0);
            auto* r=scratchView.getWritePointer(1);
            for(int s=0;s<renderSamples;++s)
            {
                const float mid=.5f*(l[s]+r[s]);
                l[s]=mid*.88f+l[s]*.12f;
                r[s]=mid*.88f+r[s]*.12f;
            }
        }

        // SUB is mono and low-passed. It never enters the shared reverb bus.
        if(i==1)
        {
            const float lpRc=1.f/(juce::MathConstants<float>::twoPi*125.f);
            const float lpA=dt/(lpRc+dt);
            auto* l=scratchView.getWritePointer(0);
            auto* r=scratchView.getNumChannels()>1?scratchView.getWritePointer(1):l;
            float state=laneLpState[(size_t)i][0];
            for(int s=0;s<renderSamples;++s)
            {
                const float mono=.5f*(l[s]+r[s]);
                state+=lpA*(mono-state);
                l[s]=state;
                r[s]=state;
            }
            laneLpState[(size_t)i][0]=state;
            laneLpState[(size_t)i][1]=state;
        }

        if(scratchView.getNumChannels()>=2)
        {
            auto* l=scratchView.getWritePointer(0);
            auto* r=scratchView.getWritePointer(1);
            const float requestedWidth=juce::jlimit(0.f,1.5f,mix.width);
            const float width=i==1?0.f:(i==0?juce::jmin(.25f,requestedWidth):requestedWidth);
            const float pan=juce::jlimit(-1.f,1.f,mix.pan);
            const float panL=pan>0.f?1.f-pan:1.f;
            const float panR=pan<0.f?1.f+pan:1.f;
            for(int s=0;s<renderSamples;++s)
            {
                const float mid=.5f*(l[s]+r[s]);
                const float side=.5f*(l[s]-r[s])*width;
                l[s]=(mid+side)*panL;
                r[s]=(mid-side)*panR;
            }
        }

        const float mixedGain=laneGain[i]*mix.level;
        const float sendGain=fxSend[i]*mix.fxSend;
        for(int ch=0;ch<out.getNumChannels();++ch)
        {
            out.addFrom(ch,0,scratchView,ch,0,renderSamples,mixedGain);
            if(sendGain>0.f)
                songFxBus.addFrom(ch,0,scratchView,ch,0,renderSamples,mixedGain*sendGain);
        }
    }

    const int drumCount=collectDrumTriggers(*a,start,renderSamples);
    drumSynth.render(out,drumTriggers.data(),drumCount);

    // One shared wet-only reverb bus; drums/BASS/SUB stay dry.
    if(songFxBus.getNumChannels()>=2)
        songReverb.processStereo(songFxBus.getWritePointer(0),songFxBus.getWritePointer(1),renderSamples);
    else if(songFxBus.getNumChannels()==1)
        songReverb.processMono(songFxBus.getWritePointer(0),renderSamples);
    for(int ch=0;ch<out.getNumChannels();++ch)
        out.addFrom(ch,0,songFxBus,ch,0,renderSamples,.72f);

    const float masterRc=1.f/(juce::MathConstants<float>::twoPi*24.f);
    const float masterDt=1.f/(float)previewSampleRate;
    const float masterAlpha=masterRc/(masterRc+masterDt);

    const int fadeStart=songFadeRemaining.load(std::memory_order_acquire);
    for(int ch=0;ch<out.getNumChannels()&&ch<2;++ch)
    {
        auto* d=out.getWritePointer(ch);
        float x1=masterHpX[(size_t)ch],y1=masterHpY[(size_t)ch];
        for(int s=0;s<renderSamples;++s)
        {
            const float x=std::isfinite(d[s])?d[s]:0.f;
            const float hp=masterAlpha*(y1+x-x1);
            x1=x;y1=hp;
            float y=juce::jlimit(-.92f,.92f,std::tanh(hp*.67f));
            if(fadeStart>s)
            {
                const int remaining=fadeStart-s;
                y*=juce::jlimit(0.f,1.f,(128.f-(float)remaining)/128.f);
            }
            d[s]=y;
        }
        masterHpX[(size_t)ch]=x1;masterHpY[(size_t)ch]=y1;
    }
    songFadeRemaining.store(juce::jmax(0,fadeStart-renderSamples),std::memory_order_release);

    const int64_t next=start+renderSamples;
    songSample.store(next);
    if(next>=total)
    {
        songPlaying.store(false);songSample.store(total);
        for(auto& e:songEngines)e.allNotesOff();
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
    juce::ValueTree root("SONARA_PROJECT");
    root.setProperty("schema",2,nullptr);
    root.setProperty("bpm",previewBpm,nullptr);
    root.addChild(engine.patch().toValueTree(),-1,nullptr);
    root.addChild(locks.toValueTree(),-1,nullptr);
    root.addChild(makeLaneMixTree(*this),-1,nullptr);
    if(auto a=arrangementSnapshot())root.addChild(a->toValueTree(),-1,nullptr);
    auto xml=root.createXml();
    return xml&&file.replaceWithText(xml->toString());
}
bool SonaraAudioProcessor::loadProject(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);
    if(!xml)return false;
    auto root=juce::ValueTree::fromXml(*xml);
    if(!root.isValid()||root.getType().toString()!="SONARA_PROJECT")return false;

    auto dna=root.getChildWithName("SoundDNA");
    if(dna.isValid())setPatchWithHistory(sonara::SoundDNA::fromValueTree(dna));
    auto lockTree=root.getChildWithName("MUTATION_LOCKS");
    if(lockTree.isValid())locks=sonara::MutationLocks::fromValueTree(lockTree);
    applyLaneMixTree(*this,root.getChildWithName("LANE_MIX"));
    previewBpm=juce::jlimit(60.0,200.0,(double)root.getProperty("bpm",128.0));

    auto arr=root.getChildWithName("SONARA_ARRANGEMENT");
    if(arr.isValid())
    {
        auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));
        const auto& lanes=made->getLanes();
        for(int i=0;i<musicalLaneCount;++i)
        {
            const int laneIndex=firstMusicalLane+i;
            if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))
                songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);
        }
        std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);
    }

    generationStatus="Project loaded";
    generationProgress.store(1.f);
    return true;
}
bool SonaraAudioProcessor::exportFullMix(const juce::File& file)
{
    auto a=arrangementSnapshot();
    if(!a){generationStatus="Generate a track first";return false;}

    generationStatus="Rendering 24-bit full mix";
    generationProgress.store(.01f);
    sonara::AudioExporter::MixArray mix{};
    for(int i=0;i<12;++i)
    {
        const auto s=getLaneMix(i);
        mix[(size_t)i]={s.level,s.pan,s.width,s.fxSend};
    }

    const bool ok=audioExporter.renderFullMix(
        *a,file,44100.0,
        [this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;},
        &mix);

    generationProgress.store(ok?1.f:0.f);
    generationStatus=ok?"24-bit full mix ready • "+file.getFullPathName():"Full mix export failed";
    return ok;
}

bool SonaraAudioProcessor::exportSelectedLaneAudio(const juce::File& file)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderSelectedLane(*a,selectedLane.load(),file,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationProgress.store(ok?1.f:0.f);generationStatus=ok?"Selected lane WAV ready • "+file.getFullPathName():"Lane export failed";return ok;
}

bool SonaraAudioProcessor::exportAllStems(const juce::File& directory)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderAllStems(*a,directory,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationProgress.store(ok?1.f:0.f);generationStatus=ok?"All 24-bit stems ready • "+directory.getFullPathName():"Stem export failed";return ok;
}

void SonaraAudioProcessor::setLaneMix(int laneIndex,LaneMixParameter parameter,float value) noexcept
{
    if(!juce::isPositiveAndBelow(laneIndex,12))return;
    const size_t i=(size_t)laneIndex;
    switch(parameter)
    {
        case LaneMixParameter::level: laneMixLevel[i].store(juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed); break;
        case LaneMixParameter::pan: laneMixPan[i].store(juce::jlimit(-1.f,1.f,value),std::memory_order_relaxed); break;
        case LaneMixParameter::width: laneMixWidth[i].store(juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed); break;
        case LaneMixParameter::fxSend: laneMixFx[i].store(juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed); break;
    }
}

SonaraAudioProcessor::LaneMixState SonaraAudioProcessor::getLaneMix(int laneIndex) const noexcept
{
    LaneMixState state;
    if(!juce::isPositiveAndBelow(laneIndex,12))return state;
    const size_t i=(size_t)laneIndex;
    state.level=laneMixLevel[i].load(std::memory_order_relaxed);
    state.pan=laneMixPan[i].load(std::memory_order_relaxed);
    state.width=laneMixWidth[i].load(std::memory_order_relaxed);
    state.fxSend=laneMixFx[i].load(std::memory_order_relaxed);
    return state;
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
    juce::ValueTree state("SONARA_STATE"); state.setProperty("schema",5,nullptr); state.setProperty("generationCounter",juce::String(generationCounter),nullptr); state.setProperty("bpm",previewBpm,nullptr);
    state.addChild(engine.patch().toValueTree(),-1,nullptr); state.addChild(locks.toValueTree(),-1,nullptr); state.addChild(makeLaneMixTree(*this),-1,nullptr); if(auto a=arrangementSnapshot()) state.addChild(a->toValueTree(),-1,nullptr);
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
        applyLaneMixTree(*this,state.getChildWithName("LANE_MIX"));
        generationCounter=(uint64_t)juce::jmax<juce::int64>(1,state.getProperty("generationCounter","1").toString().getLargeIntValue());
        previewBpm=juce::jlimit(60.0,200.0,(double)state.getProperty("bpm",128.0));
        auto arr=state.getChildWithName("SONARA_ARRANGEMENT");if(arr.isValid()){auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));const auto& lanes=made->getLanes();for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);}
        patchHistory.clear();patchHistory.push_back(engine.patch());historyIndex=0;generationProgress.store(1.f);generationStatus="Session restored";
    }
}

juce::AudioProcessorEditor* SonaraAudioProcessor::createEditor()
{
#ifndef SONARA_HEADLESS_TEST
    return new SonaraAudioProcessorEditor(*this);
#else
    return nullptr;
#endif
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SonaraAudioProcessor();}