#include <JuceHeader.h>
#include "../Source/Generation/SongArrangement.h"
#include <cmath>
#include <iostream>
#include <set>
#include <map>
#include <cstdint>

namespace {
const sonara::ArrangementLane* findLane(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& lane:a.getLanes())if(lane.name==name)return &lane;
    return nullptr;
}

double structuralDifference(const sonara::ArrangementLane& a,const sonara::ArrangementLane& b)
{
    const int na=(int)a.notes.size(),nb=(int)b.notes.size();
    const int n=juce::jmin(96,juce::jmin(na,nb));
    int diff=std::abs(na-nb)>0?juce::jmin(24,std::abs(na-nb)):0;
    for(int i=0;i<n;++i)
    {
        const auto& x=a.notes[(size_t)i];
        const auto& y=b.notes[(size_t)i];
        if(x.note!=y.note||std::abs(x.beat-y.beat)>.01||std::abs(x.length-y.length)>.01)++diff;
    }
    return diff/(double)juce::jmax(1,n+juce::jmin(24,std::abs(na-nb)));
}

bool hasSection(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& s:a.getSections())if(s.name==name)return true;
    return false;
}

uint64_t leadFingerprintHash(const sonara::SongArrangement& a)
{
    const auto* lead=findLane(a,"LEAD");
    if(!lead)return 0;
    uint64_t h=1469598103934665603ULL;
    int prev=0;
    const int n=juce::jmin(128,(int)lead->notes.size());
    for(int i=0;i<n;++i)
    {
        const auto& note=lead->notes[(size_t)i];
        const int interval=i==0?0:juce::jlimit(-24,24,note.note-prev);
        const int values[]={
            ((note.note%12)+12)%12,
            interval+24,
            (int)std::llround(std::fmod(note.beat,16.0)*8.0),
            (int)std::llround(note.length*16.0),
            note.note/12
        };
        for(const int v:values){h^=(uint64_t)(v+257);h*=1099511628211ULL;}
        prev=note.note;
    }
    h^=(uint64_t)lead->notes.size();h*=1099511628211ULL;
    return h;
}

uint64_t vectorFingerprintHash(const std::vector<int>& values)
{
    uint64_t h=1469598103934665603ULL;
    for(const int value:values){h^=(uint64_t)(value+4099);h*=1099511628211ULL;}
    h^=(uint64_t)values.size();h*=1099511628211ULL;
    return h;
}

double melodySkeletonSimilarity(const sonara::ArrangementLane& a,const sonara::ArrangementLane& b)
{
    const int n=juce::jmin(96,juce::jmin((int)a.notes.size(),(int)b.notes.size()));
    if(n<8)return 0.0;

    double score=0.0;
    for(int i=1;i<n;++i)
    {
        const auto& ax=a.notes[(size_t)i];
        const auto& ap=a.notes[(size_t)i-1];
        const auto& bx=b.notes[(size_t)i];
        const auto& bp=b.notes[(size_t)i-1];

        const int ai=juce::jlimit(-12,12,ax.note-ap.note);
        const int bi=juce::jlimit(-12,12,bx.note-bp.note);
        const int ac=ai>0?1:(ai<0?-1:0);
        const int bc=bi>0?1:(bi<0?-1:0);
        const int ag=(int)std::llround((ax.beat-ap.beat)*8.0);
        const int bg=(int)std::llround((bx.beat-bp.beat)*8.0);
        const int al=(int)std::llround(ax.length*8.0);
        const int bl=(int)std::llround(bx.length*8.0);

        if(ai==bi)score+=.30;
        if(ac==bc)score+=.22;
        if(std::abs(ag-bg)<=1)score+=.30;
        if(std::abs(al-bl)<=1)score+=.18;
    }

    const double sequence=score/(double)juce::jmax(1,n-1);
    const double countRatio=(double)juce::jmin(a.notes.size(),b.notes.size())
                           /(double)juce::jmax<size_t>(1,juce::jmax(a.notes.size(),b.notes.size()));
    return juce::jlimit(0.0,1.0,sequence*(.86+.14*countRatio));
}

double twoBarMotifRhythmOverlap(const sonara::ArrangementLane& lead,const sonara::ArrangementSection& section)
{
    if(section.bars<4)return 0.0;
    std::set<int> first,second;
    const double start=section.startBar*4.0;
    const double split=start+8.0;
    const double end=start+16.0;
    for(const auto& n:lead.notes)
    {
        if(n.beat>=start&&n.beat<split)
            first.insert((int)std::llround((n.beat-start)*8.0));
        else if(n.beat>=split&&n.beat<end)
            second.insert((int)std::llround((n.beat-split)*8.0));
    }
    if(first.empty()||second.empty())return 0.0;
    int common=0;
    for(const int v:first)if(second.count(v)>0)++common;
    return common/(double)juce::jmax<size_t>(1,juce::jmin(first.size(),second.size()));
}

bool melodyQualityOk(const sonara::SongArrangement& song,const sonara::ArrangementLane& lead)
{
    if(lead.notes.size()<24||lead.notes.size()>700)return false;

    const int root=((song.getRootMidi()%12)+12)%12;
    const int minorScale[7]={0,2,3,5,7,8,10};
    const int majorScale[7]={0,2,4,5,7,9,11};
    const int* scale=song.isMinor()?minorScale:majorScale;

    bool allowed[12]{};
    for(int i=0;i<7;++i)allowed[(root+scale[i])%12]=true;

    int hugeLeaps=0;
    int repeatedRun=1;
    int maxRepeated=1;
    int minNote=127,maxNote=0;
    double totalLength=0.0;
    std::set<int> gapShapes;

    for(size_t i=0;i<lead.notes.size();++i)
    {
        const auto& n=lead.notes[i];
        if(!allowed[((n.note%12)+12)%12])return false;
        if(n.note<48||n.note>100)return false;
        if(n.length<.06||n.length>1.8)return false;
        minNote=juce::jmin(minNote,n.note);maxNote=juce::jmax(maxNote,n.note);
        totalLength+=n.length;

        if(i>0)
        {
            const auto& p=lead.notes[i-1];
            if(std::abs(n.note-p.note)>12)++hugeLeaps;
            repeatedRun=(n.note==p.note)?repeatedRun+1:1;
            maxRepeated=juce::jmax(maxRepeated,repeatedRun);
            gapShapes.insert((int)std::llround((n.beat-p.beat)*8.0));
        }
    }

    if(maxNote-minNote>48)return false;
    if(hugeLeaps>(int)lead.notes.size()/5)return false;
    if(maxRepeated>4)return false;
    if(gapShapes.size()<2)return false;

    const double avgLength=totalLength/(double)lead.notes.size();
    return avgLength>=.10&&avgLength<=1.25;
}

}

int main()
{
    const juce::String prompt="Emotional progressive house, 128 BPM, F minor, emotional memorable hook, huge melodic drop";
    sonara::SongArrangement a;
    a.generate(prompt,120.0,123456789ULL);

    if(a.getBars()<64||a.getBars()>96){std::cerr<<"Song length outside v1.1 production range\n";return 1;}
    if(std::abs(a.getBpm()-128.0)>.01){std::cerr<<"Prompt BPM was not applied\n";return 2;}
    if(a.getSections().size()!=8){std::cerr<<"Expected 8 named song sections\n";return 3;}

    const juce::String expectedSections[]={"INTRO","VERSE","BUILD","CHORUS","DROP","BREAKDOWN","BUILD 2","FINAL HOOK"};
    for(const auto& name:expectedSections)
        if(!hasSection(a,name)){std::cerr<<"Missing section "<<name<<"\n";return 4;}
    const auto& ordered=a.getSections();
    if(ordered[2].name!="BUILD"||ordered[3].name!="CHORUS"||ordered[4].name!="DROP"||ordered[5].name!="BREAKDOWN")
    {std::cerr<<"Required BUILD -> CHORUS -> DROP -> BREAKDOWN order missing\n";return 45;}

    if(a.getLanes().size()!=12){std::cerr<<"Expected exactly 12 required lanes\n";return 5;}
    size_t notes=0;bool hasDrums=false,hasLead=false;
    for(const auto& lane:a.getLanes()){notes+=lane.notes.size();hasDrums|=lane.drums;hasLead|=lane.name=="LEAD";}
    if(notes<420||!hasDrums||!hasLead){std::cerr<<"Arrangement too sparse or missing key lanes\n";return 6;}
    const auto* subLane=findLane(a,"SUB");
    const auto* bassLane=findLane(a,"BASS");
    if(!subLane||subLane->notes.empty()||!bassLane||bassLane->notes.empty())
    {std::cerr<<"Bass/Sub lane missing real MIDI\n";return 22;}

    const auto* kickLane=findLane(a,"KICK");
    const auto* snareLane=findLane(a,"SNARE / CLAP");
    const auto* firstLead=findLane(a,"LEAD");
    const sonara::ArrangementSection* dropSection=nullptr;
    for(const auto& s:a.getSections())if(s.name=="DROP"){dropSection=&s;break;}
    if(!kickLane||!snareLane||!firstLead||!dropSection)
    {std::cerr<<"Festival energy test missing lane/section\n";return 33;}

    int dropKickMax=0,dropSnareNotes=0;
    const double dropStart=dropSection->startBar*4.0;
    const double dropEnd=(dropSection->startBar+dropSection->bars)*4.0;
    for(const auto& n:kickLane->notes)if(n.beat>=dropStart&&n.beat<dropEnd)dropKickMax=juce::jmax(dropKickMax,n.velocity);
    for(const auto& n:snareLane->notes)if(n.beat>=dropStart&&n.beat<dropEnd)++dropSnareNotes;
    if(dropKickMax<120||dropSnareNotes<dropSection->bars*3)
    {std::cerr<<"Drop drums are not strong/layered enough\n";return 34;}

    if(twoBarMotifRhythmOverlap(*firstLead,*dropSection)<.45)
    {std::cerr<<"Drop lead lacks a repeating two-bar rhythmic motif\n";return 35;}

    const sonara::ArrangementSection *chorusSection=nullptr,*breakdownSection=nullptr;
    for(const auto& s:a.getSections())
    {
        if(s.name=="CHORUS")chorusSection=&s;
        else if(s.name=="BREAKDOWN")breakdownSection=&s;
    }
    if(!chorusSection||!breakdownSection
       ||chorusSection->startBar+chorusSection->bars!=dropSection->startBar
       ||dropSection->startBar+dropSection->bars!=breakdownSection->startBar)
    {std::cerr<<"CHORUS -> DROP -> BREAKDOWN boundaries are not contiguous\n";return 46;}

    auto maxNotesPerBar=[&](const sonara::ArrangementLane& lane,const sonara::ArrangementSection& section)
    {
        int maximum=0;
        for(int bar=section.startBar;bar<section.startBar+section.bars;++bar)
        {
            int count=0;const double begin=bar*4.0,end=begin+4.0;
            for(const auto& n:lane.notes)if(n.beat>=begin&&n.beat<end)++count;
            maximum=juce::jmax(maximum,count);
        }
        return maximum;
    };
    if(maxNotesPerBar(*firstLead,*chorusSection)>5||maxNotesPerBar(*firstLead,*dropSection)>5)
    {std::cerr<<"v1.1 main hook exceeded five lead notes per bar\n";return 47;}
    const auto* pluckLane=findLane(a,"PLUCK");
    if(!pluckLane||maxNotesPerBar(*pluckLane,*dropSection)>4)
    {std::cerr<<"v1.1 supporting pluck is over-filling the drop\n";return 48;}

    auto averageNotesPerBar=[&](const sonara::ArrangementLane& lane,const sonara::ArrangementSection& section)
    {
        int count=0;
        const double begin=section.startBar*4.0;
        const double end=(section.startBar+section.bars)*4.0;
        for(const auto& n:lane.notes)if(n.beat>=begin&&n.beat<end)++count;
        return count/(double)juce::jmax(1,section.bars);
    };

    const sonara::ArrangementSection *introSection=nullptr,*verseSection=nullptr,*buildSection=nullptr;
    for(const auto& s:a.getSections())
    {
        if(s.name=="INTRO")introSection=&s;
        else if(s.name=="VERSE")verseSection=&s;
        else if(s.name=="BUILD")buildSection=&s;
    }
    if(!introSection||!verseSection||!buildSection)
    {std::cerr<<"v1.3 songwriter section missing\n";return 49;}

    const double verseLeadDensity=averageNotesPerBar(*firstLead,*verseSection);
    const double buildLeadDensity=averageNotesPerBar(*firstLead,*buildSection);
    const double chorusLeadDensity=averageNotesPerBar(*firstLead,*chorusSection);
    const double dropLeadDensity=averageNotesPerBar(*firstLead,*dropSection);
    const double breakLeadDensity=averageNotesPerBar(*firstLead,*breakdownSection);

    if(!(chorusLeadDensity>verseLeadDensity+.35
         && dropLeadDensity>=chorusLeadDensity*.85
         && breakLeadDensity<chorusLeadDensity*.72
         && buildLeadDensity<chorusLeadDensity*.80))
    {
        std::cerr<<"v1.3 section melody hierarchy does not create verse/build/chorus/drop contrast\n";
        return 50;
    }

    int introLeadNotes=0,buildFirstHalfNotes=0;
    const double introStart=introSection->startBar*4.0;
    const double introEnd=(introSection->startBar+introSection->bars)*4.0;
    const double buildStart=buildSection->startBar*4.0;
    const double buildHalf=(buildSection->startBar+buildSection->bars/2)*4.0;
    for(const auto& n:firstLead->notes)
    {
        if(n.beat>=introStart&&n.beat<introEnd)++introLeadNotes;
        if(n.beat>=buildStart&&n.beat<buildHalf)++buildFirstHalfNotes;
    }
    if(introLeadNotes>4||buildFirstHalfNotes!=0)
    {std::cerr<<"v1.3 intro/build still behave like continuous full-melody sections\n";return 51;}

    // Chorus and drop must share a recognisable four-bar rhythmic hook.
    auto fourBarRhythm=[&](const sonara::ArrangementSection& section)
    {
        std::set<int> values;
        const double start=section.startBar*4.0;
        const double end=start+16.0;
        for(const auto& n:firstLead->notes)
            if(n.beat>=start&&n.beat<end)
                values.insert((int)std::llround((n.beat-start)*8.0));
        return values;
    };
    const auto chorusRhythm=fourBarRhythm(*chorusSection);
    const auto dropRhythm=fourBarRhythm(*dropSection);
    int sharedRhythm=0;
    for(const int v:chorusRhythm)if(dropRhythm.count(v)>0)++sharedRhythm;
    const double hookOverlap=sharedRhythm/(double)juce::jmax<size_t>(1,juce::jmin(chorusRhythm.size(),dropRhythm.size()));
    if(hookOverlap<.68)
    {std::cerr<<"v1.3 chorus/drop no longer share an audible hook rhythm\n";return 52;}

    // Real pre-drop drum tension: final chorus bar must contain an escalating roll,
    // then leave the final half-beat free of kick before the drop crash.
    const double lastChorusBar=(chorusSection->startBar+chorusSection->bars-1)*4.0;
    int preDropRollHits=0,preDropRollMaxVelocity=0;
    for(const auto& n:snareLane->notes)
        if(n.beat>=lastChorusBar+2.0&&n.beat<lastChorusBar+3.5)
        {
            ++preDropRollHits;
            preDropRollMaxVelocity=juce::jmax(preDropRollMaxVelocity,n.velocity);
        }
    bool lateKick=false;
    for(const auto& n:kickLane->notes)
        if(n.beat>=lastChorusBar+3.0&&n.beat<lastChorusBar+4.0)lateKick=true;
    if(preDropRollHits<7||preDropRollMaxVelocity<112||lateKick)
    {std::cerr<<"v1.3 chorus-to-drop drum roll/tension is missing or has no breathing gap\n";return 53;}

    auto midi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-arrangement-test",".mid");
    if(!a.writeMidiFile(midi)||!midi.existsAsFile()||midi.getSize()<512){std::cerr<<"MIDI export failed\n";return 7;}
    midi.deleteFile();

    sonara::SongArrangement fresh;
    fresh.generate(prompt,120.0,987654321ULL);

    bool structureChanged=false;
    if(a.getSections().size()==fresh.getSections().size())
        for(size_t i=0;i<a.getSections().size();++i)
            structureChanged|=a.getSections()[i].bars!=fresh.getSections()[i].bars;
    if(!structureChanged){std::cerr<<"Fresh seed did not change song structure\n";return 8;}

    const juce::String requiredFresh[]={
        "KICK","SNARE / CLAP","HATS","PERCUSSION","BASS","SUB","CHORDS","PLUCK","PAD","LEAD","COUNTER","FX / TRANSITIONS"
    };
    int substantiallyDifferent=0;
    for(const auto& name:requiredFresh)
    {
        const auto* first=findLane(a,name);
        const auto* second=findLane(fresh,name);
        if(!first||!second){std::cerr<<"Missing lane "<<name<<"\n";return 9;}
        if(first->sound.seed==second->sound.seed){std::cerr<<"SoundDNA seed did not change for "<<name<<"\n";return 10;}

        const double difference=structuralDifference(*first,*second);
        if(difference<=0.0){std::cerr<<"Lane stayed structurally identical: "<<name<<"\n";return 11;}
        if(difference>=.20)++substantiallyDifferent;
    }
    if(substantiallyDifferent<7){std::cerr<<"Too few lanes changed substantially across fresh song seed\n";return 12;}

    const auto* leadA=findLane(a,"LEAD");
    const auto* leadFresh=findLane(fresh,"LEAD");
    if(!melodyQualityOk(a,*leadA)||!melodyQualityOk(fresh,*leadFresh))
    {std::cerr<<"Generated lead failed musical quality constraints\n";return 24;}
    if(structuralDifference(*leadA,*leadFresh)<.55||melodySkeletonSimilarity(*leadA,*leadFresh)>.68){std::cerr<<"Lead phrase family still too similar\n";return 13;}

    // TEST A: same prompt, 10 seeds, every lead pair must be materially different.
    const uint64_t freshnessSeeds[]={10101ULL,20202ULL,30303ULL,40404ULL,50505ULL,60606ULL,70707ULL,80808ULL,90909ULL,100010ULL};
    std::array<sonara::SongArrangement,10> variants;
    for(size_t i=0;i<variants.size();++i)variants[i].generate(prompt,120.0,freshnessSeeds[i]);
    for(size_t i=0;i<variants.size();++i)
        for(size_t j=i+1;j<variants.size();++j)
        {
            const auto* li=findLane(variants[i],"LEAD");
            const auto* lj=findLane(variants[j],"LEAD");
            if(!li||!lj){std::cerr<<"Freshness variant missing lead\n";return 14;}
            if(structuralDifference(*li,*lj)<.42||melodySkeletonSimilarity(*li,*lj)>.72)
            {
                std::cerr<<"Same-prompt melody variants reused a rhythm/contour skeleton\n";
                return 15;
            }
            if(!melodyQualityOk(variants[i],*li)||!melodyQualityOk(variants[j],*lj))
            {
                std::cerr<<"Fresh melody variant failed quality constraints\n";
                return 25;
            }
        }

    // TEST C: repeated exact-prompt songs may not collapse to one normalized
    // melody or one dominant Roman-numeral/harmonic-rhythm progression.
    std::set<uint64_t> fingerprints;
    std::set<uint64_t> harmonyFingerprints;
    std::map<uint64_t,int> progressionDistribution;
    for(uint64_t i=0;i<30;++i)
    {
        sonara::SongArrangement x;
        x.generate(prompt,120.0,0xabc000ULL+i*0x10203ULL);
        const auto hash=leadFingerprintHash(x);
        if(hash==0||!fingerprints.insert(hash).second)
        {std::cerr<<"Duplicate lead fingerprint in 30-song run\n";return 21;}
        const auto harmonyHash=vectorFingerprintHash(x.getHarmonyFingerprint());
        const auto progressionHash=vectorFingerprintHash(x.getProgressionFingerprint());
        if(harmonyHash==0||!harmonyFingerprints.insert(harmonyHash).second)
        {std::cerr<<"Duplicate normalized Harmony DNA in 30-song run\n";return 26;}
        ++progressionDistribution[progressionHash];
    }
    for(const auto& entry:progressionDistribution)
        if(entry.second>9)
        {std::cerr<<"One normalized progression dominated more than 30% of same-prompt songs\n";return 27;}

    // Transposing an otherwise identical generation must not fool the novelty
    // detector: normalized harmony and melody identities remain equivalent.
    sonara::SongArrangement fMinor,cMinor;
    fMinor.generateComposition("emotional progressive house 128 BPM F minor",128.0,0x778899ULL);
    cMinor.generateComposition("emotional progressive house 128 BPM C minor",128.0,0x778899ULL);
    if(fMinor.getHarmonyFingerprint()!=cMinor.getHarmonyFingerprint())
    {std::cerr<<"Key transposition changed normalized harmony identity\n";return 28;}
    if(fMinor.getMelodyFingerprint()!=cMinor.getMelodyFingerprint())
    {std::cerr<<"Key transposition changed normalized melody identity\n";return 32;}

    // Same key/BPM/genre: variation must come from composition, not transposition.
    std::set<uint64_t> fixedHarmony,fixedMelody,fixedBass,fixedDrums,fixedPluck;
    for(uint64_t i=0;i<10;++i)
    {
        sonara::SongArrangement x;
        x.generateComposition(prompt,128.0,0x990000ULL+i*0x314159ULL);
        fixedHarmony.insert(vectorFingerprintHash(x.getHarmonyFingerprint()));
        fixedMelody.insert(vectorFingerprintHash(x.getMelodyFingerprint()));
        const auto* bass=findLane(x,"BASS");const auto* drums=findLane(x,"KICK");const auto* pluck=findLane(x,"PLUCK");
        if(!bass||!drums||!pluck){std::cerr<<"Fixed-context composition lane missing\n";return 29;}
        auto laneHash=[](const sonara::ArrangementLane& lane)
        {
            uint64_t h=1469598103934665603ULL;
            for(const auto& note:lane.notes)
            {
                const int values[]={note.note,(int)std::llround(note.beat*8.0),(int)std::llround(note.length*16.0)};
                for(const int value:values){h^=(uint64_t)(value+257);h*=1099511628211ULL;}
            }
            return h;
        };
        fixedBass.insert(laneHash(*bass));fixedDrums.insert(laneHash(*drums));fixedPluck.insert(laneHash(*pluck));
    }
    if(fixedHarmony.size()<8||fixedMelody.size()!=10||fixedBass.size()<7||fixedDrums.size()<6||fixedPluck.size()<7)
    {std::cerr<<"Same-key/BPM/genre variation did not reach every composition subsystem\n";return 30;}

    // TEST B: genre language must alter melody grammar, not only SoundDNA.
    std::array<sonara::SongArrangement,4> genreSongs;
    const juce::String genrePrompts[4]={
        "dark minimal tech house 126 BPM F minor sparse short hook",
        "emotional progressive house 126 BPM F minor memorable evolving hook",
        "cinematic emotional EDM 126 BPM F minor long expressive melody",
        "drum and bass 126 BPM F minor fast syncopated call response melody"
    };
    for(size_t i=0;i<genreSongs.size();++i)genreSongs[i].generate(genrePrompts[i],126.0,0x5555ULL);

    std::array<const sonara::ArrangementLane*,4> genreLeads{};
    for(size_t i=0;i<genreSongs.size();++i)
    {
        genreLeads[i]=findLane(genreSongs[i],"LEAD");
        if(!genreLeads[i]){std::cerr<<"Genre test missing lead\n";return 16;}
    }
    for(size_t i=0;i<genreLeads.size();++i)
        for(size_t j=i+1;j<genreLeads.size();++j)
            if(structuralDifference(*genreLeads[i],*genreLeads[j])<.20)
            {std::cerr<<"Genre prompts did not alter melody grammar\n";return 17;}

    auto avgLength=[](const sonara::ArrangementLane& lane)
    {
        double sum=0.0;for(const auto& n:lane.notes)sum+=n.length;
        return lane.notes.empty()?0.0:sum/(double)lane.notes.size();
    };
    if(!(avgLength(*genreLeads[2])>avgLength(*genreLeads[0])*1.25))
    {std::cerr<<"Cinematic/tech melody articulation not distinct enough\n";return 23;}


    // v0.8 production-intent acceptance: explicit arrangement language must
    // influence structure and all support lanes should behave like one production.
    sonara::SongArrangement festivalSmart;
    festivalSmart.generate("huge festival mainstage progressive house 128 BPM F minor memorable anthem",128.0,0x880011ULL);
    const sonara::ArrangementSection* festBuild=nullptr;
    const sonara::ArrangementSection* festChorus=nullptr;
    const sonara::ArrangementSection* festDrop=nullptr;
    const sonara::ArrangementSection* festFinal=nullptr;
    for(const auto& s:festivalSmart.getSections())
    {
        if(s.name=="BUILD")festBuild=&s;
        else if(s.name=="CHORUS")festChorus=&s;
        else if(s.name=="DROP")festDrop=&s;
        else if(s.name=="FINAL HOOK")festFinal=&s;
    }
    if(!festBuild||!festChorus||!festDrop||!festFinal||festBuild->bars<8||festChorus->bars<8||festDrop->bars<16||festFinal->bars<12)
    {std::cerr<<"Festival production intent did not reshape song structure\n";return 36;}

    const auto* festBass=findLane(festivalSmart,"BASS");
    const auto* festPluck=findLane(festivalSmart,"PLUCK");
    const auto* festLead=findLane(festivalSmart,"LEAD");
    const auto* festChords=findLane(festivalSmart,"CHORDS");
    if(!festBass||!festPluck||!festLead||!festChords)
    {std::cerr<<"Festival smart-production lane missing\n";return 37;}
    if(twoBarMotifRhythmOverlap(*festBass,*festDrop)<.70||twoBarMotifRhythmOverlap(*festPluck,*festDrop)<.70)
    {std::cerr<<"Support lanes do not preserve coherent two-bar production motifs\n";return 38;}
    if(!festLead->sound.name.containsIgnoreCase("Festival Lead")
       ||!festChords->sound.name.containsIgnoreCase("Festival Chords")
       ||!festPluck->sound.name.containsIgnoreCase("Festival Pluck"))
    {std::cerr<<"Finalized SoundDNA palette lost semantic role identity\n";return 39;}

    const auto* festKick=findLane(festivalSmart,"KICK");
    const auto* festHats=findLane(festivalSmart,"HATS");
    const auto* festSub=findLane(festivalSmart,"SUB");
    if(!festKick||!festHats||!festSub)
    {std::cerr<<"v0.9 pre-drop lane missing\n";return 42;}
    const double finalChorusBar=(festChorus->startBar+festChorus->bars-1)*4.0;
    auto hasLateStart=[&](const sonara::ArrangementLane& lane,double from)
    {
        for(const auto& n:lane.notes)
            if(n.beat>=from&&n.beat<finalChorusBar+4.0)return true;
        return false;
    };
    if(hasLateStart(*festKick,finalChorusBar+3.0)
       ||hasLateStart(*festHats,finalChorusBar+3.5)
       ||hasLateStart(*festBass,finalChorusBar+3.5)
       ||hasLateStart(*festSub,finalChorusBar+3.5)
       ||hasLateStart(*festPluck,finalChorusBar+3.5)
       ||hasLateStart(*festLead,finalChorusBar+3.5))
    {std::cerr<<"v1.1 CHORUS-to-DROP breathing window was filled by late notes\n";return 43;}
    for(const auto& n:festSub->notes)
        if(n.beat<finalChorusBar+3.5&&n.beat+n.length>finalChorusBar+3.50&&n.beat>=finalChorusBar)
        {std::cerr<<"v1.1 sub tail spills across CHORUS-to-DROP gap\n";return 44;}

    sonara::SongArrangement earlyDrop;
    earlyDrop.generateComposition("emotional house 128 BPM F minor early drop",128.0,0x880022ULL);
    const sonara::ArrangementSection *earlyIntro=nullptr,*earlyVerse=nullptr,*earlyBuild=nullptr;
    for(const auto& s:earlyDrop.getSections())
    {
        if(s.name=="INTRO")earlyIntro=&s;
        else if(s.name=="VERSE")earlyVerse=&s;
        else if(s.name=="BUILD")earlyBuild=&s;
    }
    if(!earlyIntro||!earlyVerse||!earlyBuild||earlyIntro->bars>4||earlyVerse->bars>4||earlyBuild->bars>4)
    {std::cerr<<"Early-drop prompt did not shorten pre-drop structure\n";return 40;}

    sonara::SongArrangement longBuild;
    longBuild.generateComposition("progressive house 128 BPM F minor long build",128.0,0x880033ULL);
    const sonara::ArrangementSection* longBuildSection=nullptr;
    for(const auto& s:longBuild.getSections())if(s.name=="BUILD"){longBuildSection=&s;break;}
    if(!longBuildSection||longBuildSection->bars<12)
    {std::cerr<<"Long-build prompt did not extend build section\n";return 41;}

    sonara::SongArrangement deterministic;
    deterministic.generate(prompt,120.0,123456789ULL);
    if(deterministic.getLanes().size()!=a.getLanes().size()){std::cerr<<"Determinism lane count failed\n";return 18;}
    for(size_t i=0;i<a.getLanes().size();++i)
        if(deterministic.getLanes()[i].notes.size()!=a.getLanes()[i].notes.size())
        {std::cerr<<"Determinism note count failed\n";return 19;}

    const auto restored=sonara::SongArrangement::fromValueTree(a.toValueTree());
    if(restored.getSongId()!=a.getSongId()||restored.getHarmonyId()!=a.getHarmonyId()
       ||restored.getMelodyId()!=a.getMelodyId()
       ||restored.getHarmonyFingerprint()!=a.getHarmonyFingerprint()
       ||restored.getMelodyFingerprint()!=a.getMelodyFingerprint())
    {std::cerr<<"Composition/Harmony DNA persistence roundtrip failed\n";return 31;}

    sonara::SongArrangement dnb;
    dnb.generate("energetic drum and bass 174 BPM D minor fast aggressive",128.0,4567ULL);
    if(std::abs(dnb.getBpm()-174.0)>.01){std::cerr<<"174 BPM prompt parse failed\n";return 20;}

    std::cout<<"SONARA structure + full-lane freshness + phrase-family + BPM tests passed\n";
    return 0;
}
