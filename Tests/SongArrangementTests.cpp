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

    // v1.5: no isolated octave/pitch spike may survive between two nearby notes.
    int isolatedPitchSpikes=0;
    for(size_t i=1;i+1<firstLead->notes.size();++i)
    {
        const auto& prev=firstLead->notes[i-1];
        const auto& cur=firstLead->notes[i];
        const auto& next=firstLead->notes[i+1];
        if(std::abs(cur.note-prev.note)>7&&std::abs(cur.note-next.note)>7
           &&std::abs(prev.note-next.note)<=5&&cur.length<=.55)
            ++isolatedPitchSpikes;
    }
    if(isolatedPitchSpikes!=0)
    {std::cerr<<"v1.5 lead still contains isolated random high/low notes\n";return 54;}

    // v2: CHORUS and DROP must share a recognizable identity without being one
    // endless four-bar clone. The DROP is a tighter driving variation built from
    // the chorus opening cell.
    auto hookCells=[&](const sonara::ArrangementSection& section,double beats)
    {
        std::vector<std::pair<int,int>> cells;
        const double begin=section.startBar*4.0,end=begin+beats;
        for(const auto& n:firstLead->notes)
            if(n.beat>=begin&&n.beat<end)
                cells.push_back({(int)std::llround((n.beat-begin)*8.0),n.note});
        return cells;
    };
    const auto chorusOpening=hookCells(*chorusSection,8.0);
    const auto dropOpening=hookCells(*dropSection,8.0);
    const auto chorusFull=hookCells(*chorusSection,16.0);
    const auto dropFull=hookCells(*dropSection,16.0);
    if(chorusOpening.size()<4||chorusOpening!=dropOpening||chorusFull==dropFull)
    {std::cerr<<"v2 chorus/drop identity is either lost or still an exact clone\n";return 55;}

    // Explicit songwriter controls must affect real generation.
    sonara::SongArrangement directed;
    directed.generateComposition(
        "progressive house 128 BPM F minor, strong hook, simple melody no random notes, short intro, big chorus, long drop, short breakdown, no counter melody",
        128.0,0x151515ULL);
    const sonara::ArrangementSection *dIntro=nullptr,*dChorus=nullptr,*dDrop=nullptr,*dBreak=nullptr;
    for(const auto& s:directed.getSections())
    {
        if(s.name=="INTRO")dIntro=&s;
        else if(s.name=="CHORUS")dChorus=&s;
        else if(s.name=="DROP")dDrop=&s;
        else if(s.name=="BREAKDOWN")dBreak=&s;
    }
    const auto* dCounter=findLane(directed,"COUNTER");
    if(!dIntro||!dChorus||!dDrop||!dBreak||!dCounter
       ||dIntro->bars>4||dChorus->bars<12||dDrop->bars<20||dBreak->bars>4
       ||!dCounter->notes.empty())
    {std::cerr<<"v1.5 songwriter prompt directions are not controlling the arrangement\n";return 56;}

    const auto* dLead=findLane(directed,"LEAD");
    if(!dLead||!melodyQualityOk(directed,*dLead))
    {std::cerr<<"v1.5 directed simple melody failed musical quality validation\n";return 57;}

    // v1.6 role-pure lanes: support instruments must stay in their musical job
    // and register instead of scattering unrelated notes over the whole piano roll.
    sonara::SongArrangement rolePure;
    rolePure.generateComposition(
        "progressive house 128 BPM F minor, strong hook, simple melody, more space, powerful drop",
        128.0,0x161616ULL);

    const auto* rpChords=findLane(rolePure,"CHORDS");
    const auto* rpPluck=findLane(rolePure,"PLUCK");
    const auto* rpBass=findLane(rolePure,"BASS");
    const auto* rpSub=findLane(rolePure,"SUB");
    const auto* rpPad=findLane(rolePure,"PAD");
    if(!rpChords||!rpPluck||!rpBass||!rpSub||!rpPad)
    {std::cerr<<"v1.6 role-pure support lane missing\n";return 58;}

    auto laneRange=[&](const sonara::ArrangementLane& lane)
    {
        int lo=127,hi=0;
        for(const auto& n:lane.notes){lo=juce::jmin(lo,n.note);hi=juce::jmax(hi,n.note);}
        if(lane.notes.empty())return std::pair<int,int>{0,0};
        return std::pair<int,int>{lo,hi};
    };
    const auto chordRange=laneRange(*rpChords);
    const auto pluckRange=laneRange(*rpPluck);
    const auto bassRange=laneRange(*rpBass);
    const auto subRange=laneRange(*rpSub);
    const auto padRange=laneRange(*rpPad);

    if(chordRange.first<45||chordRange.second>76||chordRange.second-chordRange.first>31)
    {std::cerr<<"v1.6 CHORDS escaped close chord register\n";return 59;}
    if(pluckRange.first<60||pluckRange.second>79||pluckRange.second-pluckRange.first>19)
    {std::cerr<<"v1.6 PLUCK escaped narrow support register\n";return 60;}
    if(bassRange.first<32||bassRange.second>52)
    {std::cerr<<"v1.6 BASS escaped root/fifth register\n";return 61;}
    if(subRange.first<24||subRange.second>47)
    {std::cerr<<"v1.6 SUB escaped fundamental register\n";return 62;}
    if(!rpPad->notes.empty()&&(padRange.first<55||padRange.second>81))
    {std::cerr<<"v1.6 PAD escaped slow upper-harmony register\n";return 63;}

    // CHORDS should appear as grouped chord blocks: every onset carries at least
    // a triad, rather than isolated single notes pretending to be harmony.
    std::map<int,int> chordOnsets;
    for(const auto& n:rpChords->notes)
        ++chordOnsets[(int)std::llround(n.beat*16.0)];
    for(const auto& [onset,count]:chordOnsets)
        if(count<3||count>4)
        {std::cerr<<"v1.6 CHORDS contains isolated/non-chord note events\n";return 64;}

    // PLUCK is support, not another melody: never more than three note onsets/bar.
    int maxPluckOnsets=0;
    for(int bar=0;bar<rolePure.getBars();++bar)
    {
        int count=0;const double begin=bar*4.0,end=begin+4.0;
        for(const auto& n:rpPluck->notes)if(n.beat>=begin&&n.beat<end)++count;
        maxPluckOnsets=juce::jmax(maxPluckOnsets,count);
    }
    if(maxPluckOnsets>3)
    {std::cerr<<"v1.6 PLUCK is still over-filling bars\n";return 65;}

    auto maxDistinctOnsetsPerBar=[&](const sonara::ArrangementLane& lane)
    {
        int maximum=0;
        for(int bar=0;bar<rolePure.getBars();++bar)
        {
            std::set<int> onsets;
            const double begin=bar*4.0,end=begin+4.0;
            for(const auto& n:lane.notes)
                if(n.beat>=begin&&n.beat<end)
                    onsets.insert((int)std::llround((n.beat-begin)*16.0));
            maximum=juce::jmax(maximum,(int)onsets.size());
        }
        return maximum;
    };
    if(maxDistinctOnsetsPerBar(*rpChords)>2)
    {std::cerr<<"v1.6 CHORDS re-trigger more than twice per bar\n";return 66;}
    if(maxDistinctOnsetsPerBar(*rpPad)>2)
    {std::cerr<<"v1.6 PAD re-trigger more than twice per bar\n";return 67;}

    const auto* rpLead=findLane(rolePure,"LEAD");
    if(!rpLead){std::cerr<<"v1.7 shared-register test missing LEAD\n";return 68;}

    // v1.7 mainstream support is deliberately sparse.
    if(maxDistinctOnsetsPerBar(*rpChords)>1)
    {std::cerr<<"v1.7 CHORDS must be one sustained chord block per bar\n";return 69;}
    if(maxDistinctOnsetsPerBar(*rpPluck)>2)
    {std::cerr<<"v1.7 PLUCK exceeded two support onsets per bar\n";return 70;}
    if(maxDistinctOnsetsPerBar(*rpSub)>1)
    {std::cerr<<"v1.7 SUB exceeded one root onset per bar\n";return 71;}
    if(maxDistinctOnsetsPerBar(*rpBass)>2)
    {std::cerr<<"v1.7 BASS exceeded two support onsets per bar\n";return 72;}

    const sonara::ArrangementSection* rpChorusForGap=nullptr;
    for(const auto& s:rolePure.getSections())if(s.name=="CHORUS"){rpChorusForGap=&s;break;}
    for(const auto& n:rpChords->notes)
    {
        const int bar=(int)std::floor(n.beat/4.0);
        const bool intentionalPreDropGap=rpChorusForGap
            && bar==rpChorusForGap->startBar+rpChorusForGap->bars-1;
        const double minimumLength=intentionalPreDropGap?2.90:3.20;
        if(n.length<minimumLength)
        {std::cerr<<"v2 CHORDS contains short/retrigger-style notes outside the pre-drop gap\n";return 73;}
    }

    // Pitched support lanes must live in the same octave family as the lead.
    // Bass/Sub are intentionally excluded because they own the low end.
    auto averagePitchInBar=[&](const sonara::ArrangementLane& lane,int bar,bool& found)
    {
        double total=0.0;int count=0;
        const double begin=bar*4.0,end=begin+4.0;
        for(const auto& n:lane.notes)
            if(n.beat>=begin&&n.beat<end){total+=n.note;++count;}
        found=count>0;
        return count>0?total/count:0.0;
    };
    for(int bar=0;bar<rolePure.getBars();++bar)
    {
        bool hasLead=false,hasChord=false,hasPluck=false,hasPad=false;
        const double leadCentre=averagePitchInBar(*rpLead,bar,hasLead);
        const double chordCentre=averagePitchInBar(*rpChords,bar,hasChord);
        const double pluckCentre=averagePitchInBar(*rpPluck,bar,hasPluck);
        const double padCentre=averagePitchInBar(*rpPad,bar,hasPad);
        if(!hasLead)continue;

        if(hasChord&&std::abs(chordCentre-leadCentre)>11.5)
        {std::cerr<<"v1.7 CHORDS octave/register is detached from LEAD\n";return 74;}
        if(hasPluck&&std::abs(pluckCentre-leadCentre)>11.5)
        {std::cerr<<"v1.7 PLUCK octave/register is detached from LEAD\n";return 75;}
        if(hasPad&&std::abs(padCentre-leadCentre)>11.5)
        {std::cerr<<"v1.7 PAD octave/register is detached from LEAD\n";return 76;}
    }

    // v1.8 melody clarity: mainstream lead stays in a readable register,
    // hook density stays simple, and the first DROP has an unmistakable boundary.
    for(const auto& n:rpLead->notes)
        if(n.note<58)
        {std::cerr<<"v1.8 LEAD contains an unwanted low-register note\n";return 77;}

    const sonara::ArrangementSection *rpChorusSection=nullptr,*rpDropSection=nullptr,*rpFinalSection=nullptr;
    for(const auto& section:rolePure.getSections())
    {
        if(section.name=="CHORUS")rpChorusSection=&section;
        else if(section.name=="DROP")rpDropSection=&section;
        else if(section.name=="FINAL HOOK")rpFinalSection=&section;
    }
    if(!rpChorusSection||!rpDropSection||!rpFinalSection)
    {std::cerr<<"v1.8 melody-clarity sections missing\n";return 78;}

    for(const auto* section:{rpChorusSection,rpDropSection,rpFinalSection})
        for(int bar=section->startBar;bar<section->startBar+section->bars;++bar)
        {
            int noteCount=0;
            const double begin=bar*4.0,end=begin+4.0;
            for(const auto& n:rpLead->notes)if(n.beat>=begin&&n.beat<end)++noteCount;
            if(noteCount>3)
            {std::cerr<<"v1.8 hook melody is still too dense\n";return 79;}
        }

    const double chorusEnd=(rpChorusSection->startBar+rpChorusSection->bars)*4.0;
    for(const auto& n:rpLead->notes)
        if(n.beat>=chorusEnd-1.0&&n.beat<chorusEnd)
        {std::cerr<<"v1.8 chorus did not leave a full pre-drop beat\n";return 80;}

    const double rpDropStart=rpDropSection->startBar*4.0;
    bool dropAnchor=false;
    for(const auto& n:rpLead->notes)
        if(n.beat>=rpDropStart&&n.beat<=rpDropStart+.05){dropAnchor=true;break;}
    if(!dropAnchor)
    {std::cerr<<"v1.8 DROP is missing a downbeat melody anchor\n";return 81;}

    // v2.2 phrase engine: the planned motif budget must reach the rendered
    // melody, and a four-bar hook must be recognizable A/A' rather than four
    // unrelated bars or an exact two-bar copy repeated forever.
    sonara::SongArrangement phraseEngine;
    phraseEngine.generateComposition(
        "progressive house 128 BPM F minor, strong hook, short hook, simple melody, no random notes",
        128.0,0x220022ULL);
    const auto* phraseLead=findLane(phraseEngine,"LEAD");
    const sonara::ArrangementSection* phraseChorus=nullptr;
    for(const auto& section:phraseEngine.getSections())
        if(section.name=="CHORUS"){phraseChorus=&section;break;}
    if(!phraseLead||!phraseChorus||phraseChorus->bars<4)
    {std::cerr<<"v2.2 phrase-engine fixture is incomplete\n";return 82;}

    std::array<std::vector<int>,4> phraseRhythm;
    std::array<std::vector<int>,4> phrasePitch;
    for(int phraseBar=0;phraseBar<4;++phraseBar)
    {
        const double begin=(phraseChorus->startBar+phraseBar)*4.0;
        for(const auto& n:phraseLead->notes)
            if(n.beat>=begin&&n.beat<begin+4.0)
            {
                phraseRhythm[(size_t)phraseBar].push_back((int)std::llround((n.beat-begin)*8.0));
                phrasePitch[(size_t)phraseBar].push_back(n.note);
            }
        if(phraseRhythm[(size_t)phraseBar].size()<2||phraseRhythm[(size_t)phraseBar].size()>3)
        {std::cerr<<"v2.2 short hook ignored its two-to-three note bar budget\n";return 83;}
    }

    const bool answerRhythmRelated=phraseRhythm[1].size()==phraseRhythm[3].size()
        &&!phraseRhythm[1].empty()&&phraseRhythm[1].front()==phraseRhythm[3].front();
    if(phraseRhythm[0]!=phraseRhythm[2]||!answerRhythmRelated)
    {std::cerr<<"v2.2 A/A' phrase lost its stable two-bar rhythm identity\n";return 84;}
    if(phrasePitch[0]==phrasePitch[2]&&phrasePitch[1]==phrasePitch[3])
    {std::cerr<<"v2.2 four-bar phrase is still an exact two-bar pitch clone\n";return 85;}

    const double phraseEnd=(phraseChorus->startBar+4)*4.0;
    bool cadencePresent=false;
    for(const auto& n:phraseLead->notes)
        if(n.beat>=phraseEnd-1.0&&n.beat<phraseEnd){cadencePresent=true;break;}
    if(!cadencePresent)
    {std::cerr<<"v2.2 four-bar phrase lost its answer cadence to a random rest\n";return 86;}

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
        const bool intentionallyEmptyCounter=name=="COUNTER"&&first->notes.empty()&&second->notes.empty();
        if(difference<=0.0&&!intentionallyEmptyCounter)
        {std::cerr<<"Lane stayed structurally identical: "<<name<<"\n";return 11;}
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

    // v2.3: a hook moved by a shorter intro/build is still the same perceptual
    // melody. Novelty must not be fabricated from absolute timeline position or
    // from different numbers of earlier teaser notes.
    sonara::SongArrangement normalArrival,earlyArrival;
    normalArrival.generateComposition(
        "emotional progressive house 128 BPM F minor strong hook",
        128.0,0x230023ULL);
    earlyArrival.generateComposition(
        "emotional progressive house 128 BPM F minor strong hook early drop",
        128.0,0x230023ULL);
    if(normalArrival.getStructureFingerprint()==earlyArrival.getStructureFingerprint())
    {std::cerr<<"v2.3 novelty relocation fixture did not move the hook\n";return 87;}
    auto withoutChordContext=[](std::vector<int> fp)
    {
        if(fp.size()<2||fp[0]!=sonara::SongArrangement::melodyFingerprintVersion)return fp;
        const int notes=fp[1];
        for(int i=0;i<notes;++i)
            fp[(size_t)(2+i*sonara::SongArrangement::melodyFingerprintStride+3)]=0;
        const int summary=2+notes*sonara::SongArrangement::melodyFingerprintStride;
        fp[(size_t)(summary+8)]=0;
        return fp;
    };
    if(withoutChordContext(normalArrival.getMelodyFingerprint())
       !=withoutChordContext(earlyArrival.getMelodyFingerprint()))
    {std::cerr<<"v2.3 perceptual fingerprint treated a relocated hook as new\n";return 88;}
    const auto richFingerprint=normalArrival.getMelodyFingerprint();
    if(richFingerprint.size()<2
       ||richFingerprint[0]!=sonara::SongArrangement::melodyFingerprintVersion
       ||richFingerprint[1]<6)
    {std::cerr<<"v2.3 rich hook fingerprint is missing phrase data\n";return 89;}

    // v2.4: genre changes harmonic grammar, not merely SoundDNA or voicing.
    const juce::String harmonyPrompts[]={
        "minimal tech house 126 BPM C minor clean chords",
        "emotional progressive house 126 BPM C minor clean chords",
        "uplifting trance 138 BPM C minor clean chords",
        "dark drum and bass 174 BPM C minor clean chords",
        "cinematic EDM 110 BPM C minor clean chords"
    };
    std::set<uint64_t> genreProgressions;
    for(const auto& harmonyPrompt:harmonyPrompts)
    {
        sonara::SongArrangement song;
        song.generateComposition(harmonyPrompt,126.0,0x240024ULL);
        const auto fp=song.getProgressionFingerprint();
        if(fp.size()<5){std::cerr<<"v2.4 genre harmony fingerprint missing\n";return 90;}
        const int n=juce::jlimit(1,8,fp[0]);
        uint64_t h=1469598103934665603ULL;
        for(int i=0;i<n;++i){h^=(uint64_t)(fp[(size_t)(2+i*3)]+31);h*=1099511628211ULL;}
        h^=(uint64_t)n;h*=1099511628211ULL;
        genreProgressions.insert(h);
        if(!harmonyPrompt.containsIgnoreCase("tech house"))
        {
            const int ending=fp[(size_t)(2+(n-1)*3)];
            if(ending!=3&&ending!=4&&ending!=6)
            {std::cerr<<"v2.4 progression lost its functional turnaround\n";return 91;}
        }
    }
    if(genreProgressions.size()<4)
    {std::cerr<<"v2.4 genre prompts still collapse to the same chord family\n";return 92;}

    sonara::SongArrangement colouredHarmony;
    colouredHarmony.generate(
        "dreamy cinematic EDM 112 BPM C major extended chords borrowed chords",
        112.0,0x24c010ULL);
    const auto* colourChords=findLane(colouredHarmony,"CHORDS");
    if(!colourChords){std::cerr<<"v2.4 coloured harmony missing CHORDS lane\n";return 93;}
    std::map<int,std::set<int>> chordPitchClasses;
    for(const auto& note:colourChords->notes)
        chordPitchClasses[(int)std::llround(note.beat*16.0)].insert((note.note%12+12)%12);
    bool hasExtension=false,hasBorrowedMinorIv=false;
    for(const auto& [onset,pitches]:chordPitchClasses)
    {
        juce::ignoreUnused(onset);
        hasExtension|=pitches.size()>=4;
        hasBorrowedMinorIv|=pitches.count(5)>0&&pitches.count(8)>0&&pitches.count(0)>0;
    }
    if(!hasExtension)
    {std::cerr<<"v2.4 explicit extended-chord request produced only triads\n";return 94;}
    if(!hasBorrowedMinorIv)
    {std::cerr<<"v2.4 explicit borrowed-chord request did not produce minor iv\n";return 95;}

    sonara::SongArrangement cleanMajor,borrowedMajor;
    cleanMajor.generateComposition("cinematic EDM 112 BPM C major clean chords",112.0,0x24b044ULL);
    borrowedMajor.generateComposition(
        "cinematic EDM 112 BPM C major clean chords borrowed chords",112.0,0x24b044ULL);
    if(cleanMajor.getHarmonyId()==borrowedMajor.getHarmonyId())
    {std::cerr<<"v2.4 borrowed quality was omitted from Harmony DNA identity\n";return 96;}

    // v2.5: bass wording must select a real groove family. Previously every
    // mainstream prompt collapsed to the same one/two-note root pattern.
    sonara::SongArrangement sustainedBass,offbeatBass,rollingBass;
    sustainedBass.generateComposition(
        "progressive house 128 BPM F minor sustained bass",128.0,0x250025ULL);
    offbeatBass.generateComposition(
        "progressive house 128 BPM F minor offbeat bass",128.0,0x250025ULL);
    rollingBass.generateComposition(
        "progressive house 128 BPM F minor rolling bass",128.0,0x250025ULL);

    auto firstDrop=[&](const sonara::SongArrangement& song)
        ->const sonara::ArrangementSection*
    {
        for(const auto& section:song.getSections())
            if(section.name=="DROP")return &section;
        return nullptr;
    };
    auto firstBarOnsets=[&](const sonara::SongArrangement& song,const juce::String& laneName)
    {
        std::set<int> onsets;
        const auto* section=firstDrop(song);
        const auto* lane=findLane(song,laneName);
        if(section==nullptr||lane==nullptr)return onsets;
        const double begin=section->startBar*4.0;
        for(const auto& note:lane->notes)
            if(note.beat>=begin&&note.beat<begin+4.0)
                onsets.insert((int)std::llround((note.beat-begin)*100.0));
        return onsets;
    };
    const auto sustainedOnsets=firstBarOnsets(sustainedBass,"BASS");
    const auto offbeatOnsets=firstBarOnsets(offbeatBass,"BASS");
    const auto rollingOnsets=firstBarOnsets(rollingBass,"BASS");
    if(sustainedOnsets!=std::set<int>{16}
       ||offbeatOnsets!=std::set<int>({50,150,250,350})
       ||rollingOnsets!=std::set<int>({16,75,150,216,275,350}))
    {std::cerr<<"v2.5 explicit bass groove families collapsed or were ignored\n";return 97;}

    sonara::SongArrangement octavePickupBass;
    octavePickupBass.generateComposition(
        "progressive house 128 BPM C minor octave pickup",128.0,0x250025ULL);
    const auto* octaveDrop=firstDrop(octavePickupBass);
    const auto* octaveLane=findLane(octavePickupBass,"BASS");
    std::vector<int> pickupPitches;
    if(octaveDrop&&octaveLane)
    {
        const double begin=octaveDrop->startBar*4.0;
        for(const auto& note:octaveLane->notes)
            if(note.beat>=begin&&note.beat<begin+4.0)pickupPitches.push_back(note.note);
    }
    if(pickupPitches.size()!=4||pickupPitches.back()-pickupPitches.front()!=12)
    {std::cerr<<"v2.5 octave-pickup groove did not produce its deliberate lift\n";return 104;}

    auto laneIsMonophonic=[](const sonara::ArrangementLane& lane)
    {
        for(size_t i=1;i<lane.notes.size();++i)
            if(lane.notes[i-1].beat+lane.notes[i-1].length>lane.notes[i].beat+1.0e-6)
                return false;
        return true;
    };
    for(const auto* song:{&sustainedBass,&offbeatBass,&rollingBass})
    {
        const auto* bass=findLane(*song,"BASS");
        const auto* sub=findLane(*song,"SUB");
        if(!bass||!sub||!laneIsMonophonic(*bass)||!laneIsMonophonic(*sub))
        {std::cerr<<"v2.5 low lanes contain overlapping monophonic notes\n";return 98;}
        for(const auto& note:bass->notes)if(note.note<32||note.note>52)
        {std::cerr<<"v2.5 BASS escaped its support register\n";return 99;}
        for(const auto& note:sub->notes)if(note.note<24||note.note>43)
        {std::cerr<<"v2.5 SUB escaped its safe fundamental register\n";return 100;}
    }

    sonara::SongArrangement dnbLowEnd;
    dnbLowEnd.generate(
        "drum and bass 174 BPM D minor moving reese bass",174.0,0x25d0b5ULL);
    const auto* dnbSub=findLane(dnbLowEnd,"SUB");
    const auto dnbSubOnsets=firstBarOnsets(dnbLowEnd,"SUB");
    if(!dnbSub||dnbSubOnsets!=std::set<int>({16,216})
       ||!laneIsMonophonic(*dnbSub)||dnbSub->sound.unison!=1
       ||dnbSub->sound.width!=0.f||dnbSub->sound.reverb!=0.f||dnbSub->sound.delay!=0.f)
    {std::cerr<<"v2.5 SUB is not deterministic, mono, dry and kick-aware\n";return 101;}

    const auto* offbeatChorus=(const sonara::ArrangementSection*)nullptr;
    for(const auto& section:offbeatBass.getSections())
        if(section.name=="CHORUS"){offbeatChorus=&section;break;}
    const auto* offbeatBassLane=findLane(offbeatBass,"BASS");
    const auto* offbeatSubLane=findLane(offbeatBass,"SUB");
    if(!offbeatChorus||!offbeatBassLane||!offbeatSubLane)
    {std::cerr<<"v2.5 transition-gap fixture missing\n";return 102;}
    const double gapBar=(offbeatChorus->startBar+offbeatChorus->bars-1)*4.0;
    for(const auto* lane:{offbeatBassLane,offbeatSubLane})
        for(const auto& note:lane->notes)
            if(note.beat>=gapBar&&note.beat<gapBar+4.0&&note.beat+note.length>gapBar+3.001)
            {std::cerr<<"v2.5 low-frequency tail fills the pre-drop breathing space\n";return 103;}

    // v2.6: genre and section must change the actual drum grammar, not only the
    // generated kit. In particular DnB must no longer share trap's half-time
    // snare placement, and trance must use its high-motion drop hats.
    sonara::SongArrangement houseDrums,tranceDrums,dnbDrums,trapDrums;
    houseDrums.generateComposition(
        "progressive house 128 BPM F minor closed hats",128.0,0x260026ULL);
    tranceDrums.generateComposition(
        "uplifting trance 138 BPM F minor open hats",138.0,0x260026ULL);
    dnbDrums.generateComposition(
        "drum and bass 174 BPM F minor breakbeat drums",174.0,0x260026ULL);
    trapDrums.generateComposition(
        "trap 140 BPM F minor half time drums",140.0,0x260026ULL);

    const auto dnbSnareOnsets=firstBarOnsets(dnbDrums,"SNARE / CLAP");
    const auto trapSnareOnsets=firstBarOnsets(trapDrums,"SNARE / CLAP");
    if(dnbSnareOnsets.count(100)==0||dnbSnareOnsets.count(300)==0
       ||dnbSnareOnsets.count(200)>0||trapSnareOnsets.count(200)==0
       ||trapSnareOnsets.count(100)>0||trapSnareOnsets.count(300)>0)
    {std::cerr<<"v2.6 DnB and trap still share the same backbeat grammar\n";return 105;}

    const auto houseHatOnsets=firstBarOnsets(houseDrums,"HATS");
    const auto tranceHatOnsets=firstBarOnsets(tranceDrums,"HATS");
    if(houseHatOnsets.size()>8||tranceHatOnsets.size()<12
       ||tranceHatOnsets.size()<=houseHatOnsets.size())
    {std::cerr<<"v2.6 trance drop hats did not gain genre-specific motion\n";return 106;}

    auto sectionNamed=[](const sonara::SongArrangement& song,const juce::String& name)
        ->const sonara::ArrangementSection*
    {
        for(const auto& section:song.getSections())if(section.name==name)return &section;
        return nullptr;
    };
    auto averageLaneNotes=[](const sonara::ArrangementLane& lane,const sonara::ArrangementSection& section)
    {
        int notes=0;
        const double begin=section.startBar*4.0;
        const double end=(section.startBar+section.bars)*4.0;
        for(const auto& note:lane.notes)if(note.beat>=begin&&note.beat<end)++notes;
        return notes/(double)juce::jmax(1,section.bars);
    };
    const auto* drumVerse=sectionNamed(houseDrums,"VERSE");
    const auto* drumDrop=sectionNamed(houseDrums,"DROP");
    const auto* drumBreak=sectionNamed(houseDrums,"BREAKDOWN");
    const auto* houseKick=findLane(houseDrums,"KICK");
    const auto* houseHats=findLane(houseDrums,"HATS");
    const auto* housePerc=findLane(houseDrums,"PERCUSSION");
    if(!drumVerse||!drumDrop||!drumBreak||!houseKick||!houseHats||!housePerc)
    {std::cerr<<"v2.6 section-aware drum fixture missing\n";return 107;}

    int verseKickMax=0,dropKickMaxV26=0;
    for(const auto& note:houseKick->notes)
    {
        const int bar=(int)std::floor(note.beat/4.0);
        if(bar>=drumVerse->startBar&&bar<drumVerse->startBar+drumVerse->bars)
            verseKickMax=juce::jmax(verseKickMax,note.velocity);
        if(bar>=drumDrop->startBar&&bar<drumDrop->startBar+drumDrop->bars)
            dropKickMaxV26=juce::jmax(dropKickMaxV26,note.velocity);
    }
    if(!(averageLaneNotes(*houseKick,*drumDrop)>averageLaneNotes(*houseKick,*drumBreak)+3.0
         &&averageLaneNotes(*houseHats,*drumDrop)>averageLaneNotes(*houseHats,*drumBreak)+2.0
         &&dropKickMaxV26>verseKickMax))
    {std::cerr<<"v2.6 drums do not create drop/breakdown section contrast\n";return 108;}

    const double phraseFillBar=(drumDrop->startBar+3)*4.0;
    bool hasFourBarFill=false;
    for(const auto& note:housePerc->notes)
        if(note.beat>=phraseFillBar+3.5&&note.beat<phraseFillBar+4.0
           &&(note.note==47||note.note==50))hasFourBarFill=true;
    if(!hasFourBarFill)
    {std::cerr<<"v2.6 internal four-bar drum fill is missing\n";return 109;}

    const double breakExit=(drumBreak->startBar+drumBreak->bars-1)*4.0;
    std::set<int> breakdownExitToms;
    for(const auto& note:housePerc->notes)
        if(note.beat>=breakExit+2.7&&note.beat<breakExit+4.0)
            breakdownExitToms.insert(note.note);
    if(breakdownExitToms.count(45)==0||breakdownExitToms.count(47)==0||breakdownExitToms.count(50)==0)
    {std::cerr<<"v2.6 breakdown exit has no rising transition fill\n";return 110;}

    sonara::SongArrangement drumPromptEdit=houseDrums;
    drumPromptEdit.regenerateDrumsOnly("half time trap drums",0x26feedULL);
    const auto editedSnareOnsets=firstBarOnsets(drumPromptEdit,"SNARE / CLAP");
    if(editedSnareOnsets.count(200)==0||editedSnareOnsets.count(100)>0||editedSnareOnsets.count(300)>0)
    {std::cerr<<"v2.6 drum-only prompt did not control the regenerated groove\n";return 111;}

    // v2.7: FX must describe section transitions rather than appearing as
    // unrelated decoration. Every major arrival gets a rise, ascending tension,
    // reverse/suck and downbeat accent; a release gets a descending downlifter
    // plus an atmosphere change. The final instant before a DROP remains clear.
    const auto* transitionFx=findLane(houseDrums,"FX / TRANSITIONS");
    if(!transitionFx||transitionFx->notes.empty())
    {std::cerr<<"v2.7 transition lane is missing\n";return 112;}
    for(size_t i=1;i<transitionFx->notes.size();++i)
        if(transitionFx->notes[i].beat+1.0e-6<transitionFx->notes[i-1].beat)
        {std::cerr<<"v2.7 transition events are not time ordered\n";return 113;}

    auto hasFx=[](const sonara::ArrangementLane& lane,double begin,double end,
                  int lowNote,int highNote)
    {
        for(const auto& note:lane.notes)
            if(note.beat>=begin&&note.beat<end&&note.note>=lowNote&&note.note<=highNote)
                return true;
        return false;
    };

    for(const auto& section:houseDrums.getSections())
    {
        const bool major=section.name=="CHORUS"||section.name=="DROP"
            ||section.name.contains("FINAL");
        if(!major)continue;
        const double boundary=section.startBar*4.0;
        if(!hasFx(*transitionFx,boundary-8.01,boundary-.50,82,89)
           ||!hasFx(*transitionFx,boundary-2.01,boundary-.50,74,81)
           ||!hasFx(*transitionFx,boundary-.51,boundary-.10,92,97)
           ||!hasFx(*transitionFx,boundary-.01,boundary+.40,36,47))
        {std::cerr<<"v2.7 major arrival is missing its produced FX sequence\n";return 114;}

        std::vector<const sonara::ArrangementNote*> tension;
        for(const auto& note:transitionFx->notes)
            if(note.beat>=boundary-2.01&&note.beat<boundary-.50
               &&note.note>=74&&note.note<=81)tension.push_back(&note);
        if(tension.size()!=3||tension[0]->note>=tension[1]->note
           ||tension[1]->note>=tension[2]->note
           ||tension[0]->velocity>=tension[1]->velocity
           ||tension[1]->velocity>=tension[2]->velocity)
        {std::cerr<<"v2.7 filter tension does not rise into the arrival\n";return 115;}

        if(section.name=="DROP"||section.name.contains("FINAL"))
            for(const auto& note:transitionFx->notes)
                if(note.beat<boundary&&note.beat+note.length>boundary-.099)
                {std::cerr<<"v2.7 FX tail fills the protected pre-drop air\n";return 116;}
    }

    const auto* transitionPerc=findLane(houseDrums,"PERCUSSION");
    for(const auto& section:houseDrums.getSections())
    {
        if(section.name!="DROP"&&!section.name.contains("FINAL"))continue;
        const double boundary=section.startBar*4.0;
        if(!transitionPerc||!hasFx(*transitionPerc,boundary-.01,boundary+.01,49,49)
           ||!hasFx(*transitionPerc,boundary-.01,boundary+.01,57,57))
        {std::cerr<<"v2.7 DROP is missing its rendered crash/impact pair\n";return 117;}
    }

    const auto* transitionBreak=sectionNamed(houseDrums,"BREAKDOWN");
    if(!transitionBreak)
    {std::cerr<<"v2.7 release fixture is missing\n";return 118;}
    const double releaseBoundary=transitionBreak->startBar*4.0;
    std::vector<const sonara::ArrangementNote*> downlifter;
    for(const auto& note:transitionFx->notes)
        if(note.beat>=releaseBoundary&&note.beat<releaseBoundary+1.4
           &&note.note>=52&&note.note<=63)downlifter.push_back(&note);
    if(downlifter.size()!=3||downlifter[0]->note<=downlifter[1]->note
       ||downlifter[1]->note<=downlifter[2]->note
       ||!hasFx(*transitionFx,releaseBoundary,releaseBoundary+.20,66,73))
    {std::cerr<<"v2.7 breakdown release lacks downlifter/atmosphere direction\n";return 119;}

    for(const auto& note:transitionFx->notes)
    {
        bool boundaryDriven=false;
        for(size_t i=1;i<houseDrums.getSections().size();++i)
        {
            const double boundary=houseDrums.getSections()[i].startBar*4.0;
            if(note.beat>=boundary-8.01&&note.beat<=boundary+1.01)
            {boundaryDriven=true;break;}
        }
        if(!boundaryDriven)
        {std::cerr<<"v2.7 FX was scattered away from a section boundary\n";return 120;}
    }

    sonara::SongArrangement transitionPalette;
    transitionPalette.generate("festival progressive house powerful drop",128.0,0x270027ULL);
    const auto* paletteFx=findLane(transitionPalette,"FX / TRANSITIONS");
    if(!paletteFx||!paletteFx->sound.name.containsIgnoreCase("riser")
       ||paletteFx->sound.subLevel!=0.f||paletteFx->sound.attack<.15f)
    {std::cerr<<"v2.7 impact rule still overwrites the swept FX SoundDNA\n";return 121;}

    // v2.8: structure is now a real plan. Genre chooses an architecture, the
    // structure seed develops its lengths, and an explicit bar target controls
    // total duration without breaking section contiguity.
    sonara::SongArrangement radioPlan,festivalPlan,progressivePlan,cinematicPlan,targetPlan;
    radioPlan.generateComposition("radio pop EDM 124 BPM C minor",124.0,0x280028ULL);
    festivalPlan.generateComposition("festival mainstage EDM 128 BPM C minor",128.0,0x280028ULL);
    progressivePlan.generateComposition("progressive house 128 BPM C minor",128.0,0x280028ULL);
    cinematicPlan.generateComposition("cinematic EDM 112 BPM C minor",112.0,0x280028ULL);
    targetPlan.generateComposition("progressive house 128 BPM C minor 72 bars",128.0,0x280028ULL);

    auto structureSignature=[](const sonara::SongArrangement& song)
    {
        juce::StringArray names;
        for(const auto& section:song.getSections())names.add(section.name);
        return names.joinIntoString("|");
    };
    const std::set<juce::String> structureFamilies={
        structureSignature(radioPlan),structureSignature(festivalPlan),
        structureSignature(progressivePlan),structureSignature(cinematicPlan)};
    if(structureFamilies.size()<3
       ||!structureSignature(festivalPlan).contains("DROP 2")
       ||structureSignature(cinematicPlan).indexOf("BREAKDOWN")
          >structureSignature(cinematicPlan).indexOf("DROP"))
    {std::cerr<<"v2.8 genre prompts still collapse to one fixed architecture\n";return 122;}

    for(const auto* song:{&radioPlan,&festivalPlan,&progressivePlan,&cinematicPlan,&targetPlan})
    {
        int nextBar=0,total=0;
        if(song->getSections().size()!=8)
        {std::cerr<<"v2.8 SongPlan lost a complete eight-role arrangement\n";return 123;}
        for(const auto& section:song->getSections())
        {
            if(section.startBar!=nextBar||section.bars<4||section.bars%4!=0)
            {std::cerr<<"v2.8 section plan is discontinuous or off phrase grid\n";return 124;}
            nextBar+=section.bars;total+=section.bars;
        }
        if(total!=song->getBars())
        {std::cerr<<"v2.8 planned section lengths do not equal song length\n";return 125;}
    }
    if(targetPlan.getBars()!=72)
    {std::cerr<<"v2.8 explicit target song length was ignored\n";return 126;}
    const auto targetTree=targetPlan.toValueTree();
    const auto targetDna=targetTree.getChildWithName("COMPOSITION_DNA");
    const auto restoredTarget=sonara::SongArrangement::fromValueTree(targetTree).toValueTree()
        .getChildWithName("COMPOSITION_DNA");
    if(!targetDna.hasProperty("structureVariant")||!targetDna.hasProperty("targetBars")
       ||(int)targetDna.getProperty("targetBars",0)!=72
       ||targetDna.getProperty("structureVariant")!=restoredTarget.getProperty("structureVariant")
       ||(int)restoredTarget.getProperty("targetBars",0)!=72)
    {std::cerr<<"v2.8 adaptive SongPlan metadata did not survive persistence\n";return 127;}

    const auto* cinematicLead=findLane(cinematicPlan,"LEAD");
    const auto* cinematicBass=findLane(cinematicPlan,"BASS");
    const auto* cinematicSub=findLane(cinematicPlan,"SUB");
    const auto* cinematicKick=findLane(cinematicPlan,"KICK");
    const auto* cinematicSnare=findLane(cinematicPlan,"SNARE / CLAP");
    const auto& cinematicSections=cinematicPlan.getSections();
    const sonara::ArrangementSection *beforeDrop=nullptr,*cinematicChorus=nullptr;
    for(size_t i=0;i<cinematicSections.size();++i)
    {
        if(cinematicSections[i].name=="CHORUS")cinematicChorus=&cinematicSections[i];
        if(i+1<cinematicSections.size()&&cinematicSections[i+1].name.contains("DROP"))
            beforeDrop=&cinematicSections[i];
    }
    if(!cinematicLead||!cinematicBass||!cinematicSub||!cinematicKick||!cinematicSnare
       ||!beforeDrop||!cinematicChorus||beforeDrop==cinematicChorus)
    {std::cerr<<"v2.8 adaptive pre-drop fixture is incomplete\n";return 128;}
    const double adaptiveGap=(beforeDrop->startBar+beforeDrop->bars)*4.0;
    for(const auto* lane:{cinematicLead,cinematicBass,cinematicSub,cinematicKick})
        for(const auto& note:lane->notes)
            if(note.beat>=adaptiveGap-.50&&note.beat<adaptiveGap)
            {std::cerr<<"v2.8 pre-drop air is still attached to a fixed section name\n";return 129;}

    const double chorusLast=(cinematicChorus->startBar+cinematicChorus->bars-1)*4.0;
    const double buildLast=(beforeDrop->startBar+beforeDrop->bars-1)*4.0;
    int chorusRoll=0,adaptiveRoll=0;
    for(const auto& note:cinematicSnare->notes)
    {
        if(note.beat>=chorusLast+2.0&&note.beat<chorusLast+3.5)++chorusRoll;
        if(note.beat>=buildLast+2.0&&note.beat<buildLast+3.5)++adaptiveRoll;
    }
    if(chorusRoll>=7||adaptiveRoll<7)
    {std::cerr<<"v2.8 transition tension did not follow the actual DROP boundary\n";return 130;}

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
