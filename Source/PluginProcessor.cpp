#include "PluginProcessor.h"
#include "Engine/MixPolicy.h"
#include "Generation/ProducerPrompt.h"
#ifndef SONARA_HEADLESS_TEST
#include "PluginEditor.h"
#endif
#include <algorithm>
#include <cmath>

namespace {
std::atomic<int> gSonaraInstanceCount{0};

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

void applyLaneSoundCpuBudget(const juce::String& laneName,sonara::SoundDNA& d)
{
    const auto n=laneName.toUpperCase();

    // Global guard for pathological patches: expensive modulation + many unison
    // oscillators is where the user-visible CPU spikes came from.
    const float complexity=d.fmAmount*1.4f+d.ringMod*1.2f
        +std::abs(d.lfoMorphA)+std::abs(d.lfoMorphB)+d.lfoCutoff*.7f
        +d.noiseLevel*.5f+d.chorus*.4f+d.delay*.3f+d.reverb*.25f;
    if(complexity>2.0f)d.unison=juce::jmin(d.unison,3);
    else if(complexity>1.25f)d.unison=juce::jmin(d.unison,4);
    else d.unison=juce::jmin(d.unison,6);

    if(n=="BASS")
    {
        d.unison=juce::jmin(d.unison,2);d.width=juce::jmin(d.width,.20f);
        d.release=juce::jmin(d.release,.36f);d.reverb=juce::jmin(d.reverb,.035f);d.delay=0.f;
    }
    else if(n=="SUB")
    {
        d.unison=1;d.width=0.f;d.chorus=0.f;d.reverb=0.f;d.delay=0.f;
        d.noiseLevel=0.f;d.release=juce::jmin(d.release,.22f);
    }
    else if(n=="CHORDS")
    {
        d.unison=juce::jmin(d.unison,4);d.release=juce::jmin(d.release,.55f);
        d.reverb=juce::jmin(d.reverb,.12f);d.delay=juce::jmin(d.delay,.08f);
    }
    else if(n=="PAD")
    {
        d.unison=juce::jmin(d.unison,4);d.release=juce::jmin(d.release,.90f);
        d.reverb=juce::jmin(d.reverb,.18f);d.delay=juce::jmin(d.delay,.08f);
    }
    else if(n=="LEAD")
    {
        d.unison=juce::jmin(d.unison,5);d.release=juce::jmin(d.release,.50f);
        d.reverb=juce::jmin(d.reverb,.12f);d.delay=juce::jmin(d.delay,.12f);
    }
    else if(n=="PLUCK"||n=="COUNTER")
    {
        d.unison=juce::jmin(d.unison,3);d.release=juce::jmin(d.release,.34f);
    }
    else if(n=="FX / TRANSITIONS")
        d.unison=juce::jmin(d.unison,2);
}


std::vector<int> melodyFingerprint(const sonara::SongArrangement& a)
{
    return a.getMelodyFingerprint();
}

struct MelodyFingerprintView
{
    const std::vector<int>* values=nullptr;
    int notes=0;
    int recordOffset=2;
    int summaryOffset=0;
    bool valid=false;

    int note(int index,int field) const noexcept
    {
        return (*values)[(size_t)(recordOffset+index*sonara::SongArrangement::melodyFingerprintStride+field)];
    }
    int summary(int field) const noexcept
    {
        return (*values)[(size_t)(summaryOffset+field)];
    }
};

MelodyFingerprintView viewMelodyFingerprint(const std::vector<int>& fp)
{
    MelodyFingerprintView view;
    if(fp.size()<2||fp[0]!=sonara::SongArrangement::melodyFingerprintVersion)return view;
    const int notes=fp[1];
    const int summary=2+notes*sonara::SongArrangement::melodyFingerprintStride;
    const int required=summary+sonara::SongArrangement::melodyFingerprintSummarySize;
    if(notes<=0||required!=(int)fp.size())return view;
    view.values=&fp;view.notes=notes;view.summaryOffset=summary;view.valid=true;
    return view;
}

float fingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    const auto av=viewMelodyFingerprint(a),bv=viewMelodyFingerprint(b);
    if(!av.valid||!bv.valid)return 0.f;
    const int n=juce::jmin(av.notes,bv.notes);
    if(n<6)return 0.f;

    float pitch=0.f,interval=0.f,contour=0.f,chordRole=0.f;
    float onset=0.f,gap=0.f,length=0.f,repeats=0.f,opening=0.f;
    int openingComparisons=0;
    for(int i=0;i<n;++i)
    {
        if(av.note(i,0)==bv.note(i,0))pitch+=1.f;
        if(std::abs(av.note(i,1)-bv.note(i,1))<=1)interval+=1.f;
        if(av.note(i,2)==bv.note(i,2))contour+=1.f;
        if(av.note(i,3)==bv.note(i,3))chordRole+=1.f;
        if(std::abs(av.note(i,4)-bv.note(i,4))<=1)onset+=1.f;
        if(std::abs(av.note(i,5)-bv.note(i,5))<=1)gap+=1.f;
        if(std::abs(av.note(i,6)-bv.note(i,6))<=1)length+=1.f;
        if(av.note(i,8)==bv.note(i,8))repeats+=1.f;
        if(av.note(i,4)<64&&bv.note(i,4)<64)
        {
            opening+=(av.note(i,0)==bv.note(i,0)? .30f:0.f)
                    +(std::abs(av.note(i,1)-bv.note(i,1))<=1?.25f:0.f)
                    +(av.note(i,2)==bv.note(i,2)?.20f:0.f)
                    +(std::abs(av.note(i,4)-bv.note(i,4))<=1?.25f:0.f);
            ++openingComparisons;
        }
    }

    const float inv=1.f/(float)n;
    pitch*=inv;interval*=inv;contour*=inv;chordRole*=inv;
    onset*=inv;gap*=inv;length*=inv;repeats*=inv;
    opening=openingComparisons>0?opening/(float)openingComparisons:0.f;
    const float countRatio=(float)juce::jmin(av.notes,bv.notes)/(float)juce::jmax(1,juce::jmax(av.notes,bv.notes));
    float phraseStarts=0.f;
    for(int bar=0;bar<4;++bar)
        if(std::abs(av.summary(3+bar)-bv.summary(3+bar))<=1)phraseStarts+=.25f;
    const float cadence=(av.summary(7)==bv.summary(7)?.65f:0.f)
                       +(av.summary(8)==bv.summary(8)?.35f:0.f);
    const float range=1.f-juce::jmin(1.f,std::abs((float)av.summary(0)-(float)bv.summary(0))/24.f);

    // Compare what a listener actually recognises: first two-bar hook, complete
    // four-bar rhythm, interval contour, chord-relative choices, phrase starts,
    // repeated-note behaviour, range and cadence. Generic house rhythm or a
    // common rise/fall contour is evidence, not identity on its own: rejecting on
    // either in isolation can exhaust every candidate and leave the old song in
    // place. Perceptual identity requires corroboration across musical dimensions.
    const float rhythmSkeleton=(onset*.34f+gap*.24f+length*.16f+phraseStarts*.26f)*(.82f+.18f*countRatio);
    const float contourSkeleton=(contour*.48f+interval*.36f+cadence*.16f)*(.84f+.16f*countRatio);
    const float pitchIdentity=(pitch*.52f+interval*.24f+chordRole*.24f)*(.82f+.18f*countRatio);
    const float combined=(pitch*.12f+interval*.15f+contour*.13f+chordRole*.10f
                         +onset*.14f+gap*.10f+length*.07f+phraseStarts*.08f
                         +repeats*.04f+range*.03f+cadence*.04f)
                         *(.84f+.16f*countRatio);

    const float openingIdentity=opening*.44f+pitchIdentity*.19f+contourSkeleton*.17f
                               +rhythmSkeleton*.14f+cadence*.06f;
    const float phraseIdentity=pitchIdentity*.29f+contourSkeleton*.24f+rhythmSkeleton*.23f
                              +opening*.14f+cadence*.06f+range*.04f;
    return juce::jlimit(0.f,1.f,juce::jmax(combined,juce::jmax(openingIdentity,phraseIdentity)));
}

float melodyRhythmSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    const auto av=viewMelodyFingerprint(a),bv=viewMelodyFingerprint(b);
    if(!av.valid||!bv.valid)return 0.f;
    const int n=juce::jmin(av.notes,bv.notes);
    if(n<6)return 0.f;
    float score=0.f;
    for(int i=0;i<n;++i)
    {
        if(std::abs(av.note(i,4)-bv.note(i,4))<=1)score+=.38f;
        if(std::abs(av.note(i,5)-bv.note(i,5))<=1)score+=.30f;
        if(std::abs(av.note(i,6)-bv.note(i,6))<=1)score+=.18f;
        if(av.note(i,8)==bv.note(i,8))score+=.06f;
    }
    float phraseStarts=0.f;
    for(int bar=0;bar<4;++bar)
        if(std::abs(av.summary(3+bar)-bv.summary(3+bar))<=1)phraseStarts+=.02f;
    const float countRatio=(float)juce::jmin(av.notes,bv.notes)/(float)juce::jmax(1,juce::jmax(av.notes,bv.notes));
    return juce::jlimit(0.f,1.f,((score/(float)n)+phraseStarts)*(.84f+.16f*countRatio));
}

std::vector<int> lanePatternFingerprint(const sonara::SongArrangement& song,const juce::String& laneName)
{
    std::vector<int> fp;
    const auto* lane=laneNamed(song,laneName);
    if(lane==nullptr||lane->notes.empty())return fp;
    const int limit=juce::jmin(128,(int)lane->notes.size());
    fp.reserve((size_t)(limit*5+2));
    fp.push_back(lane->drums?1:0);
    int previous=lane->notes.front().note;
    double previousBeat=lane->notes.front().beat;
    for(int i=0;i<limit;++i)
    {
        const auto& note=lane->notes[(size_t)i];
        fp.push_back(lane->drums?note.note:note.note-song.getRootMidi());
        fp.push_back(i==0?0:juce::jlimit(-24,24,note.note-previous));
        fp.push_back((int)std::llround(std::fmod(juce::jmax(0.0,note.beat),16.0)*4.0));
        fp.push_back(i==0?0:juce::jlimit(0,64,(int)std::llround((note.beat-previousBeat)*8.0)));
        fp.push_back(juce::jlimit(1,32,(int)std::llround(note.length*8.0)));
        previous=note.note;
        previousBeat=note.beat;
    }
    fp.push_back((int)lane->notes.size());
    return fp;
}

std::vector<int> drumFingerprint(const sonara::SongArrangement& song)
{
    std::vector<int> fp;
    for(const auto& name:{juce::String("KICK"),juce::String("SNARE / CLAP"),juce::String("HATS"),juce::String("PERCUSSION")})
    {
        const auto lane=lanePatternFingerprint(song,name);
        fp.push_back(-1000-(int)fp.size());
        fp.insert(fp.end(),lane.begin(),lane.end());
    }
    return fp;
}

float flatFingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.empty()||b.empty())return 0.f;
    const int n=juce::jmin((int)a.size(),(int)b.size());
    float exact=0.f,near=0.f;
    for(int i=0;i<n;++i)
    {
        if(a[(size_t)i]==b[(size_t)i])exact+=1.f;
        else if(std::abs(a[(size_t)i]-b[(size_t)i])==1)near+=1.f;
    }
    const float countRatio=(float)n/(float)juce::jmax(a.size(),b.size());
    return juce::jlimit(0.f,1.f,((exact+.30f*near)/(float)n)*(.82f+.18f*countRatio));
}

float harmonyFingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.size()<5||b.size()<5)return flatFingerprintSimilarity(a,b);
    const int an=juce::jlimit(1,8,a[0]),bn=juce::jlimit(1,8,b[0]);
    if((int)a.size()<2+an*3||(int)b.size()<2+bn*3)
        return flatFingerprintSimilarity(a,b);

    const int n=juce::jmin(an,bn);
    float main=0.f,alternate=0.f,rhythm=0.f;
    for(int i=0;i<n;++i)
    {
        const int ai=2+i*3,bi=2+i*3;
        if(a[(size_t)ai]==b[(size_t)bi])main+=1.f;
        if(a[(size_t)ai+1]==b[(size_t)bi+1])alternate+=1.f;
        if(std::abs(a[(size_t)ai+2]-b[(size_t)bi+2])<=1)rhythm+=1.f;
    }
    main/=(float)n;alternate/=(float)n;rhythm/=(float)n;
    const float countRatio=(float)n/(float)juce::jmax(an,bn);
    const float cadence=a[1]==b[1]?1.f:0.f;

    // Voicing, inversions and section timing may develop a progression, but they
    // cannot disguise a recycled normalized root loop. Exact main degrees alone
    // reach the hard .86 identity boundary; related harmony remains available via
    // one controlled functional substitution.
    const float progressionIdentity=(main*.86f+alternate*.05f+rhythm*.05f+cadence*.04f)
                                    *(.84f+.16f*countRatio);
    return juce::jlimit(0.f,1.f,juce::jmax(flatFingerprintSimilarity(a,b),progressionIdentity));
}

float harmonyQualityScore(const sonara::SongArrangement& song)
{
    const auto progression=song.getProgressionFingerprint();
    if(progression.size()<5)return 0.f;
    const int n=juce::jlimit(1,8,progression[0]);
    std::array<bool,7> used{};
    int unique=0,consecutiveRepeats=0;
    for(int i=0;i<n;++i)
    {
        const int degree=juce::jlimit(0,6,progression[(size_t)(2+i*3)]);
        if(!used[(size_t)degree]){used[(size_t)degree]=true;++unique;}
        if(i>0&&degree==progression[(size_t)(2+(i-1)*3)])++consecutiveRepeats;
    }
    const float variety=n<=2?1.f:juce::jlimit(0.f,1.f,(float)unique/(float)juce::jmin(n,5));
    const float repetition=1.f-(float)consecutiveRepeats/(float)juce::jmax(1,n-1);
    const bool tonalCentre=used[0];
    const int finalDegree=juce::jlimit(0,6,progression[(size_t)(2+(n-1)*3)]);
    const bool functionalTurnaround=finalDegree==3||finalDegree==4||finalDegree==6;
    return juce::jlimit(0.f,1.f,.35f*variety+.27f*repetition
                        +.20f*(tonalCentre?1.f:.55f)+.18f*(functionalTurnaround?1.f:.35f));
}


const sonara::ArrangementSection* sectionNamed(const sonara::SongArrangement& song,const juce::String& name)
{
    for(const auto& s:song.getSections())if(s.name==name)return &s;
    return nullptr;
}

float laneDensityInSection(const sonara::SongArrangement& song,const juce::String& laneName,
                           const sonara::ArrangementSection* section)
{
    if(section==nullptr)return 0.f;
    const auto* lane=laneNamed(song,laneName);
    if(lane==nullptr)return 0.f;
    const double start=section->startBar*4.0;
    const double end=(section->startBar+section->bars)*4.0;
    int count=0;
    for(const auto& n:lane->notes)if(n.beat>=start&&n.beat<end)++count;
    return count/(float)juce::jmax(1,section->bars);
}

float sectionRhythmOverlap(const sonara::ArrangementLane* lane,
                           const sonara::ArrangementSection* a,
                           const sonara::ArrangementSection* b)
{
    if(lane==nullptr||a==nullptr||b==nullptr)return 0.f;
    std::array<bool,64> first{},second{};
    const double aStart=a->startBar*4.0,bStart=b->startBar*4.0;
    const double aEnd=aStart+8.0,bEnd=bStart+8.0;
    for(const auto& n:lane->notes)
    {
        if(n.beat>=aStart&&n.beat<aEnd)
        {
            const int q=juce::jlimit(0,63,(int)std::llround((n.beat-aStart)*8.0));
            first[(size_t)q]=true;
        }
        if(n.beat>=bStart&&n.beat<bEnd)
        {
            const int q=juce::jlimit(0,63,(int)std::llround((n.beat-bStart)*8.0));
            second[(size_t)q]=true;
        }
    }
    int aCount=0,bCount=0,common=0;
    for(size_t i=0;i<first.size();++i)
    {
        if(first[i])++aCount;
        if(second[i])++bCount;
        if(first[i]&&second[i])++common;
    }
    if(aCount==0||bCount==0)return 0.f;
    return juce::jlimit(0.f,1.f,common/(float)juce::jmax(1,juce::jmin(aCount,bCount)));
}

float internalTwoBarRepeat(const sonara::ArrangementLane* lane,
                           const sonara::ArrangementSection* section)
{
    if(lane==nullptr||section==nullptr||section->bars<4)return .5f;
    sonara::ArrangementSection second=*section;
    second.startBar+=2;
    return sectionRhythmOverlap(lane,section,&second);
}

float sectionPitchRhythmRecall(const sonara::ArrangementLane* lane,
                               const sonara::ArrangementSection* a,
                               const sonara::ArrangementSection* b)
{
    if(lane==nullptr||a==nullptr||b==nullptr)return 0.f;
    struct Cell{int beat=0,pitch=0;};
    std::vector<Cell> first,second;
    const double aStart=a->startBar*4.0,bStart=b->startBar*4.0;
    const double aEnd=aStart+16.0,bEnd=bStart+16.0;
    for(const auto& n:lane->notes)
    {
        if(n.beat>=aStart&&n.beat<aEnd)
            first.push_back({(int)std::llround((n.beat-aStart)*8.0),((n.note%12)+12)%12});
        if(n.beat>=bStart&&n.beat<bEnd)
            second.push_back({(int)std::llround((n.beat-bStart)*8.0),((n.note%12)+12)%12});
    }
    if(first.empty()||second.empty())return 0.f;
    int matched=0;
    for(const auto& x:first)
        for(const auto& y:second)
            if(x.beat==y.beat&&x.pitch==y.pitch){++matched;break;}
    return juce::jlimit(0.f,1.f,matched/(float)juce::jmax<size_t>(1,juce::jmin(first.size(),second.size())));
}


float arrangementSpaceScore(const sonara::SongArrangement& song,const sonara::ArrangementSection* section)
{
    if(section==nullptr)return .5f;
    constexpr int maxBins=384;
    std::array<int,maxBins> stack{};
    const double start=section->startBar*4.0;
    const double end=(section->startBar+section->bars)*4.0;
    const int bins=juce::jlimit(1,maxBins,(int)std::ceil((end-start)*4.0));

    // Count distinct musical lanes starting in each 1/16-note-ish slot. A chord is
    // one production layer, not three separate "collisions".
    const auto& lanes=song.getLanes();
    for(size_t li=4;li<lanes.size();++li)
    {
        if(lanes[li].name=="FX / TRANSITIONS")continue;
        std::array<bool,maxBins> laneUsed{};
        for(const auto& n:lanes[li].notes)
        {
            if(n.beat<start||n.beat>=end)continue;
            const int b=juce::jlimit(0,bins-1,(int)std::floor((n.beat-start)*4.0+.5));
            laneUsed[(size_t)b]=true;
        }
        for(int b=0;b<bins;++b)if(laneUsed[(size_t)b])++stack[(size_t)b];
    }

    int used=0,busy=0,severe=0,peak=0;
    for(int b=0;b<bins;++b)
    {
        if(stack[(size_t)b]<=0)continue;
        ++used;peak=juce::jmax(peak,stack[(size_t)b]);
        if(stack[(size_t)b]>=5)++busy;
        if(stack[(size_t)b]>=6)++severe;
    }
    if(used==0)return .35f;
    const float busyRatio=busy/(float)used;
    const float severeRatio=severe/(float)used;
    const float peakPenalty=peak<=4?0.f:juce::jlimit(0.f,.35f,(peak-4)*.12f);
    return juce::jlimit(0.f,1.f,1.f-busyRatio*.65f-severeRatio*.90f-peakPenalty);
}

float productionQualityScore(const sonara::SongArrangement& song)
{
    const auto* verse=sectionNamed(song,"VERSE");
    const auto* build=sectionNamed(song,"BUILD");
    const auto* drop=sectionNamed(song,"DROP");
    const auto* chorus=sectionNamed(song,"CHORUS");
    const auto* breakdown=sectionNamed(song,"BREAKDOWN");
    const auto* finalHook=sectionNamed(song,"FINAL HOOK");
    const auto* lead=laneNamed(song,"LEAD");
    if(drop==nullptr||lead==nullptr)return 0.f;

    const float motifRepeat=internalTwoBarRepeat(lead,drop);
    const float chorusDropRecall=chorus?sectionRhythmOverlap(lead,chorus,drop):.5f;
    const float chorusDropPitchRecall=chorus?sectionPitchRhythmRecall(lead,chorus,drop):.5f;
    const float hookRecall=finalHook?sectionRhythmOverlap(lead,drop,finalHook):.5f;
    const float spaceScore=arrangementSpaceScore(song,drop);

    const float dropKick=laneDensityInSection(song,"KICK",drop);
    const float breakKick=laneDensityInSection(song,"KICK",breakdown);
    const float drumContrast=juce::jlimit(0.f,1.f,(dropKick-breakKick+.5f)/4.0f);

    const float dropBass=laneDensityInSection(song,"BASS",drop);
    const float breakBass=laneDensityInSection(song,"BASS",breakdown);
    const float bassContrast=juce::jlimit(0.f,1.f,(dropBass-breakBass+.5f)/5.0f);

    const float verseLead=laneDensityInSection(song,"LEAD",verse);
    const float buildLead=laneDensityInSection(song,"LEAD",build);
    const float chorusLead=laneDensityInSection(song,"LEAD",chorus);
    const float dropLead=laneDensityInSection(song,"LEAD",drop);
    const float breakLead=laneDensityInSection(song,"LEAD",breakdown);
    const float leadContrast=juce::jlimit(0.f,1.f,(dropLead-breakLead+.5f)/4.0f);

    const float verseSpace=juce::jlimit(0.f,1.f,1.f-verseLead/3.2f);
    const float buildSpace=juce::jlimit(0.f,1.f,1.f-buildLead/3.0f);
    const float chorusArrival=juce::jlimit(0.f,1.f,(chorusLead-verseLead+.7f)/3.4f);
    const float dropArrival=juce::jlimit(0.f,1.f,(dropLead-chorusLead+1.2f)/2.4f);
    const float breakdownRelease=juce::jlimit(0.f,1.f,(chorusLead-breakLead+.6f)/3.6f);
    const float sectionShape=verseSpace*.16f+buildSpace*.14f+chorusArrival*.25f
                           +dropArrival*.22f+breakdownRelease*.23f;

    // Good EDM drops are neither empty nor note soup. Reward a useful density
    // window while keeping breakdown-to-drop contrast obvious.
    const float leadDensityShape=juce::jlimit(0.f,1.f,1.f-std::abs(dropLead-3.5f)/3.5f);
    const float kickDensityShape=juce::jlimit(0.f,1.f,1.f-std::abs(dropKick-4.2f)/4.2f);

    // Reward recognisable motif memory without demanding exact cloning.
    const float repeatShape=juce::jlimit(0.f,1.f,1.f-std::abs(motifRepeat-.72f)/.72f);
    const float recallShape=juce::jlimit(0.f,1.f,1.f-std::abs(hookRecall-.68f)/.68f);
    const float chorusRecallShape=juce::jlimit(0.f,1.f,1.f-std::abs(chorusDropRecall-.88f)/.88f);
    return juce::jlimit(0.f,1.f,
        sectionShape*.18f+repeatShape*.11f+chorusRecallShape*.12f+chorusDropPitchRecall*.16f+
        recallShape*.09f+spaceScore*.11f+drumContrast*.09f+bassContrast*.06f+
        leadContrast*.04f+leadDensityShape*.03f+kickDensityShape*.01f);
}

float promptCompositionMatch(const juce::String& prompt,const sonara::SongArrangement& song)
{
    const auto p=sonara::ProducerPrompt::parse(prompt).positive;
    const int n=song.getHarmonyProgressionLength();
    float score=.82f;
    // Use the same precedence as the harmony planner. A cinematic mood on a
    // tech-house brief cannot demand both a two-chord vamp and six chords.
    if(p.contains("tech house")||p.contains("minimal house")){if(n>3)score-=.30f;}
    else if(p.contains("cinematic")||p.contains("film")){if(n<6)score-=.24f;}
    else if(p.contains("drum and bass")||p.contains("dnb")){}
    else if(p.contains("pop")||p.contains("radio")){if(n<3||n>6)score-=.18f;}
    else if(p.contains("progressive")||p.contains("melodic house")){if(n<3||n>8)score-=.20f;}
    if(p.contains("minor")&&!song.isMinor())score-=.30f;
    if(p.contains("major")&&song.isMinor())score-=.30f;
    if(p.contains("emotional")&&song.getMelodyArchetypeName()=="SPARSE MOTIF")score-=.08f;
    return juce::jlimit(0.f,1.f,score);
}

float melodyQualityScore(const sonara::SongArrangement& song)
{
    const auto* lead=laneNamed(song,"LEAD");
    const auto minimumNotes=(size_t)juce::jlimit(3,20,song.getBars()/2);
    if(lead==nullptr||lead->notes.size()<minimumNotes)return 0.f;

    int hugeLeaps=0,repeatedRun=1,maxRepeated=1,isolatedOutliers=0;
    int minNote=127,maxNote=0,lowRegisterNotes=0,maxNotesPerBar=0;
    double totalLength=0.0;
    std::array<bool,33> gapKinds{};
    std::vector<int> notesPerBar((size_t)juce::jmax(1,song.getBars()),0);
    int gapVariety=0;

    for(size_t i=0;i<lead->notes.size();++i)
    {
        const auto& n=lead->notes[i];
        minNote=juce::jmin(minNote,n.note);
        maxNote=juce::jmax(maxNote,n.note);
        if(n.note<58)++lowRegisterNotes;
        const int bar=juce::jlimit(0,juce::jmax(0,song.getBars()-1),(int)std::floor(n.beat/4.0));
        if(juce::isPositiveAndBelow(bar,(int)notesPerBar.size()))
        {
            const int count=++notesPerBar[(size_t)bar];
            maxNotesPerBar=juce::jmax(maxNotesPerBar,count);
        }
        totalLength+=n.length;

        if(i>0)
        {
            const auto& p=lead->notes[i-1];
            if(std::abs(n.note-p.note)>12)++hugeLeaps;
            repeatedRun=(n.note==p.note)?repeatedRun+1:1;
            maxRepeated=juce::jmax(maxRepeated,repeatedRun);
            const int q=juce::jlimit(0,32,(int)std::llround((n.beat-p.beat)*8.0));
            if(!gapKinds[(size_t)q]){gapKinds[(size_t)q]=true;++gapVariety;}
        }
    }

    for(size_t i=1;i+1<lead->notes.size();++i)
    {
        const auto& prev=lead->notes[i-1];
        const auto& cur=lead->notes[i];
        const auto& next=lead->notes[i+1];
        if(std::abs(cur.note-prev.note)>7&&std::abs(cur.note-next.note)>7
           &&std::abs(prev.note-next.note)<=5&&cur.length<.60)
            ++isolatedOutliers;
    }

    const float leapRatio=(float)hugeLeaps/(float)juce::jmax<size_t>(1,lead->notes.size()-1);
    const float rangeScore=juce::jlimit(0.f,1.f,1.f-std::abs((float)(maxNote-minNote)-24.f)/36.f);
    const float leapScore=juce::jlimit(0.f,1.f,1.f-leapRatio*3.8f);
    const float repeatScore=maxRepeated<=2?1.f:(maxRepeated==3?.78f:(maxRepeated==4?.50f:.12f));
    const float rhythmScore=juce::jlimit(0.f,1.f,(float)gapVariety/6.f);
    const float avgLength=(float)(totalLength/(double)lead->notes.size());
    const float lengthScore=juce::jlimit(0.f,1.f,1.f-std::abs(avgLength-.38f)/.75f);
    const float density=(float)lead->notes.size()/(float)juce::jmax(1,song.getBars());
    const float densityScore=juce::jlimit(0.f,1.f,1.f-std::abs(density-1.85f)/2.5f);
    const float outlierRatio=isolatedOutliers/(float)juce::jmax<size_t>(1,lead->notes.size());
    const float outlierScore=juce::jlimit(0.f,1.f,1.f-outlierRatio*12.f);
    const float barClarity=maxNotesPerBar<=3?1.f:(maxNotesPerBar==4?.72f:(maxNotesPerBar==5?.28f:0.f));
    const float lowRegisterScore=lowRegisterNotes==0?1.f:
        juce::jlimit(0.f,1.f,1.f-lowRegisterNotes/(float)juce::jmax<size_t>(1,lead->notes.size())*10.f);

    return juce::jlimit(0.f,1.f,
        rangeScore*.10f+leapScore*.16f+repeatScore*.13f+
        rhythmScore*.10f+lengthScore*.08f+densityScore*.10f+
        outlierScore*.14f+barClarity*.11f+lowRegisterScore*.08f);
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

void resetLaneMix(SonaraAudioProcessor& p)
{
    for(int i=0;i<12;++i)
    {
        p.setLaneMix(i,SonaraAudioProcessor::LaneMixParameter::level,1.f);
        p.setLaneMix(i,SonaraAudioProcessor::LaneMixParameter::pan,0.f);
        p.setLaneMix(i,SonaraAudioProcessor::LaneMixParameter::width,1.f);
        p.setLaneMix(i,SonaraAudioProcessor::LaneMixParameter::fxSend,1.f);
    }
}

juce::ValueTree makeReferenceTree(const sonara::ReferenceAnalysis& r,bool melodyPreview)
{
    juce::ValueTree root("REFERENCE_ANALYSIS");
    root.setProperty("schema",1,nullptr);
    root.setProperty("fileName",r.fileName,nullptr);
    root.setProperty("keyName",r.keyName,nullptr);
    root.setProperty("sampleRate",r.sampleRate,nullptr);
    root.setProperty("duration",r.durationSeconds,nullptr);
    root.setProperty("bpm",r.estimatedBpm,nullptr);
    root.setProperty("rmsDb",r.rmsDb,nullptr);
    root.setProperty("peakDb",r.peakDb,nullptr);
    root.setProperty("melodyPreview",melodyPreview,nullptr);
    for(const auto& n:r.melody)
    {
        juce::ValueTree note("NOTE");
        note.setProperty("beat",n.beat,nullptr);
        note.setProperty("length",n.length,nullptr);
        note.setProperty("midi",n.midiNote,nullptr);
        note.setProperty("velocity",n.velocity,nullptr);
        root.addChild(note,-1,nullptr);
    }
    return root;
}

sonara::ReferenceAnalysis referenceFromTree(const juce::ValueTree& root,bool& melodyPreview)
{
    sonara::ReferenceAnalysis out;
    melodyPreview=false;
    if(!root.isValid()||root.getType().toString()!="REFERENCE_ANALYSIS")return out;
    out.fileName=root.getProperty("fileName","").toString();
    out.keyName=root.getProperty("keyName","Unknown").toString();
    out.sampleRate=juce::jmax(0.0,(double)root.getProperty("sampleRate",0.0));
    out.durationSeconds=juce::jmax(0.0,(double)root.getProperty("duration",0.0));
    out.estimatedBpm=juce::jlimit(40.0,240.0,(double)root.getProperty("bpm",120.0));
    out.rmsDb=(float)root.getProperty("rmsDb",-100.f);
    out.peakDb=(float)root.getProperty("peakDb",-100.f);
    melodyPreview=(bool)root.getProperty("melodyPreview",false);
    out.melody.reserve((size_t)root.getNumChildren());
    for(int i=0;i<root.getNumChildren();++i)
    {
        const auto note=root.getChild(i);
        if(note.getType().toString()!="NOTE")continue;
        const int midi=juce::jlimit(0,127,(int)note.getProperty("midi",60));
        const int velocity=juce::jlimit(1,127,(int)note.getProperty("velocity",96));
        const double beat=juce::jmax(0.0,(double)note.getProperty("beat",0.0));
        const double length=juce::jmax(.0625,(double)note.getProperty("length",.5));
        out.melody.push_back({beat,length,midi,velocity});
    }
    return out;
}

SonaraAudioProcessor::SonaraAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    audioExporter.setCancelFlag(&backgroundCancel);
    gSonaraInstanceCount.fetch_add(1,std::memory_order_relaxed);
    engine.setVoiceLimit(4);
    sessionSalt = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks());
    for(int i=0;i<12;++i){laneMixLevel[(size_t)i].store(1.f);laneMixPan[(size_t)i].store(0.f);laneMixWidth[(size_t)i].store(1.f);laneMixFx[(size_t)i].store(1.f);}
    patchHistory.push_back(engine.patch());
    historyIndex = 0;
}

SonaraAudioProcessor::~SonaraAudioProcessor()
{
    gSonaraInstanceCount.fetch_sub(1,std::memory_order_relaxed);
}

void SonaraAudioProcessor::prepareToPlay(double sr, int bs)
{
    previewSampleRate = juce::jmax(8000.0, sr);
    maximumBlockSize = juce::jmax(8192, bs);
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * 16.0);

    engine.prepare(sr, maximumBlockSize, getTotalNumOutputChannels());
    songRenderer.prepare(sr,maximumBlockSize);
    hostMidiRenderer.prepare(sr,maximumBlockSize);
    hostMidiSample=0;hostWasPlaying=false;expectedHostPpq=-1.0;
    if(auto a=arrangementSnapshot()){songRenderer.configure(*a);hostMidiRenderer.configure(*a);rendererPlan=a.get();}else rendererPlan=nullptr;
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

    if(useLaneMidiSound.load(std::memory_order_acquire)&&(!m.isEmpty()||!previewPlaying.load()))
    {
        auto a=arrangementSnapshot();
        // Advance DSP over leading rests as well: chorus phase and delay/FX
        // history are part of the saved sound, even before the first note.
        if(a&&(!songPlaying.load()||!m.isEmpty()))
        {
            if(!m.isEmpty()){songPlaying.store(false);previewPlaying.store(false);}
            int64_t position=hostMidiSample;
            bool playing=false,seek=false;
            if(auto* playhead=getPlayHead())if(auto transport=playhead->getPosition())
            {
                playing=transport->getIsPlaying();
                seek=playing&&!hostWasPlaying;
                if(auto ppq=transport->getPpqPosition();ppq&&playing)
                {
                    position=(int64_t)std::llround(std::max(0.0,*ppq)*previewSampleRate*60.0/a->getBpm());
                    // Section automation follows musical position. DSP continuity
                    // follows the host tempo, not the saved song's sample stride.
                    // Different tempos previously looked like a seek every block.
                    const double tempo=transport->getBpm().orFallback(a->getBpm());
                    if(expectedHostPpq>=0.0&&std::abs(*ppq-expectedHostPpq)>.001)seek=true;
                    expectedHostPpq=*ppq+b.getNumSamples()/previewSampleRate*tempo/60.0;
                }
                else expectedHostPpq=-1.0;
            }
            if(!playing&&hostWasPlaying){seek=true;expectedHostPpq=-1.0;}
            hostWasPlaying=playing;
            if(seek)hostMidiRenderer.reset(position,false);
            const auto mix=songMixSnapshot();
            hostMidiRenderer.renderMidi(*a,b,m,position,b.getNumSamples(),&mix,selectedLane.load(),
                                       midiRoutingMode.load()==MidiRoutingMode::selectedLane);
            hostMidiSample=position+b.getNumSamples();
            m.clear();return;
        }
        // Advance over leading rests too; MIDI imported at its original song
        // position must receive the same section automation as internal playback.
        if(a)hostMidiSample+=b.getNumSamples();
    }

    if(previewPlaying.load()&&referenceMelodyPreview.load()&&referenceSong)
    {
        m.clear();const auto start=previewSample.load();songRenderer.render(*referenceSong,b,start,b.getNumSamples());
        previewSample.store(start+b.getNumSamples());if(start+b.getNumSamples()>=previewLengthSamples)previewPlaying.store(false);
        return;
    }

    if (songPlaying.load(std::memory_order_acquire))
    {
        m.clear();
        renderSongBlock(b, b.getNumSamples());
        return;
    }

    // A loaded but idle SONARA instance must be nearly free. Previously every
    // instance still entered the synth/render/post-processing path every block.
    const bool previewActive=previewPlaying.load(std::memory_order_acquire);
    if(!previewActive&&m.isEmpty()&&!engine.hasActiveVoices())
        return;

    // Opening another instance must never rewrite the currently audible patch.
    // The existing voice cap and idle fast path keep CPU bounded.
    engine.setRuntimeEcoMode(false);

    injectPreviewMidi(m, b.getNumSamples());
    engine.render(b, m);

    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(c, i, juce::jlimit(-sonara::mixpolicy::masterCeiling(),sonara::mixpolicy::masterCeiling(),std::tanh(b.getSample(c, i) * 1.12f)));
}

void SonaraAudioProcessor::startPreview()
{
    const juce::ScopedLock lock(getCallbackLock());
    stopSongPreview();
    engine.reset();
    if(referenceMelodyPreview.load()&&referenceLoaded)
    {
        referenceSong=std::make_shared<sonara::SongArrangement>(sonara::AudioExporter::makeReferenceSong(reference,engine.patchSnapshot()));
        songRenderer.configure(*referenceSong);rendererPlan=referenceSong.get();previewSample.store(0);
        previewLengthSamples=(int64_t)std::llround(referenceSong->getTotalBeats()*previewSampleRate*60.0/referenceSong->getBpm()+previewSampleRate*4.0);
        previewPlaying.store(true);generationStatus="Reference preview • same renderer as RESOUND WAV";return;
    }
    const double beats = referenceMelodyPreview && referenceLoaded && !reference.melody.empty() ? juce::jmax(1.0, reference.melodyBeats()) : 16.0;
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * beats);
    previewSample.store(0);
    previewPlaying.store(true);
    generationStatus = referenceMelodyPreview ? "RESOUND preview playing" : "Sound preview playing";
}

void SonaraAudioProcessor::stopPreview()
{
    const juce::ScopedLock lock(getCallbackLock());
    previewPlaying.store(false);
    previewSample.store(0);
    engine.reset();
    hostMidiRenderer.reset(0,false);hostMidiSample=0;hostWasPlaying=false;expectedHostPpq=-1.0;
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
        const int64_t step = std::max<int64_t>(1, previewLengthSamples / 16);
        for (int i = 0; i < 16; ++i)
        {
            const int64_t on = (int64_t) i * step;
            const int64_t off = std::min<int64_t>(previewLengthSamples - 1, on + (step * 3) / 4);
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
    useLaneMidiSound.store(false);
    engine.setPatch(d);
    if (historyIndex + 1 < (int) patchHistory.size()) patchHistory.erase(patchHistory.begin() + historyIndex + 1, patchHistory.end());
    patchHistory.push_back(d);
    if (patchHistory.size() > 32) patchHistory.erase(patchHistory.begin());
    historyIndex = (int) patchHistory.size() - 1;
}

void SonaraAudioProcessor::generatePatch(const juce::String& p)
{
    const juce::ScopedLock operation(operationLock);
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
    const juce::ScopedLock operation(operationLock);
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter), .45f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = s; });
    setPatchWithHistory(d); generationStatus = "Mutation ready";
}

void SonaraAudioProcessor::generateSimilarPatch()
{
    const juce::ScopedLock operation(operationLock);
    const auto source = engine.patch();
    const auto seed = source.seed + (++generationCounter * 0x517cc1b727220a95ULL);
    auto d = generator.mutate(source, seed, .18f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = "Similar: " + s; });
    d.sourcePrompt = source.sourcePrompt;
    setPatchWithHistory(d); generationStatus = "Similar variation ready";
}

void SonaraAudioProcessor::generateVariation(int index)
{
    const juce::ScopedLock operation(operationLock);
    const float amount = .12f + .12f * (float) juce::jlimit(1, 4, index);
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter * 0x94d049bb133111ebULL), amount, locks);
    d.name = "Variation " + juce::String(index);
    setPatchWithHistory(d); generationProgress.store(1.f); generationStatus = "Variation " + juce::String(index) + " ready";
}

void SonaraAudioProcessor::randomizePatch()
{
    const juce::ScopedLock operation(operationLock);
    const auto sourcePrompt = engine.patch().sourcePrompt.isNotEmpty() ? engine.patch().sourcePrompt : "experimental wide synth";
    generatePatch(sourcePrompt + " randomized texture");
}

void SonaraAudioProcessor::undoPatch(){const juce::ScopedLock operation(operationLock); if(historyIndex>0){ --historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Undo"; } }
void SonaraAudioProcessor::redoPatch(){const juce::ScopedLock operation(operationLock); if(historyIndex+1<(int)patchHistory.size()){ ++historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Redo"; } }
void SonaraAudioProcessor::captureA(){const juce::ScopedLock operation(operationLock); patchA=engine.patch();hasA=true;generationStatus="A captured"; }
void SonaraAudioProcessor::captureB(){const juce::ScopedLock operation(operationLock); patchB=engine.patch();hasB=true;generationStatus="B captured"; }
void SonaraAudioProcessor::recallA(){const juce::ScopedLock operation(operationLock); if(hasA){ engine.setPatch(patchA);generationStatus="A recalled"; } }
void SonaraAudioProcessor::recallB(){const juce::ScopedLock operation(operationLock); if(hasB){ engine.setPatch(patchB);generationStatus="B recalled"; } }

bool SonaraAudioProcessor::generateTrack(const juce::String& prompt)
{
    const juce::ScopedLock operation(operationLock);
    setSongPromptDraft(prompt);
    stopPreview();stopSongPreview();
    generationProgress.store(.03f);
    generationStatus="Planning new harmony, melody and groove identities";

    const auto previous=arrangementSnapshot();
    const auto previousFingerprint=previous?melodyFingerprint(*previous):std::vector<int>{};
    const auto previousHarmony=previous?previous->getHarmonyFingerprint():std::vector<int>{};
    const auto previousBass=previous?lanePatternFingerprint(*previous,"BASS"):std::vector<int>{};
    const auto previousDrums=previous?drumFingerprint(*previous):std::vector<int>{};
    const auto previousPluck=previous?lanePatternFingerprint(*previous,"PLUCK"):std::vector<int>{};
    const auto previousStructure=previous?previous->getStructureFingerprint():std::vector<int>{};

    // Invalidate the displayed/exportable output before any attempt. A failed
    // request can never masquerade as success by leaving the previous song live.
    storeArrangement(std::shared_ptr<const sonara::SongArrangement>{});
    lastSongSeed.store(0,std::memory_order_relaxed);
    const auto preparedPrompt=sonara::ProducerPrompt::parse(prompt);
    if(prompt.trim().isEmpty()||prompt.length()>8192||preparedPrompt.exclusions==4095u)
    {
        generationProgress.store(0.f);
        generationStatus="Generation failed • enter a prompt with at least one instrument";
        return false;
    }

    std::shared_ptr<sonara::SongArrangement> made;
    std::shared_ptr<sonara::SongArrangement> bestCandidate;
    std::vector<int> acceptedFingerprint,acceptedHarmony,acceptedBass,acceptedDrums,acceptedPluck,acceptedStructure;
    std::vector<int> bestFingerprint,bestHarmony,bestBass,bestDrums,bestPluck,bestStructure;
    uint64_t seed=0,bestSeed=0;
    float melodySimilarityMax=1.f,harmonySimilarityMax=1.f,wholeSimilarityMax=1.f;
    float bestMelodySimilarity=1.f,bestHarmonySimilarity=1.f,bestWholeSimilarity=1.f;
    float bestQuality=0.f,bestPromptMatch=0.f,bestCombined=-1.f;

    const auto maxFlatSimilarity=[](const std::vector<int>& candidate,
                                    const std::vector<int>& previousValue,
                                    const std::deque<std::vector<int>>& history)
    {
        float value=previousValue.empty()?0.f:flatFingerprintSimilarity(previousValue,candidate);
        for(const auto& old:history)value=juce::jmax(value,flatFingerprintSimilarity(old,candidate));
        return value;
    };

    constexpr int maxAttempts=48;
    for(int attempt=0;attempt<maxAttempts;++attempt)
    {
        if(backgroundCancel.load()){generationStatus="Generation cancelled";generationProgress.store(0);return false;}
        const uint64_t entropy=static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                             ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks())
                             ^ ((uint64_t)(attempt+1)*0xd1342543de82ef95ULL);
        const uint64_t candidateSeed=scrambleSongSeed((uint64_t)prompt.hashCode64()
                           ^ sessionSalt
                           ^ (++generationCounter*0x9e3779b97f4a7c15ULL)
                           ^ entropy
                           ^ ((uint64_t)attempt*0xa24baed4963ee407ULL)
                           ^ 0x534f4e475f465245ULL);

        auto candidate=std::make_shared<sonara::SongArrangement>();
        // Candidate generation deliberately stops before SoundDNA synthesis. The
        // full palette is generated exactly once, after the winning composition
        // plan has passed prompt, quality and whole-song novelty scoring.
        candidate->generateComposition(prompt,previewBpm,candidateSeed);
        if(!candidate->validate())continue;
        auto fingerprint=melodyFingerprint(*candidate);
        auto harmony=candidate->getHarmonyFingerprint();
        auto bass=lanePatternFingerprint(*candidate,"BASS");
        auto drums=drumFingerprint(*candidate);
        auto pluck=lanePatternFingerprint(*candidate,"PLUCK");
        auto structure=candidate->getStructureFingerprint();

        float melodySim=previousFingerprint.empty()?0.f:fingerprintSimilarity(previousFingerprint,fingerprint);
        float rhythmSim=previousFingerprint.empty()?0.f:melodyRhythmSimilarity(previousFingerprint,fingerprint);
        for(const auto& historic:melodyHistory)
        {
            melodySim=juce::jmax(melodySim,fingerprintSimilarity(historic,fingerprint));
            rhythmSim=juce::jmax(rhythmSim,melodyRhythmSimilarity(historic,fingerprint));
        }

        float harmonySim=previousHarmony.empty()?0.f:
            harmonyFingerprintSimilarity(previousHarmony,harmony);
        for(const auto& historic:harmonyHistory)
            harmonySim=juce::jmax(harmonySim,harmonyFingerprintSimilarity(historic,harmony));
        const float bassSim=maxFlatSimilarity(bass,previousBass,bassHistory);
        const float drumSim=maxFlatSimilarity(drums,previousDrums,drumHistory);
        const float pluckSim=maxFlatSimilarity(pluck,previousPluck,pluckHistory);
        const float structureSim=maxFlatSimilarity(structure,previousStructure,structureHistory);
        const float wholeSim=juce::jlimit(0.f,1.f,
              harmonySim*.35f+melodySim*.25f+rhythmSim*.12f+bassSim*.10f
             +drumSim*.08f+pluckSim*.05f+structureSim*.05f);

        const bool leadExcluded=(candidate->getExclusions()&sonara::ProducerPrompt::lead)!=0u;
        const bool drumsExcluded=(candidate->getExclusions()&sonara::ProducerPrompt::kick)!=0u;
        const float melodyQuality=leadExcluded?1.f:melodyQualityScore(*candidate);
        const float harmonyQuality=harmonyQualityScore(*candidate);
        const float productionQuality=(leadExcluded||drumsExcluded)?.82f:productionQualityScore(*candidate);
        const bool melodyClear=melodyQuality>=.68f;
        const float quality=melodyQuality*.42f+harmonyQuality*.20f+productionQuality*.38f;
        const float promptMatch=promptCompositionMatch(prompt,*candidate);
        const float novelty=1.f-wholeSim;
        // v1.3: novelty is a constraint, not the main creative goal. Prefer the
        // strongest song that is sufficiently different over the strangest song.
        const float combined=quality*.58f+promptMatch*.27f+novelty*.15f;

        // Fallback is still required to be genuinely new. v1.3's old
        // fallback could publish a high-quality but familiar hook after the strict
        // gate missed; that is how repeated "same song" generations slipped out.
        const bool noveltySafe=melodySim<.55f&&harmonySim<.86f&&wholeSim<.64f;
        if((!fingerprint.empty()||leadExcluded)&&!harmony.empty()&&noveltySafe&&melodyClear
           &&quality>=.66f&&promptMatch>=.64f&&combined>bestCombined)
        {
            bestCombined=combined;
            bestQuality=quality;
            bestPromptMatch=promptMatch;
            bestMelodySimilarity=melodySim;
            bestHarmonySimilarity=harmonySim;
            bestWholeSimilarity=wholeSim;
            bestSeed=candidateSeed;
            bestCandidate=candidate;
            bestFingerprint=fingerprint;
            bestHarmony=harmony;
            bestBass=bass;
            bestDrums=drums;
            bestPluck=pluck;
            bestStructure=structure;
        }

        generationProgress.store(.08f+.018f*attempt);

        // Harmony and melody both have hard identity gates. This prevents a tiny
        // lead reroll from hiding a recycled chord/root skeleton.
        if((!fingerprint.empty()||leadExcluded)&&!harmony.empty()&&melodySim<.55f&&harmonySim<.86f
           &&wholeSim<.64f&&melodyQuality>=.72f&&quality>=.75f&&promptMatch>=.68f)
        {
            made=std::move(candidate);
            acceptedFingerprint=std::move(fingerprint);
            acceptedHarmony=std::move(harmony);
            acceptedBass=std::move(bass);
            acceptedDrums=std::move(drums);
            acceptedPluck=std::move(pluck);
            acceptedStructure=std::move(structure);
            seed=candidateSeed;
            melodySimilarityMax=melodySim;
            harmonySimilarityMax=harmonySim;
            wholeSimilarityMax=wholeSim;
            bestQuality=quality;
            bestPromptMatch=promptMatch;
            break;
        }

        generationStatus=(melodyQuality<.72f?"Rejecting complicated or weak melody":
                          (quality<.75f?"Rejecting weak song structure":
                          (harmonySim>=.86f?"Rejecting recycled harmony":
                          (melodySim>=.55f?"Rejecting recycled hook":"Rejecting familiar whole song"))))
                       +juce::String(" • trying composition ")
                       +juce::String(attempt+2)+"/"+juce::String(maxAttempts);
    }

    // If the strict quality gate misses, fallback may only use a candidate that
    // already passed the hard novelty floors above. Never publish a recycled hook.
    if(!made&&bestCandidate)
    {
        made=std::move(bestCandidate);
        acceptedFingerprint=std::move(bestFingerprint);
        acceptedHarmony=std::move(bestHarmony);
        acceptedBass=std::move(bestBass);
        acceptedDrums=std::move(bestDrums);
        acceptedPluck=std::move(bestPluck);
        acceptedStructure=std::move(bestStructure);
        seed=bestSeed;
        melodySimilarityMax=bestMelodySimilarity;
        harmonySimilarityMax=bestHarmonySimilarity;
        wholeSimilarityMax=bestWholeSimilarity;
    }

    if(!made)
    {
        generationProgress.store(0.f);
        generationStatus="Generation failed • no valid new song was published";
        return false;
    }

    if(!acceptedFingerprint.empty()&&!acceptedHarmony.empty())
    {
        melodyHistory.push_back(acceptedFingerprint);
        harmonyHistory.push_back(acceptedHarmony);
        bassHistory.push_back(acceptedBass);
        drumHistory.push_back(acceptedDrums);
        pluckHistory.push_back(acceptedPluck);
        structureHistory.push_back(acceptedStructure);
        const auto trim=[](auto& history){while(history.size()>10)history.pop_front();};
        trim(melodyHistory);trim(harmonyHistory);trim(bassHistory);
        trim(drumHistory);trim(pluckHistory);trim(structureHistory);
    }

    if(backgroundCancel.load()){generationStatus="Generation cancelled";generationProgress.store(0);return false;}
    made->finalizeSoundPalette();
    lastSongSeed.store(seed,std::memory_order_relaxed);
    lastMelodyNovelty.store(juce::jlimit(0.f,1.f,1.f-melodySimilarityMax),std::memory_order_relaxed);
    lastHarmonyNovelty.store(juce::jlimit(0.f,1.f,1.f-harmonySimilarityMax),std::memory_order_relaxed);
    lastSongNovelty.store(juce::jlimit(0.f,1.f,1.f-wholeSimilarityMax),std::memory_order_relaxed);
    generationProgress.store(.66f);
    generationStatus="Loading fresh SoundDNA palette into the new composition";

    const auto& lanes=made->getLanes();
    

    previewBpm=made->getBpm();
    storeArrangement(std::shared_ptr<const sonara::SongArrangement>(made));
    setSelectedLane(lanes.size()>9?9:0);
    generationProgress.store(1.f);
    generationStatus="NEW SONG READY • whole "+juce::String((1.f-wholeSimilarityMax)*100.f,0)
                   +"% • harmony "+juce::String((1.f-harmonySimilarityMax)*100.f,0)
                   +"% • melody "+juce::String((1.f-melodySimilarityMax)*100.f,0)
                   +"% • quality "+juce::String(bestQuality*100.f,0)
                   +"% • prompt "+juce::String(bestPromptMatch*100.f,0)+"%";
    return true;
}

void SonaraAudioProcessor::publishSong(std::shared_ptr<sonara::SongArrangement> made)
{
    const auto& lanes=made->getLanes();
    
    previewBpm=made->getBpm();
    lastSongSeed.store(made->getSongId(),std::memory_order_relaxed);
    storeArrangement(std::shared_ptr<const sonara::SongArrangement>(made));
    setSelectedLane(9);
}

bool SonaraAudioProcessor::generateTrackWithSeed(const juce::String& prompt,uint64_t seed)
{
    const juce::ScopedLock operation(operationLock);
    setSongPromptDraft(prompt);
    stopPreview();stopSongPreview();
    storeArrangement(std::shared_ptr<const sonara::SongArrangement>{});
    lastSongSeed.store(0,std::memory_order_relaxed);
    generationProgress.store(.04f);
    if(prompt.trim().isEmpty()||prompt.length()>8192)
    {generationStatus="Generation failed • enter a valid prompt";generationProgress.store(0.f);return false;}
    generationStatus="Planning the requested seed";
    if(backgroundCancel.load()){generationStatus="Generation cancelled";generationProgress.store(0);return false;}
    auto made=std::make_shared<sonara::SongArrangement>();
    made->generate(prompt,previewBpm,seed);
    if(backgroundCancel.load()){generationStatus="Generation cancelled";generationProgress.store(0);return false;}
    juce::String reason;
    if(!made->validate(&reason))
    {generationStatus="Generation failed • "+reason;generationProgress.store(0.f);return false;}
    publishSong(std::move(made));
    generationStatus="Song ready • reproduced seed "+juce::String::toHexString((juce::int64)seed);
    generationProgress.store(1.f);
    return true;
}

bool SonaraAudioProcessor::randomizeEverything(const juce::String& prompt)
{
    const juce::ScopedLock operation(operationLock);
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
    if(!generateTrack(q))
    {
        generationStatus="RANDOMIZE FAILED • no stale song published";
        return false;
    }
    generationStatus="EVERYTHING RANDOMIZED • new song + melody + structure + drums + bass + sub + SoundDNA";
    return true;
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
    const juce::ScopedLock operation(operationLock);
    stopSongPreview();
    generationProgress.store(.08f);
    generationStatus = "Understanding drum prompt only";

    const uint64_t seed = (uint64_t) prompt.hashCode64()
                        ^ sessionSalt
                        ^ (++generationCounter * 0xa24baed4963ee407ULL)
                        ^ 0x4452554d535f4f4eULL;

    auto current = arrangementSnapshot();
    std::shared_ptr<sonara::SongArrangement> updated;
    if(current)
    {
        updated=std::make_shared<sonara::SongArrangement>(*current);
        updated->regenerateDrumsOnly(prompt+" drums only tight punchy fills transitions",seed);
    }
    else
    {
        updated=std::make_shared<sonara::SongArrangement>();
        updated->generate(prompt+" drums only tight punchy fills transitions",previewBpm,seed);
    }


    storeArrangement(std::shared_ptr<const sonara::SongArrangement>(updated));
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

void SonaraAudioProcessor::startChorusPreview()
{
    auto a=arrangementSnapshot();
    if(!a){generationStatus="Generate a full track first";return;}
    for(const auto& section:a->getSections())
        if(section.name=="CHORUS")
        {
            startSongPreviewAtBar(section.startBar);
            generationStatus="CHORUS PREVIEW • bar "+juce::String(section.startBar+1);
            return;
        }
    generationStatus="No CHORUS section in this arrangement";
}

void SonaraAudioProcessor::startDropPreview()
{
    auto a=arrangementSnapshot();
    if(!a){generationStatus="Generate a full track first";return;}
    for(const auto& section:a->getSections())
        if(section.name=="DROP")
        {
            startSongPreviewAtBar(section.startBar);
            generationStatus="DROP PREVIEW • bar "+juce::String(section.startBar+1);
            return;
        }
    generationStatus="No DROP section in this arrangement";
}

void SonaraAudioProcessor::startSongPreviewAtBar(int bar)
{
    const juce::ScopedLock lock(getCallbackLock());
    auto a=arrangementSnapshot();if(!a||a->isEmpty()){generationStatus="Generate a full track first";return;}
    stopPreview();
    const int safe=juce::jlimit(0,a->getBars()-1,bar);
    const auto start=(int64_t)std::llround(safe*4.0*previewSampleRate*60.0/a->getBpm());
    if(rendererPlan!=a.get()){songRenderer.configure(*a);rendererPlan=a.get();}
    songRenderer.reset(start);songSample.store(start);songPlaying.store(true);
    generationStatus="Playing "+currentSectionName()+" • bar "+juce::String(safe+1);
}
void SonaraAudioProcessor::pauseSongPreview()
{
    const juce::ScopedLock lock(getCallbackLock());
    if(!songPlaying.exchange(false))return;
    generationStatus="Song preview paused • bar "+juce::String(currentSongBar()+1);
}
void SonaraAudioProcessor::resumeSongPreview()
{
    const juce::ScopedLock lock(getCallbackLock());
    auto a=arrangementSnapshot();if(!a)return;
    const auto total=(int64_t)std::llround(a->getTotalBeats()*previewSampleRate*60.0/a->getBpm());
    const auto position=songSample.load();
    if(position<=0||position>=total){startSongPreviewAtBar(0);return;}
    // Pausing retains envelopes, FX and note state at the same sample.
    songPlaying.store(true);generationStatus="Song preview resumed • "+currentSectionName();
}
void SonaraAudioProcessor::stopSongPreview()
{
    const juce::ScopedLock lock(getCallbackLock());
    songPlaying.store(false);songSample.store(0);songRenderer.reset(0);
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
    const double spb=previewSampleRate*60.0/a->getBpm();
    const double beat=spb>0.0?(double)songSample.load()/spb:0.0;
    const double barPosition=beat/sonara::SongArrangement::beatsPerBar;
    const double nearestBoundary=std::round(barPosition);
    // startSongPreviewAtBar() must round an exact musical boundary to an integer
    // sample. When that sample is converted back to beats it can land just below
    // the requested bar, making PLAY DROP report the preceding CHORUS. Snap only
    // positions within half a sample of a bar boundary; normal playback still
    // advances continuously and cannot jump early.
    const double halfSampleInBars=spb>0.0?.5/(spb*sonara::SongArrangement::beatsPerBar):0.0;
    const double stableBar=std::abs(barPosition-nearestBoundary)<=halfSampleInBars+1.0e-12
        ?nearestBoundary:std::floor(barPosition);
    const int exactBar=(int)stableBar;
    return juce::jlimit(0,juce::jmax(0,a->getBars()-1),exactBar);
}

juce::String SonaraAudioProcessor::currentSectionName() const
{
    auto a=arrangementSnapshot();if(!a)return {};
    const int bar=currentSongBar();
    for(const auto& s:a->getSections())
        if(bar>=s.startBar&&bar<s.startBar+s.bars)return s.name;
    return {};
}

sonara::SongMixArray SonaraAudioProcessor::songMixSnapshot() const noexcept
{
    sonara::SongMixArray mix{};for(int i=0;i<12;++i){const auto s=getLaneMix(i);mix[(size_t)i]={s.level,s.pan,s.width,s.fxSend};}return mix;
}
void SonaraAudioProcessor::storeArrangement(std::shared_ptr<const sonara::SongArrangement> made)
{
    const juce::ScopedLock lock(getCallbackLock());
    if(made){songRenderer.configure(*made);songRenderer.reset(songSample.load());hostMidiRenderer.configure(*made);rendererPlan=made.get();lastSongSeed.store(made->getSongId());}
    else {songPlaying.store(false);songRenderer.reset(0);hostMidiRenderer.reset(0,false);useLaneMidiSound.store(false);rendererPlan=nullptr;lastSongSeed.store(0);}
    hostMidiSample=0;hostWasPlaying=false;expectedHostPpq=-1.0;
    std::atomic_store_explicit(&arrangement,std::move(made),std::memory_order_release);
}
void SonaraAudioProcessor::renderSongBlock(juce::AudioBuffer<float>& out,int numSamples)
{
    auto a=arrangementSnapshot();if(!a){songPlaying.store(false);return;}
    sonara::SongMixArray mix{};
    for(int i=0;i<12;++i){const auto m=getLaneMix(i);mix[(size_t)i]={m.level,m.pan,m.width,m.fxSend};}
    const auto start=songSample.load();
    songRenderer.render(*a,out,start,numSamples,&mix);
    const auto total=(int64_t)std::llround(a->getTotalBeats()*previewSampleRate*60.0/a->getBpm()+previewSampleRate*4.0);
    songSample.store(std::min<int64_t>(total,start+numSamples));
    if(start+numSamples>=total)songPlaying.store(false);
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
    auto timeSig=juce::MidiMessage::timeSignatureMetaEvent(4,4);timeSig.setTimeStamp(0);seq.addEvent(timeSig);
    for(const auto& section:a->getSections()){auto marker=juce::MidiMessage::textMetaEvent(6,section.name);marker.setTimeStamp(section.startBar*4.0*960);seq.addEvent(marker);}
    for (const auto& n : lane.notes)
    {
        auto on=juce::MidiMessage::noteOn(1,n.note,(juce::uint8)n.velocity); on.setTimeStamp(n.beat*960.0); seq.addEvent(on);
        auto off=juce::MidiMessage::noteOff(1,n.note); off.setTimeStamp((n.beat+n.length)*960.0); seq.addEvent(off);
    }
    auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(a->getTotalBeats()*960);seq.addEvent(end);
    seq.updateMatchedPairs(); mf.addTrack(seq); destination.deleteFile(); juce::FileOutputStream out(destination);
    return out.openedOk() && mf.writeTo(out);
}


bool SonaraAudioProcessor::writeLeadMidiFile(const juce::File& destination) const
{
    auto a=arrangementSnapshot(); if(!a)return false;
    const auto& lanes=a->getLanes();
    const auto it=std::find_if(lanes.begin(),lanes.end(),[](const auto& lane){return lane.name=="LEAD";});
    if(it==lanes.end()||it->notes.empty())return false;
    const auto& lane=*it;

    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    auto tempo=juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0/a->getBpm()));
    tempo.setTimeStamp(0);seq.addEvent(tempo);
    auto name=juce::MidiMessage::textMetaEvent(3,"LEAD");name.setTimeStamp(0);seq.addEvent(name);
    auto meter=juce::MidiMessage::timeSignatureMetaEvent(4,4);meter.setTimeStamp(0);seq.addEvent(meter);
    for(const auto& section:a->getSections()){auto marker=juce::MidiMessage::textMetaEvent(6,section.name);marker.setTimeStamp(section.startBar*4.0*960);seq.addEvent(marker);}
    for(const auto& n:lane.notes)
    {
        auto on=juce::MidiMessage::noteOn(1,n.note,(juce::uint8)n.velocity);
        auto off=juce::MidiMessage::noteOff(1,n.note);
        on.setTimeStamp(n.beat*960.0);off.setTimeStamp((n.beat+n.length)*960.0);
        seq.addEvent(on);seq.addEvent(off);
    }
    auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(a->getTotalBeats()*960);seq.addEvent(end);
    seq.updateMatchedPairs();mf.addTrack(seq);destination.deleteFile();juce::FileOutputStream out(destination);
    return out.openedOk()&&mf.writeTo(out);
}


bool SonaraAudioProcessor::analyseReferenceFile(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    stopPreview();stopSongPreview();
    reference={};referenceLoaded=false;referenceMelodyPreview=false;
    generationProgress.store(.05f);generationStatus="Analyzing reference audio";
    auto result=referenceAnalyzer.analyseAudio(file);
    if(!result.valid())
    {
        generationProgress.store(0.f);
        generationStatus=result.sampleRate>0.0
            ?"Reference analysis failed • no stable melody could be extracted"
            :"Reference file could not be read";
        return false;
    }
    reference=std::move(result);referenceLoaded=true;
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);
    generationStatus="Reference ready • "+reference.keyName+" • "+juce::String(reference.melody.size())+" stable melody notes";
    return true;
}

bool SonaraAudioProcessor::importMidiFile(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    stopPreview();stopSongPreview();
    reference={};referenceLoaded=false;referenceMelodyPreview=false;
    generationProgress.store(.1f);generationStatus="Importing MIDI";
    auto result=referenceAnalyzer.importMidi(file);
    if(!result.valid())
    {
        generationProgress.store(0.f);generationStatus="MIDI import failed • no usable note data";
        return false;
    }
    reference=std::move(result);referenceLoaded=true;referenceMelodyPreview=true;
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);generationStatus="MIDI imported • ready to RESOUND or REBUILD";
    return true;
}

bool SonaraAudioProcessor::resoundReference(const juce::String& prompt)
{
    const juce::ScopedLock operation(operationLock);
    if(!hasReference())
    {
        generationProgress.store(0.f);generationStatus="Load a usable reference audio or MIDI first";
        return false;
    }
    generatePatch(prompt+" reference melody resound instrument clean expressive");
    referenceMelodyPreview=true;
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    startPreview();
    generationProgress.store(1.f);
    generationStatus="RESOUND • playing extracted melody with new SONARA SoundDNA";
    return true;
}

bool SonaraAudioProcessor::rebuildInstrumentalFromReference(const juce::String& prompt)
{
    const juce::ScopedLock operation(operationLock);
    if(!hasReference())
    {
        generationProgress.store(0.f);generationStatus="Load a usable reference audio or MIDI first";
        return false;
    }

    stopPreview();stopSongPreview();
    referenceMelodyPreview=false;
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(.04f);
    generationStatus="REBUILD • creating a new backing arrangement around the extracted melody";

    // Put reference tempo/key first because the song parser intentionally treats
    // the first explicit tempo/key as authoritative. A contradictory text prompt
    // must not silently pull the rebuilt backing away from the analysed reference.
    const auto rebuildPrompt=juce::String(reference.estimatedBpm,1)+" BPM, "+reference.keyName
        +", "+prompt.trim()+", rebuild backing around reference hook, leave space for lead";
    if(!generateTrack(rebuildPrompt))
    {
        generationProgress.store(0.f);
        generationStatus="Reference rebuild failed • backing generation did not pass the quality gate";
        return false;
    }

    auto current=arrangementSnapshot();
    if(!current)
    {
        generationProgress.store(0.f);generationStatus="Reference rebuild failed • no new arrangement was produced";
        return false;
    }
    auto rebuilt=std::make_shared<sonara::SongArrangement>(*current);
    auto& lanes=rebuilt->editLanes();
    int leadIndex=-1;
    for(int i=0;i<(int)lanes.size();++i)if(lanes[(size_t)i].name=="LEAD"){leadIndex=i;break;}
    if(leadIndex<0)
    {
        generationProgress.store(0.f);generationStatus="Reference rebuild failed • generated arrangement has no LEAD lane";
        return false;
    }

    auto& lead=lanes[(size_t)leadIndex];
    lead.notes.clear();
    const double total=rebuilt->getTotalBeats();
    const double refStart=reference.melody.front().beat;
    const double loop=juce::jmax(4.0,reference.melodyBeats()-refStart);
    int inserted=0;

    const auto addReferenceRange=[&](double startBeat,double endBeat,float velocityScale,int stride)
    {
        if(endBeat<=startBeat)return;
        for(double base=startBeat;base<endBeat;base+=loop)
        {
            for(size_t i=0;i<reference.melody.size();i+=(size_t)juce::jmax(1,stride))
            {
                const auto& n=reference.melody[i];
                const double local=juce::jmax(0.0,n.beat-refStart);
                const double beat=base+local;
                if(beat>=endBeat)break;
                const double length=juce::jmax(.0625,juce::jmin(n.length,endBeat-beat));
                const int velocity=juce::jlimit(1,127,(int)std::lround(n.velocity*velocityScale));
                lead.notes.push_back({n.midiNote,velocity,beat,length});
                ++inserted;
            }
        }
    };

    // Preserve the analysed hook in the sections where listeners expect the main
    // identity, while using a lighter call/response version in verses. This avoids
    // the old behaviour of pasting the same reference loop across every section.
    for(const auto& section:rebuilt->getSections())
    {
        const double start=section.startBar*4.0;
        const double end=juce::jmin(total,(section.startBar+section.bars)*4.0);
        const auto name=section.name.toUpperCase();
        if(name=="CHORUS"||name.contains("DROP")||name=="FINAL HOOK")
            addReferenceRange(start,end,1.0f,1);
        else if(name=="VERSE")
            addReferenceRange(start,end,.78f,2);
    }
    if(inserted==0)addReferenceRange(0.0,total,1.0f,1);

    std::sort(lead.notes.begin(),lead.notes.end(),[](const sonara::ArrangementNote&a,const sonara::ArrangementNote&b){return a.beat<b.beat;});
    lead.sound=generator.generate(prompt+" clean expressive reference lead, controlled low end",
        (uint64_t)prompt.hashCode64()^(++generationCounter*0x94d049bb133111ebULL));
    applyLaneSoundCpuBudget("LEAD",lead.sound);

    

    storeArrangement(std::shared_ptr<const sonara::SongArrangement>(rebuilt));
    setSelectedLane(leadIndex);
    generationProgress.store(1.f);
    generationStatus="REFERENCE REBUILT • "+reference.keyName+" • "+juce::String(reference.estimatedBpm,1)
        +" BPM • "+juce::String(inserted)+" lead notes placed into song sections";
    return true;
}

bool SonaraAudioProcessor::writeReferenceMidiFile(const juce::File& file) const
{
    return referenceLoaded&&referenceAnalyzer.writeMelodyMidi(reference,file);
}

bool SonaraAudioProcessor::saveSound(const juce::File& file) const
{
    auto xml=engine.patchSnapshot().toValueTree().createXml();return xml&&file.replaceWithText(xml->toString());
}

bool SonaraAudioProcessor::loadSound(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);if(!xml)return false;auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||tree.getType().toString()!="SoundDNA")return false;setPatchWithHistory(sonara::SoundDNA::fromValueTree(tree));referenceMelodyPreview=false;generationStatus="Sound loaded";return true;
}

bool SonaraAudioProcessor::saveProject(const juce::File& file) const
{
    const juce::ScopedLock operation(operationLock);
    juce::ValueTree root("SONARA_PROJECT");
    root.setProperty("schema",4,nullptr);root.setProperty("promptDraft",getCurrentSongPrompt(),nullptr);
    root.setProperty("generationCounter",juce::String(generationCounter),nullptr);
    root.setProperty("bpm",previewBpm.load(),nullptr);
    root.setProperty("selectedLane",selectedLane.load(),nullptr);
    root.setProperty("laneMidiSound",useLaneMidiSound.load(),nullptr);
    root.setProperty("midiRoutingMode",(int)midiRoutingMode.load(),nullptr);
    root.addChild(engine.patchSnapshot().toValueTree(),-1,nullptr);
    root.addChild(locks.toValueTree(),-1,nullptr);
    root.addChild(makeLaneMixTree(*this),-1,nullptr);
    if(auto a=arrangementSnapshot())root.addChild(a->toValueTree(),-1,nullptr);
    if(hasReference())root.addChild(makeReferenceTree(reference,referenceMelodyPreview),-1,nullptr);
    auto xml=root.createXml();
    return xml&&file.replaceWithText(xml->toString());
}
bool SonaraAudioProcessor::loadProject(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    if(file.getSize()>64*1024*1024)return false;
    auto xml=juce::XmlDocument::parse(file);
    if(!xml)return false;
    auto root=juce::ValueTree::fromXml(*xml);
    if(!root.isValid()||root.getType().toString()!="SONARA_PROJECT")return false;
    auto dna=root.getChildWithName("SoundDNA");
    if(!dna.isValid())return false;

    stopPreview();stopSongPreview();
    setSongPromptDraft(root.getProperty("promptDraft","").toString());
    resetLaneMix(*this);
    reference={};referenceLoaded=false;referenceMelodyPreview=false;
    storeArrangement(std::shared_ptr<const sonara::SongArrangement>{});
    selectedLane.store(9);

    setPatchWithHistory(sonara::SoundDNA::fromValueTree(dna));
    auto lockTree=root.getChildWithName("MUTATION_LOCKS");
    locks=lockTree.isValid()?sonara::MutationLocks::fromValueTree(lockTree):sonara::MutationLocks{};
    applyLaneMixTree(*this,root.getChildWithName("LANE_MIX"));
    generationCounter=(uint64_t)std::max<juce::int64>(1,
        root.getProperty("generationCounter","1").toString().getLargeIntValue());
    setPreviewBpm((double)root.getProperty("bpm",128.0));

    const auto referenceTree=root.getChildWithName("REFERENCE_ANALYSIS");
    if(referenceTree.isValid())
    {
        bool preview=false;
        auto restoredReference=referenceFromTree(referenceTree,preview);
        if(restoredReference.valid())
        {
            reference=std::move(restoredReference);
            referenceLoaded=true;
            referenceMelodyPreview=preview;
        }
    }

    auto arr=root.getChildWithName("SONARA_ARRANGEMENT");
    if(arr.isValid())
    {
        auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));
        if(!made->validate()){generationStatus="Project load failed • invalid arrangement";generationProgress.store(0);return false;}
        if(!made->isEmpty())
        {
            const auto& lanes=made->getLanes();
                    
            storeArrangement(std::shared_ptr<const sonara::SongArrangement>(made));
            if(!root.hasProperty("promptDraft"))setSongPromptDraft(made->getSourcePrompt());
            setSelectedLane(juce::jlimit(0,(int)lanes.size()-1,(int)root.getProperty("selectedLane",9)));
            engine.setPatch(sonara::SoundDNA::fromValueTree(dna));
        }
    }

    setMidiRoutingMode((MidiRoutingMode)juce::jlimit(0,1,(int)root.getProperty("midiRoutingMode",0)));
    useLaneMidiSound.store((bool)root.getProperty("laneMidiSound",arrangementSnapshot()&&engine.patchSnapshot().seed==arrangementSnapshot()->getLanes()[(size_t)selectedLane.load()].sound.seed));
    generationStatus="Project loaded • song, sounds, mix and reference restored";
    generationProgress.store(1.f);
    return true;
}
bool SonaraAudioProcessor::exportFullMix(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
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
        *a,file,previewSampleRate,
        [this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;},
        &mix);

    generationProgress.store(ok?1.f:0.f);
    generationStatus=ok?"24-bit full mix ready • "+file.getFullPathName():"Full mix export failed";
    return ok;
}

bool SonaraAudioProcessor::exportSelectedLaneAudio(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    auto a=arrangementSnapshot();if(!a)return false;const auto mix=songMixSnapshot();const bool ok=audioExporter.renderSelectedLane(*a,selectedLane.load(),file,previewSampleRate,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;},&mix);generationProgress.store(ok?1.f:0.f);generationStatus=ok?"Selected lane WAV ready • "+file.getFullPathName():"Lane export failed";return ok;
}

bool SonaraAudioProcessor::exportLeadAudio(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    auto a=arrangementSnapshot();if(!a)return false;
    const auto& lanes=a->getLanes();
    int leadIndex=-1;
    for(int i=0;i<(int)lanes.size();++i)if(lanes[(size_t)i].name=="LEAD"){leadIndex=i;break;}
    if(leadIndex<0)return false;
    const auto mix=songMixSnapshot();
    const bool ok=audioExporter.renderSelectedLane(*a,leadIndex,file,previewSampleRate,
        [this](float x,const juce::String& status){generationProgress.store(x);generationStatus=status;},&mix);
    generationProgress.store(ok?1.f:0.f);
    generationStatus=ok?"LEAD WAV ready • "+file.getFullPathName():"LEAD WAV export failed";
    return ok;
}

bool SonaraAudioProcessor::exportReferenceAudio(const juce::File& file)
{
    const juce::ScopedLock operation(operationLock);
    if(!hasReference())
    {
        generationProgress.store(0.f);
        generationStatus="RESOUND WAV unavailable • load a usable reference first";
        return false;
    }

    generationProgress.store(.02f);
    generationStatus="Rendering RESOUND WAV";
    const bool ok=audioExporter.renderReferenceMelody(reference,engine.patchSnapshot(),file,previewSampleRate,
        [this](float x,const juce::String& status)
        {
            generationProgress.store(x);
            generationStatus=status;
        });
    generationProgress.store(ok?1.f:0.f);
    generationStatus=ok?"RESOUND WAV ready • "+file.getFullPathName():"RESOUND WAV export failed";
    return ok;
}

bool SonaraAudioProcessor::exportAllStems(const juce::File& directory)
{
    const juce::ScopedLock operation(operationLock);
    auto a=arrangementSnapshot();if(!a)return false;const auto mix=songMixSnapshot();
    const bool ok=audioExporter.renderAllStems(*a,directory,previewSampleRate,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;},&mix)
        &&saveProject(directory.getChildFile("SONARA-Stems.sonaraproject"))&&a->writeMidiFile(directory.getChildFile("SONARA-Arrangement.mid"));
    generationProgress.store(ok?1.f:0.f);generationStatus=ok?"All 24-bit stems + MIDI + SoundDNA project ready • "+directory.getFullPathName():"Stem export failed";return ok;
}

void SonaraAudioProcessor::setLaneMix(int laneIndex,LaneMixParameter parameter,float value) noexcept
{
    if(!juce::isPositiveAndBelow(laneIndex,12))return;
    if(!std::isfinite(value))value=parameter==LaneMixParameter::pan?0.f:1.f;
    const size_t i=(size_t)laneIndex;
    switch(parameter)
    {
        case LaneMixParameter::level:
            laneMixLevel[i].store(juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::pan:
            laneMixPan[i].store(laneIndex==5?0.f:juce::jlimit(-1.f,1.f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::width:
            laneMixWidth[i].store(laneIndex==5?0.f:juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::fxSend:
            laneMixFx[i].store(laneIndex==5?0.f:juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
    }
}

void SonaraAudioProcessor::setSelectedLane(int i)
{
    const juce::ScopedLock operation(operationLock);
    const int bounded=juce::jlimit(0,11,i);
    selectedLane.store(bounded);
    auto a=arrangementSnapshot();
    if(!a||!juce::isPositiveAndBelow(bounded,(int)a->getLanes().size()))return;

    const auto& lane=a->getLanes()[(size_t)bounded];
    {
        const juce::ScopedLock callback(getCallbackLock());
        hostMidiRenderer.reset(0,false);hostMidiSample=0;hostWasPlaying=false;expectedHostPpq=-1.0;
        useLaneMidiSound.store(true,std::memory_order_release);
    }
    if(!lane.drums)
    {
        // The selected arrangement sound also becomes the live SONARA instrument.
        // This lets editable FL Piano Roll MIDI play the same SoundDNA when the
        // MIDI is placed on a SONARA channel.
        engine.setPatch(lane.sound);
        generationStatus="LIVE SOUND • "+lane.name+" • drop lane MIDI on this SONARA channel";
    }
}

void SonaraAudioProcessor::setMidiRoutingMode(MidiRoutingMode mode)
{
    const juce::ScopedLock callback(getCallbackLock());
    midiRoutingMode.store(mode);hostMidiRenderer.reset(0,false);hostMidiSample=0;hostWasPlaying=false;expectedHostPpq=-1.0;
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

bool SonaraAudioProcessor::setSelectedLaneSound(const juce::String& prompt,bool automatic)
{
    const juce::ScopedLock operation(operationLock);
    auto current=arrangementSnapshot();
    if(!current)return false;
    const int laneIndex=selectedLane.load();
    if(!juce::isPositiveAndBelow(laneIndex,(int)current->getLanes().size()))return false;

    auto updated=std::make_shared<sonara::SongArrangement>(*current);
    auto& lanes=updated->editLanes();
    sonara::SoundDNA next;

    if(automatic)
    {
        std::vector<sonara::SoundDNA> preserved;
        preserved.reserve(lanes.size());
        for(const auto& lane:lanes)preserved.push_back(lane.sound);

        // Re-run SONARA's automatic palette intelligence, then keep only the
        // selected lane's newly fitted sound so all other manual choices survive.
        updated->finalizeSoundPalette();
        next=updated->getLanes()[(size_t)laneIndex].sound;
        for(size_t i=0;i<lanes.size();++i)
            if((int)i!=laneIndex)lanes[i].sound=preserved[i];
    }
    else
    {
        const auto userPrompt=prompt.trim();
        if(userPrompt.isEmpty())return false;
        const uint64_t seed=scrambleSongSeed(sessionSalt
            ^ (++generationCounter*0x9e3779b97f4a7c15ULL)
            ^ (uint64_t)userPrompt.hashCode64()
            ^ ((uint64_t)laneIndex+1ULL)*0xbf58476d1ce4e5b9ULL);
        next=generator.generate(userPrompt+" • "+lanes[(size_t)laneIndex].name+" instrument",seed);
        applyLaneSoundCpuBudget(lanes[(size_t)laneIndex].name,next);
        const auto semantic=next.name.replace("Generated ","").trim();
        next.name=lanes[(size_t)laneIndex].name+" • Custom "+(semantic.isEmpty()?juce::String("Sound"):semantic);
        lanes[(size_t)laneIndex].sound=next;
    }

    if(automatic)
        applyLaneSoundCpuBudget(lanes[(size_t)laneIndex].name,lanes[(size_t)laneIndex].sound);

    if(laneIndex>=4)engine.setPatch(lanes[(size_t)laneIndex].sound);

    storeArrangement(std::shared_ptr<const sonara::SongArrangement>(updated));
    generationProgress.store(1.f);
    generationStatus=juce::String(automatic?"AUTO FIT • ":"CUSTOM SOUND • ")+lanes[(size_t)laneIndex].name;
    return true;
}

void SonaraAudioProcessor::setMacro(Macro macro, float normalized)
{
    const juce::ScopedLock operation(operationLock);
    const float x = juce::jlimit(0.f,1.f,normalized); auto d = engine.patch();
    switch(macro)
    {
        case Macro::brightness: d.macroBrightness=x; d.cutoff=juce::jlimit(30.f,22000.f,80.f*std::pow(250.f,x)); break;
        case Macro::movement: d.macroMovement=x; d.lfoRate=.08f+7.92f*x*x; d.lfoCutoff=(x-.5f)*1.5f; d.lfoMorphA=(x-.5f)*1.4f; d.lfoMorphB=-(x-.5f)*1.2f; break;
        case Macro::space: d.macroSpace=x; d.chorus=juce::jlimit(0.f,1.f,x*.75f); d.reverb=juce::jlimit(0.f,1.f,x*.85f); d.delay=juce::jlimit(0.f,1.f,juce::jmax(0.f,(x-.18f)*.9f)); break;
        case Macro::impact: d.macroImpact=x; d.drive=juce::jlimit(0.f,1.f,.04f+x*.62f); d.subLevel=juce::jlimit(0.f,1.f,x*.52f); break;
    }
    const int lane=selectedLane.load();
    if(auto a=arrangementSnapshot();a&&juce::isPositiveAndBelow(lane,(int)a->getLanes().size()))
    {
        auto updated=std::make_shared<sonara::SongArrangement>(*a);applyLaneSoundCpuBudget(updated->getLanes()[(size_t)lane].name,d);
        updated->editLanes()[(size_t)lane].sound=d;storeArrangement(updated);
    }
    engine.setPatch(d); generationStatus = "Macro adjusted";
}

juce::String SonaraAudioProcessor::exportProjectForCyanoryx() const
{
    const juce::ScopedLock operation(operationLock);
    auto a=arrangementSnapshot();
    return cyanoryx.serializeInterchange(engine.patchSnapshot(), a ? a->toValueTree() : juce::ValueTree{}, previewBpm, referenceLoaded ? reference.keyName : juce::String("Unknown"));
}

bool SonaraAudioProcessor::importPatchFromCyanoryx(const juce::String& payload)
{
    sonara::SoundDNA imported; if(!cyanoryx.deserializePatch(payload,imported))return false;
    setPatchWithHistory(imported); generationProgress.store(1.f); generationStatus="Cyanoryx patch received"; return true;
}

void SonaraAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    const juce::ScopedLock operation(operationLock);
    juce::ValueTree state("SONARA_STATE");
    state.setProperty("schema",8,nullptr);state.setProperty("promptDraft",getCurrentSongPrompt(),nullptr);
    state.setProperty("generationCounter",juce::String(generationCounter),nullptr);
    state.setProperty("bpm",previewBpm.load(),nullptr);
    state.setProperty("selectedLane",selectedLane.load(),nullptr);
    state.setProperty("laneMidiSound",useLaneMidiSound.load(),nullptr);
    state.setProperty("midiRoutingMode",(int)midiRoutingMode.load(),nullptr);
    state.addChild(engine.patchSnapshot().toValueTree(),-1,nullptr);
    state.addChild(locks.toValueTree(),-1,nullptr);
    state.addChild(makeLaneMixTree(*this),-1,nullptr);
    if(auto a=arrangementSnapshot())state.addChild(a->toValueTree(),-1,nullptr);
    if(hasReference())state.addChild(makeReferenceTree(reference,referenceMelodyPreview),-1,nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,dest);
}

void SonaraAudioProcessor::setStateInformation(const void* data,int bytes)
{
    const juce::ScopedLock operation(operationLock);
    if(data==nullptr||bytes<=0||bytes>64*1024*1024)return;
    if(auto xml=getXmlFromBinary(data,bytes))
    {
        const auto state=juce::ValueTree::fromXml(*xml);if(!state.isValid())return;
        stopPreview();stopSongPreview();

        if(state.getType().toString()=="SoundDNA")
        {
            resetLaneMix(*this);
            reference={};referenceLoaded=false;referenceMelodyPreview=false;
            storeArrangement(std::shared_ptr<const sonara::SongArrangement>{});
            selectedLane.store(9);
            setPatchWithHistory(sonara::SoundDNA::fromValueTree(state));
            setSongPromptDraft({});locks={};generationCounter=1;generationStatus="Legacy preset restored";return;
        }
        if(state.getType().toString()!="SONARA_STATE")return;
        const auto dna=state.getChildWithName("SoundDNA");if(!dna.isValid())return;

        setSongPromptDraft(state.getProperty("promptDraft","").toString());
        resetLaneMix(*this);
        reference={};referenceLoaded=false;referenceMelodyPreview=false;
        storeArrangement(std::shared_ptr<const sonara::SongArrangement>{});
        selectedLane.store(9);

        engine.setPatch(sonara::SoundDNA::fromValueTree(dna));
        const auto lockState=state.getChildWithName("MUTATION_LOCKS");
        locks=lockState.isValid()?sonara::MutationLocks::fromValueTree(lockState):sonara::MutationLocks{};
        applyLaneMixTree(*this,state.getChildWithName("LANE_MIX"));
        generationCounter=(uint64_t)std::max<juce::int64>(1,
            state.getProperty("generationCounter","1").toString().getLargeIntValue());
        setPreviewBpm((double)state.getProperty("bpm",128.0));

        const auto referenceTree=state.getChildWithName("REFERENCE_ANALYSIS");
        if(referenceTree.isValid())
        {
            bool preview=false;
            auto restoredReference=referenceFromTree(referenceTree,preview);
            if(restoredReference.valid())
            {
                reference=std::move(restoredReference);
                referenceLoaded=true;
                referenceMelodyPreview=preview;
            }
        }

        auto arr=state.getChildWithName("SONARA_ARRANGEMENT");
        if(arr.isValid())
        {
            auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));
            if(!made->validate()){generationStatus="Session restore failed • invalid arrangement";generationProgress.store(0);return;}
            if(!made->isEmpty())
            {
                const auto& lanes=made->getLanes();
                            
                storeArrangement(std::shared_ptr<const sonara::SongArrangement>(made));
                if(!state.hasProperty("promptDraft"))setSongPromptDraft(made->getSourcePrompt());
                setSelectedLane(juce::jlimit(0,(int)lanes.size()-1,(int)state.getProperty("selectedLane",9)));
                engine.setPatch(sonara::SoundDNA::fromValueTree(dna));
            }
        }

        setMidiRoutingMode((MidiRoutingMode)juce::jlimit(0,1,(int)state.getProperty("midiRoutingMode",0)));
        useLaneMidiSound.store((bool)state.getProperty("laneMidiSound",arrangementSnapshot()&&engine.patchSnapshot().seed==arrangementSnapshot()->getLanes()[(size_t)selectedLane.load()].sound.seed));
        patchHistory.clear();
        patchHistory.push_back(engine.patch());
        historyIndex=0;
        generationProgress.store(1.f);
        generationStatus="Session restored • song, sounds, mix and reference";
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
