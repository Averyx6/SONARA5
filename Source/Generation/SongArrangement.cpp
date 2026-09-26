#include "SongArrangement.h"
#include "PromptGenerator.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace sonara {
namespace {
constexpr double tpq = 960.0;

void addNote(ArrangementLane& lane, int note, double beat, double length, int velocity)
{
    lane.notes.push_back({ juce::jlimit(0, 127, note), juce::jlimit(1, 127, velocity), beat, juce::jmax(0.03, length) });
}

bool sectionContains(const ArrangementSection& s, int bar) noexcept
{
    return bar >= s.startBar && bar < s.startBar + s.bars;
}

double bpmFromPrompt(const juce::String& raw, double fallback)
{
    juce::StringArray tokens;
    tokens.addTokens(raw, " ,;:/\t\r\n", "");
    tokens.trim();
    tokens.removeEmptyStrings();

    for (int i = 0; i < tokens.size(); ++i)
    {
        auto token = tokens[i].trim();
        if (token.endsWithIgnoreCase("bpm"))
        {
            const double value = token.dropLastCharacters(3).getDoubleValue();
            if (value >= 60.0 && value <= 200.0) return value;
        }

        if (i + 1 < tokens.size() && tokens[i + 1].equalsIgnoreCase("bpm"))
        {
            const double value = token.getDoubleValue();
            if (value >= 60.0 && value <= 200.0) return value;
        }
    }
    return fallback;
}
}

uint64_t SongArrangement::mix64(uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

float SongArrangement::random01(uint64_t seed, uint64_t salt) noexcept
{
    return static_cast<float>(mix64(seed ^ salt) & 0x00ffffffULL) / 16777215.0f;
}

void SongArrangement::buildSeedDomains(uint64_t master)
{
    masterSeed=master;
    domains.structure   = mix64(master ^ 0x5354525543545552ULL); // STRUCTUR
    domains.harmony     = mix64(master ^ 0x4841524d4f4e5955ULL); // HARMONYU
    domains.voicing     = mix64(master ^ 0x564f4943494e4755ULL); // VOICINGU
    domains.drums       = mix64(master ^ 0x4452554d535f5345ULL); // DRUMS_SE
    domains.bass        = mix64(master ^ 0x424153535f534545ULL); // BASS_SEE
    domains.sub         = mix64(master ^ 0x5355425f53454544ULL); // SUB_SEED
    domains.pluck       = mix64(master ^ 0x504c55434b534545ULL); // PLUCKSEE
    domains.pad         = mix64(master ^ 0x5041445f53454544ULL); // PAD_SEED
    domains.melody      = mix64(master ^ 0x4d454c4f44595345ULL); // MELODYSE
    domains.counter     = mix64(master ^ 0x434f554e54455253ULL); // COUNTERS
    domains.fx          = mix64(master ^ 0x46585f5345454455ULL); // FX_SEEDU
    domains.soundPalette= mix64(master ^ 0x534f554e4450414cULL); // SOUNDPAL
}

int SongArrangement::scaleSemitoneForDegree(int degree) const noexcept
{
    static constexpr int minorScale[7]={0,2,3,5,7,8,10};
    static constexpr int majorScale[7]={0,2,4,5,7,9,11};
    const int* scale=minor?minorScale:majorScale;
    int octave=0;
    while(degree<0){degree+=7;--octave;}
    while(degree>=7){degree-=7;++octave;}
    return scale[degree]+12*octave;
}

std::array<int,4> SongArrangement::chordTonesFor(const HarmonyEvent& event) const noexcept
{
    const int d=juce::jlimit(0,6,event.scaleDegree);
    const int root=rootMidi+scaleSemitoneForDegree(d);
    int third=root+(scaleSemitoneForDegree(d+2)-scaleSemitoneForDegree(d));
    const int fifth=root+(scaleSemitoneForDegree(d+4)-scaleSemitoneForDegree(d));

    // A borrowed dominant in minor raises the third, providing real dominant tension.
    if(event.borrowed&&minor&&d==4)third=root+4;

    int fourth=root+(scaleSemitoneForDegree(d+6)-scaleSemitoneForDegree(d)); // diatonic seventh
    if(event.extension==2)fourth=root+14;      // add9
    else if(event.extension==3)third=root+2;   // sus2
    else if(event.extension==4)third=root+5;   // sus4

    std::array<int,4> notes{root,third,fifth,fourth};

    int inversion=juce::jlimit(0,2,event.inversion);
    for(int i=0;i<inversion;++i)notes[(size_t)i]+=12;
    std::sort(notes.begin(),notes.end());

    // Voicing style changes spacing without changing harmonic identity.
    switch(event.voicingStyle%6)
    {
        case 1: notes[2]+=12; break;                    // open fifth
        case 2: notes[1]+=12; std::sort(notes.begin(),notes.end()); break;
        case 3: notes[0]-=12; break;                    // wider root
        case 4: notes[3]+=12; break;                    // high extension
        case 5: notes[0]-=12; notes[2]+=12; break;      // wide/drop-style
        default: break;
    }
    return notes;
}

void SongArrangement::buildHarmonyPlan(uint64_t seed)
{
    harmonyPlan=HarmonyPlan{};
    const auto p=sourcePrompt.toLowerCase();
    const bool tech=p.contains("tech house")||p.contains("minimal house");
    const bool cinematic=p.contains("cinematic")||p.contains("film");
    const bool pop=p.contains("pop")||p.contains("radio");
    const bool progressive=p.contains("progressive")||p.contains("melodic house");
    const bool dreamy=p.contains("dreamy");
    const bool aggressive=p.contains("aggressive")||p.contains("powerful")||p.contains("hard");
    const bool uplifting=p.contains("uplifting")||p.contains("bright");

    // Progression length is a musical decision, not a fixed four-chord template.
    if(tech)
        harmonyPlan.progressionLength=random01(seed,0x3101)>.72f?3:2;
    else if(cinematic)
        harmonyPlan.progressionLength=random01(seed,0x3102)>.42f?8:6;
    else if(progressive)
    {
        const float r=random01(seed,0x3103);
        harmonyPlan.progressionLength=r<.18f?3:(r<.60f?4:(r<.83f?6:8));
    }
    else if(pop)
        harmonyPlan.progressionLength=random01(seed,0x3104)>.78f?6:4;
    else
    {
        static constexpr int lengths[5]={2,3,4,6,8};
        harmonyPlan.progressionLength=lengths[(int)(random01(seed,0x3105)*5.f)%5];
    }

    harmonyPlan.cadenceStyle=(int)(random01(seed,0x3110)*4.f)%4;
    harmonyPlan.rhythmMode=(int)(random01(seed,0x3111)*6.f)%6;
    harmonyPlan.registerBase=progressive||cinematic?60:57;
    harmonyPlan.tension=.20f+.58f*random01(seed,0x3112);
    harmonyPlan.extensionProbability=.08f+.28f*random01(seed,0x3113);
    harmonyPlan.suspensionProbability=.04f+.22f*random01(seed,0x3114);
    harmonyPlan.passingProbability=.04f+.18f*random01(seed,0x3115);
    harmonyPlan.borrowedProbability=minor?(.03f+.15f*random01(seed,0x3116)):.025f;
    harmonyPlan.pedalIntro=random01(seed,0x3117)<(cinematic?.72f:.48f);
    harmonyPlan.pedalVerse=tech||random01(seed,0x3118)<.16f;

    if(dreamy){harmonyPlan.extensionProbability=juce::jmax(harmonyPlan.extensionProbability,.28f);harmonyPlan.suspensionProbability=juce::jmax(harmonyPlan.suspensionProbability,.24f);}
    if(aggressive){harmonyPlan.extensionProbability*=.45f;harmonyPlan.tension=juce::jmax(harmonyPlan.tension,.58f);}
    if(uplifting)harmonyPlan.cadenceStyle=0;

    // Build progression from functional scale-degree families. This produces many
    // normalized progressions rather than selecting one of eight fixed loops.
    const int n=harmonyPlan.progressionLength;
    harmonyPlan.mainDegrees.fill(0);
    harmonyPlan.alternateDegrees.fill(0);

    int first=0;
    if(pop&&random01(seed,0x3120)>.78f)first=5;          // vi start in major/pop
    if(minor&&random01(seed,0x3121)>.88f)first=5;       // VI opening
    harmonyPlan.mainDegrees[0]=first;

    for(int i=1;i<n;++i)
    {
        const bool final=i==n-1;
        int candidates[7]={0,5,2,6,3,4,1};
        int candidateCount=7;

        if(tech)
        {
            int techPool[4]={0,5,6,3};
            const int pick=(int)(random01(seed,0x3130+i)*4.f)%4;
            harmonyPlan.mainDegrees[(size_t)i]=techPool[pick];
        }
        else if(final)
        {
            // End loops with dominant/leading/pre-dominant tension often enough
            // to create a meaningful return to the first chord.
            const float r=random01(seed,0x3140+i);
            harmonyPlan.mainDegrees[(size_t)i]=r<.48f?4:(r<.82f?6:3);
        }
        else
        {
            const int prev=harmonyPlan.mainDegrees[(size_t)i-1];
            int pick=(int)(random01(seed,0x3150+i*17)*candidateCount)%candidateCount;
            int d=candidates[pick];
            if(d==prev)d=candidates[(pick+2+(i%3))%candidateCount];
            if(i==1&&d==0)d=minor?5:3;
            harmonyPlan.mainDegrees[(size_t)i]=d;
        }
    }

    // Alternate progression is related but not a rotation/copy.
    for(int i=0;i<n;++i)
    {
        int d=harmonyPlan.mainDegrees[(size_t)i];
        const float r=random01(seed,0x3200+i*23);
        if(i==0&&r<.55f)d=0;
        else if(r<.30f)d=(d+2)%7;
        else if(r<.56f)d=(d+4)%7;
        else if(r<.72f)d=(d+6)%7;
        harmonyPlan.alternateDegrees[(size_t)i]=d;
    }
    bool same=true;
    for(int i=0;i<n;++i)if(harmonyPlan.alternateDegrees[(size_t)i]!=harmonyPlan.mainDegrees[(size_t)i]){same=false;break;}
    if(same&&n>1)harmonyPlan.alternateDegrees[1]=(harmonyPlan.alternateDegrees[1]+3)%7;

    // Harmonic rhythm patterns are stored independently of progression roots.
    for(int i=0;i<8;++i)
    {
        double barsPerChord=1.0;
        switch(harmonyPlan.rhythmMode)
        {
            case 0: barsPerChord=1.0; break;
            case 1: barsPerChord=(i%3==0?2.0:1.0); break;
            case 2: barsPerChord=(i%2==0?.5:1.0); break;
            case 3: barsPerChord=(i%4==3?.5:1.5); break;
            case 4: barsPerChord=(i%3==1?.5:.75); break;
            default: barsPerChord=(i%2==0?2.0:.5); break;
        }
        if(tech)barsPerChord=juce::jmax(1.0,barsPerChord);
        if(cinematic)barsPerChord=juce::jmax(1.0,barsPerChord);
        harmonyPlan.rhythmBars[(size_t)i]=barsPerChord;
        harmonyPlan.inversions[(size_t)i]=(int)(random01(domains.voicing,0x3300+i*31)*3.f)%3;
        harmonyPlan.voicingStyles[(size_t)i]=(int)(random01(domains.voicing,0x3400+i*37)*6.f)%6;
    }
}

void SongArrangement::buildHarmonyTimeline(uint64_t seed)
{
    harmonyEvents.clear();
    const int n=juce::jmax(1,harmonyPlan.progressionLength);

    for(size_t si=0;si<sections.size();++si)
    {
        const auto& section=sections[si];
        const auto name=section.name;
        const bool intro=name.contains("INTRO");
        const bool build=name.contains("BUILD");
        const bool breakdown=name.contains("BREAKDOWN");
        const bool chorus=name.contains("CHORUS");
        const bool drop=name.contains("DROP");
        const bool finalHook=name.contains("FINAL")||name.contains("HOOK");
        const bool verse=name.contains("VERSE");

        const bool useAlt=chorus||finalHook||(drop&&random01(seed,0x4100+si)>.64f);
        const auto& sequence=useAlt?harmonyPlan.alternateDegrees:harmonyPlan.mainDegrees;

        double beat=(double)section.startBar*beatsPerBar;
        const double endBeat=(double)(section.startBar+section.bars)*beatsPerBar;
        int step=(int)((si*2+(int)(random01(seed,0x4110+si)*n))%n);

        while(beat<endBeat-.001)
        {
            int degree=sequence[(size_t)(step%n)];

            if(intro&&harmonyPlan.pedalIntro&&beat<(section.startBar+section.bars/2.0)*beatsPerBar)
                degree=0;
            if(verse&&harmonyPlan.pedalVerse&&((step%3)!=2))
                degree=0;

            double barsLen=harmonyPlan.rhythmBars[(size_t)(step%8)];
            if(build)barsLen=juce::jmax(.5,barsLen*.5);
            if(breakdown)barsLen=juce::jmin(2.0,barsLen*1.75);
            if(drop&&barsLen>1.0)barsLen=1.0;

            double length=juce::jmax(2.0,barsLen*beatsPerBar);
            length=juce::jmin(length,endBeat-beat);

            // Last build chord intentionally increases tension into the next section.
            if(build&&beat+length>=endBeat-.01)
                degree=(harmonyPlan.cadenceStyle%2==0)?4:6;

            HarmonyEvent event;
            event.beat=beat;
            event.length=length;
            event.scaleDegree=juce::jlimit(0,6,degree);
            event.inversion=harmonyPlan.inversions[(size_t)(step%8)];
            event.voicingStyle=harmonyPlan.voicingStyles[(size_t)(step%8)];
            event.sectionIndex=(int)si;
            event.borrowed=minor&&event.scaleDegree==4&&random01(seed,0x4200+(uint64_t)harmonyEvents.size())<harmonyPlan.borrowedProbability;

            const float ext=random01(seed,0x4300+(uint64_t)harmonyEvents.size());
            if(ext<harmonyPlan.suspensionProbability)
                event.extension=random01(seed,0x4310+(uint64_t)harmonyEvents.size())>.5f?3:4;
            else if(ext<harmonyPlan.suspensionProbability+harmonyPlan.extensionProbability)
                event.extension=random01(seed,0x4320+(uint64_t)harmonyEvents.size())>.5f?1:2;

            harmonyEvents.push_back(event);
            beat+=length;
            ++step;
        }
    }

    std::sort(harmonyEvents.begin(),harmonyEvents.end(),
              [](const HarmonyEvent& a,const HarmonyEvent& b){return a.beat<b.beat;});
}

const SongArrangement::HarmonyEvent* SongArrangement::harmonyAtBeat(double beat) const noexcept
{
    if(harmonyEvents.empty())return nullptr;
    const HarmonyEvent* best=&harmonyEvents.front();
    for(const auto& event:harmonyEvents)
    {
        if(event.beat>beat+.0001)break;
        best=&event;
        if(beat<event.beat+event.length-.0001)return &event;
    }
    return best;
}

uint64_t SongArrangement::computeHarmonyId() const noexcept
{
    uint64_t h=0x4841524d4f4e5955ULL;
    for(const auto& e:harmonyEvents)
    {
        const uint64_t beatQ=(uint64_t)std::llround(e.beat*4.0);
        const uint64_t lenQ=(uint64_t)std::llround(e.length*4.0);
        h=mix64(h ^ (uint64_t)(e.scaleDegree+1)*0x9e3779b97f4a7c15ULL ^ beatQ ^ (lenQ<<8)
                  ^ ((uint64_t)e.inversion<<24) ^ ((uint64_t)e.voicingStyle<<28)
                  ^ ((uint64_t)e.extension<<32));
    }
    return h;
}

uint64_t SongArrangement::computeMelodyId() const noexcept
{
    const ArrangementLane* lead=nullptr;
    for(const auto& lane:lanes)if(lane.name=="LEAD"){lead=&lane;break;}
    if(lead==nullptr)return 0;
    uint64_t h=0x4d454c4f44594944ULL;
    int previous=rootMidi;
    for(size_t i=0;i<lead->notes.size()&&i<128;++i)
    {
        const auto& n=lead->notes[i];
        const int interval=juce::jlimit(-24,24,n.note-previous);
        const uint64_t onset=(uint64_t)std::llround(n.beat*8.0);
        const uint64_t length=(uint64_t)std::llround(n.length*16.0);
        h=mix64(h ^ (uint64_t)(interval+25) ^ (onset<<8) ^ (length<<32));
        previous=n.note;
    }
    return h;
}

juce::String SongArrangement::getMelodyArchetypeName() const
{
    static constexpr const char* names[]={
        "CALL / RESPONSE","ARPEGGIATED HOOK","OCTAVE ANTHEM","SPARSE MOTIF",
        "LYRICAL LINE","SYNCOPATED RIFF","PEDAL + ANSWER","SEQUENCED CELL",
        "FALLING HOOK","RISING LIFT","OFFBEAT HOOK","WIDE LEAP"
    };
    return names[juce::jlimit(0,11,plan.melodyArchetype)];
}

juce::String SongArrangement::getHarmonySummary() const
{
    static constexpr const char* minorRoman[]={"i","ii°","III","iv","v","VI","VII"};
    static constexpr const char* majorRoman[]={"I","ii","iii","IV","V","vi","vii°"};
    const auto* roman=minor?minorRoman:majorRoman;
    juce::StringArray parts;
    for(int i=0;i<harmonyPlan.progressionLength;++i)
        parts.add(roman[juce::jlimit(0,6,harmonyPlan.mainDegrees[(size_t)i])]);
    return parts.joinIntoString(" - ");
}

juce::String SongArrangement::getHarmonicRhythmSummary() const
{
    juce::StringArray parts;
    for(int i=0;i<harmonyPlan.progressionLength;++i)
    {
        const double v=harmonyPlan.rhythmBars[(size_t)i];
        parts.add(v==.5?"1/2":juce::String(v,1));
    }
    return parts.joinIntoString(" / ")+" bars";
}

void SongArrangement::clear()
{
    lanes.clear();
    sections.clear();
    harmonyEvents.clear();
    harmonyId=0;
    melodyId=0;
}

int SongArrangement::parseRootMidi(const juce::String& raw, bool& minorOut)
{
    const auto p = raw.toLowerCase();
    minorOut = p.contains(" minor") || p.contains("minor ") || p.contains(" min") || p.contains("m ");
    struct Key { const char* text; int pc; };
    static constexpr Key keys[] = {
        {"c#",1},{"db",1},{"d#",3},{"eb",3},{"f#",6},{"gb",6},{"g#",8},{"ab",8},{"a#",10},{"bb",10},
        {"c",0},{"d",2},{"e",4},{"f",5},{"g",7},{"a",9},{"b",11}
    };
    for (const auto& k : keys)
    {
        if (p.contains(juce::String(k.text) + " minor") || p.contains(juce::String(k.text) + " major") ||
            p.contains(juce::String(k.text) + " min") || p.contains(juce::String(k.text) + " maj"))
            return 48 + k.pc; // C3 octave range, friendly for bass/chords
    }
    minorOut = !p.contains("major");
    return 53; // F3
}

void SongArrangement::buildSongPlan(uint64_t seed)
{
    const auto p=sourcePrompt.toLowerCase();
    plan.structureStyle=(int)(random01(domains.structure,0x1001)*4.f)%4;
    plan.drumGroove=(int)(random01(domains.drums,0x1002)*6.f)%6;
    plan.hatMode=(int)(random01(domains.drums,0x1003)*4.f)%4;
    plan.progressionIndex=0;
    plan.alternateProgressionIndex=1;
    plan.bassMode=(int)(random01(domains.bass,0x1006)*5.f)%5;
    plan.chordMode=(int)(random01(domains.voicing,0x1007)*5.f)%5;
    plan.arpMode=(int)(random01(domains.pluck,0x1008)*6.f)%6;
    plan.melodyArchetype=(int)(random01(domains.melody,0x1009)*12.f)%12;
    plan.rhythmFamily=(int)(random01(domains.melody,0x100b)*10.f)%10;
    plan.startingDegree=(int)(random01(domains.melody,0x100c)*7.f)%7;
    plan.cadenceStyle=(int)(random01(domains.melody,0x100d)*4.f)%4;
    plan.motifLength=3+(int)(random01(domains.melody,0x100a)*6.f);
    plan.phraseBars=random01(domains.melody,0x1010)>.62f?8:4;
    plan.octaveRange=1+(int)(random01(domains.melody,0x1011)*3.f);
    plan.density=.46f+.42f*random01(domains.melody,0x1012);
    plan.syncopation=.12f+.58f*random01(domains.melody,0x1013);
    plan.restAmount=.08f+.30f*random01(domains.melody,0x1014);
    plan.development=.30f+.62f*random01(domains.melody,0x1015);

    if(p.contains("minimal")){plan.density*=.62f;plan.restAmount=juce::jmax(plan.restAmount,.30f);}
    if(p.contains("complex")){plan.density=juce::jmin(1.f,plan.density+.18f);plan.development=juce::jmax(plan.development,.72f);}
    if(p.contains("simple")){plan.density=juce::jmin(plan.density,.62f);plan.development=juce::jmin(plan.development,.48f);}
    if(p.contains("emotional")){plan.octaveRange=juce::jmax(plan.octaveRange,2);plan.development=juce::jmax(plan.development,.66f);}
    if(p.contains("aggressive")||p.contains("powerful")){plan.density=juce::jmin(1.f,plan.density+.12f);plan.syncopation=juce::jmax(plan.syncopation,.46f);}
    if(p.contains("dreamy")){plan.phraseBars=8;plan.restAmount=juce::jmax(plan.restAmount,.22f);plan.density=juce::jmin(plan.density,.68f);}
    if(p.contains("uplifting")){plan.melodyArchetype=(plan.melodyArchetype+1)%12;plan.octaveRange=juce::jmax(plan.octaveRange,2);}
    if(p.contains("catchy")||p.contains("memorable")){plan.motifLength=juce::jlimit(4,6,plan.motifLength);plan.development=juce::jlimit(.42f,.68f,plan.development);}
    if(p.contains("anthemic")){plan.octaveRange=3;plan.density=juce::jmax(plan.density,.68f);}
    if(p.contains("fast melody"))plan.density=juce::jmax(plan.density,.82f);
    if(p.contains("slow melody")){plan.density=juce::jmin(plan.density,.54f);plan.restAmount=juce::jmax(plan.restAmount,.24f);}
    if(p.contains("dark"))plan.melodyArchetype=(plan.melodyArchetype+5)%12;
    if(p.contains("bright"))plan.melodyArchetype=(plan.melodyArchetype+2)%12;
    if(p.contains("syncopated"))plan.syncopation=juce::jmax(plan.syncopation,.70f);
    if(p.contains("repetitive"))plan.development=juce::jmin(plan.development,.38f);
    if(p.contains("evolving"))plan.development=juce::jmax(plan.development,.78f);
    if(p.contains("short hook")){plan.phraseBars=4;plan.motifLength=juce::jmin(plan.motifLength,6);}
    if(p.contains("long melody")){plan.phraseBars=8;plan.motifLength=juce::jmax(plan.motifLength,7);}
    if(p.contains("drum and bass")||p.contains("dnb")){plan.density=juce::jmax(plan.density,.78f);plan.syncopation=juce::jmax(plan.syncopation,.66f);}
    if(p.contains("tech house")){plan.density=juce::jmin(plan.density,.58f);plan.restAmount=juce::jmax(plan.restAmount,.24f);plan.octaveRange=1;}
    if(p.contains("cinematic")){plan.phraseBars=8;plan.restAmount=juce::jmax(plan.restAmount,.22f);plan.development=juce::jmax(plan.development,.72f);}
}

void SongArrangement::buildSections(uint64_t seed)
{
    struct Layout { int lengths[8]; };
    static constexpr Layout layouts[] = {
        {{8,8,8,12,8,8,4,16}},
        {{4,12,8,16,8,8,4,12}},
        {{8,8,4,16,12,8,4,12}},
        {{4,8,8,12,8,12,8,12}}
    };

    const int style = plan.structureStyle;
    const auto& l = layouts[style].lengths;
    static constexpr const char* names[8] = {
        "INTRO","VERSE","BUILD","DROP","BREAKDOWN","CHORUS","BUILD 2","FINAL HOOK"
    };
    static constexpr float energy[8] = {
        .18f,.40f,.66f,.96f,.30f,.76f,.82f,1.00f
    };

    sections.clear();
    int start = 0;
    for (int i = 0; i < 8; ++i)
    {
        sections.push_back({ names[i], start, l[i], energy[i] });
        start += l[i];
    }
    bars = start;
}

void SongArrangement::generate(const juce::String& prompt, double bpm, uint64_t seed)
{
    clear();
    sourcePrompt = prompt;
    tempo = juce::jlimit(60.0, 200.0, bpmFromPrompt(prompt, bpm));
    rootMidi = parseRootMidi(prompt, minor);
    bars = defaultBars;
    buildSeedDomains(seed);
    buildSongPlan(domains.structure);
    buildSections(domains.structure);
    buildHarmonyPlan(domains.harmony);
    buildHarmonyTimeline(domains.harmony);

    const auto lower = prompt.toLowerCase();
    const bool energetic = lower.contains("energetic") || lower.contains("powerful") || lower.contains("festival") || lower.contains("hard") || lower.contains("edm");
    addDrums(domains.drums, energetic);
    addHarmony(domains.harmony);
    addMelody(domains.melody, energetic);
    addFx(domains.fx);

    harmonyId=computeHarmonyId();
    melodyId=computeMelodyId();

    PromptGenerator designer;
    for (size_t i = 0; i < lanes.size(); ++i)
    {
        auto& lane = lanes[i];
        juce::String soundPrompt = prompt + " " + lane.name + " ";
        if (lane.drums) soundPrompt += "tight punchy drum transient ";
        else if (lane.name == "BASS") soundPrompt += "deep controlled bass mono harmonic body ";
        else if (lane.name == "SUB") soundPrompt += "pure clean sine sub mono lowpass controlled no highs ";
        else if (lane.name == "CHORDS" || lane.name == "PAD") soundPrompt += "wide musical harmony ";
        else if (lane.name == "LEAD") soundPrompt += "memorable emotional lead ";
        else if (lane.name == "PLUCK") soundPrompt += "pluck rhythmic ";
        else soundPrompt += "clean atmospheric texture ";
        const float flavour = random01(domains.soundPalette, 0x9000ULL + static_cast<uint64_t>(i) * 0x9e37ULL);
        soundPrompt += flavour < .25f ? "warm soft spacious long release" :
                       flavour < .50f ? "bright crisp wide fast attack" :
                       flavour < .75f ? "dark thick body subtle modulation" :
                                        "airy animated wide heavy modulation";
        lane.sound = designer.generate(soundPrompt, mix64(domains.soundPalette ^ mix64((static_cast<uint64_t>(i)+1ULL) * 0x517cc1b727220a95ULL)));

        // Keep song playback lighter and cleaner than single-instrument design.
        // Each role gets a bounded unison/FX/sub budget so low notes do not stack into mud.
        if(lane.name=="BASS")
        {
            lane.sound.unison=juce::jlimit(1,2,lane.sound.unison);
            lane.sound.width=juce::jmin(.18f,lane.sound.width);
            lane.sound.subLevel=juce::jlimit(.10f,.34f,lane.sound.subLevel+.08f);
            lane.sound.reverb=juce::jmin(.025f,lane.sound.reverb);
            lane.sound.delay=0.f; lane.sound.chorus=0.f;
            lane.sound.release=juce::jmin(.34f,lane.sound.release);
        }
        else if(lane.name=="SUB")
        {
            lane.sound.oscA=WaveShape::sine; lane.sound.oscB=WaveShape::sine;
            lane.sound.oscMix=.08f; lane.sound.unison=1; lane.sound.detune=0.f;
            lane.sound.width=0.f; lane.sound.subLevel=0.f; lane.sound.noiseLevel=0.f;
            lane.sound.filterMode=FilterMode::lowpass;
            lane.sound.cutoff=125.f; lane.sound.resonance=.08f; lane.sound.filterEnv=0.f;
            lane.sound.attack=.004f; lane.sound.decay=.08f; lane.sound.sustain=.92f; lane.sound.release=.16f;
            lane.sound.drive=.025f; lane.sound.chorus=0.f; lane.sound.reverb=0.f; lane.sound.delay=0.f;
            lane.sound.lfoCutoff=0.f; lane.sound.lfoPitch=0.f; lane.sound.lfoMorphA=0.f; lane.sound.lfoMorphB=0.f;
        }
        else if(lane.name=="CHORDS")
        {
            lane.sound.unison=juce::jlimit(1,3,lane.sound.unison);
            lane.sound.subLevel=0.f; lane.sound.noiseLevel=juce::jmin(.04f,lane.sound.noiseLevel);
            lane.sound.reverb=juce::jmin(.09f,lane.sound.reverb);
            lane.sound.delay=juce::jmin(.045f,lane.sound.delay);
            lane.sound.release=juce::jmin(.48f,lane.sound.release);
        }
        else if(lane.name=="PLUCK")
        {
            lane.sound.unison=juce::jlimit(1,2,lane.sound.unison);
            lane.sound.subLevel=0.f;
            lane.sound.reverb=juce::jmin(.055f,lane.sound.reverb);
            lane.sound.delay=juce::jmin(.07f,lane.sound.delay);
            lane.sound.release=juce::jmin(.26f,lane.sound.release);
        }
        else if(lane.name=="PAD")
        {
            lane.sound.unison=juce::jlimit(1,3,lane.sound.unison);
            lane.sound.subLevel=0.f; lane.sound.noiseLevel=juce::jmin(.06f,lane.sound.noiseLevel);
            lane.sound.reverb=juce::jmin(.14f,lane.sound.reverb);
            lane.sound.delay=juce::jmin(.04f,lane.sound.delay);
            lane.sound.release=juce::jmin(.78f,lane.sound.release);
        }
        else if(lane.name=="LEAD")
        {
            lane.sound.unison=juce::jlimit(2,4,lane.sound.unison);
            lane.sound.subLevel=juce::jmin(.025f,lane.sound.subLevel);
            lane.sound.reverb=juce::jmin(.10f,lane.sound.reverb);
            lane.sound.delay=juce::jmin(.10f,lane.sound.delay);
            lane.sound.release=juce::jmin(.44f,lane.sound.release);
        }
        else if(lane.name=="COUNTER")
        {
            lane.sound.unison=juce::jlimit(1,2,lane.sound.unison);
            lane.sound.subLevel=0.f;
            lane.sound.reverb=juce::jmin(.06f,lane.sound.reverb);
            lane.sound.delay=juce::jmin(.05f,lane.sound.delay);
            lane.sound.release=juce::jmin(.34f,lane.sound.release);
        }
        else if(lane.name=="FX / TRANSITIONS")
        {
            lane.sound.unison=1; lane.sound.subLevel=0.f;
            lane.sound.chorus=juce::jmin(.08f,lane.sound.chorus);
            lane.sound.reverb=juce::jmin(.08f,lane.sound.reverb);
            lane.sound.delay=0.f;
            lane.sound.release=juce::jmin(.45f,lane.sound.release);
        }

        lane.sound.name = lane.name + " • Generated";
    }
}

void SongArrangement::addDrums(uint64_t seed, bool energetic)
{
    ArrangementLane kick{"KICK",10,true}, snare{"SNARE / CLAP",10,true}, hats{"HATS",10,true}, perc{"PERCUSSION",10,true};
    const auto p = sourcePrompt.toLowerCase();
    const bool house = p.contains("house") || p.contains("future rave") || p.contains("edm");
    const bool trap = p.contains("trap") || p.contains("hip hop");
    const bool dnb = p.contains("drum and bass") || p.contains("dnb");
    const int groove = plan.drumGroove;
    const int hatMode = plan.hatMode;
    const float swing = .012f + random01(seed,13) * .062f;

    for(int bar=0;bar<bars;++bar)
    {
        const ArrangementSection* section=nullptr;
        for(const auto& s:sections) if(sectionContains(s,bar)){section=&s;break;}
        const float energy=section?section->energy:.4f;
        const juce::String sectionName=section?section->name:juce::String();
        const bool intro=sectionName.contains("INTRO");
        const bool build=sectionName.contains("BUILD");
        const bool breakdown=sectionName.contains("BREAKDOWN");
        const bool drop=sectionName.contains("DROP") || sectionName.contains("HOOK");
        const bool chorus=sectionName.contains("CHORUS");
        const bool sectionStart=section && bar==section->startBar;
        const bool sectionEnd=section && bar==section->startBar+section->bars-1;
        const double b=bar*beatsPerBar;
        const uint64_t bs=(uint64_t)bar*97ULL;

        if(!breakdown && (!intro || bar-section->startBar>=juce::jmax(1,section->bars/2)))
        {
            if(dnb)
            {
                addNote(kick,36,b,.09,drop?118:106);
                addNote(kick,36,b+2.5,.08,104);
                if(random01(seed,1000+bs)>.35f) addNote(kick,36,b+1.75,.07,92);
                if(drop && random01(seed,1010+bs)>.50f) addNote(kick,36,b+3.25,.07,96);
            }
            else if(house)
            {
                for(int q=0;q<4;++q)
                    if(!intro || q==0 || q==2 || bar-section->startBar>=section->bars-2)
                        addNote(kick,36,b+q,.10,drop?118:101+(q==0?5:0));
                if((drop||build||chorus) && ((bar+groove)%4==3))
                    addNote(kick,36,b+3.5,.08,88+(int)(random01(seed,1020+bs)*20.f));
                if(drop && groove%3==2 && bar%2==0)
                    addNote(kick,36,b+1.75,.07,86);
            }
            else if(trap)
            {
                addNote(kick,36,b,.11,drop?118:104);
                if(random01(seed,1100+bs)>.35f) addNote(kick,36,b+1.5+.25*(groove%2),.09,96);
                if(random01(seed,1110+bs)>.48f) addNote(kick,36,b+2.75,.09,104);
                if(drop && random01(seed,1120+bs)>.5f) addNote(kick,36,b+3.5,.07,92);
            }
            else
            {
                addNote(kick,36,b,.11,108);
                addNote(kick,36,b+1.5+.5*(groove%3),.10,drop?116:100);
                if((drop||chorus) && random01(seed,1130+bs)>.38f) addNote(kick,36,b+3.25,.08,94);
            }
        }

        if(!intro && !breakdown)
        {
            if(trap || dnb)
            {
                addNote(snare,38,b+2.0,.12,drop?114:104);
                if((drop||chorus) && random01(seed,1200+bs)>.55f) addNote(snare,39,b+1.75,.07,72);
            }
            else
            {
                addNote(snare,38,b+1.0,.12,98+(drop?12:0));
                addNote(snare,38,b+3.0,.12,102+(drop?10:0));
                if((bar+groove)%4==2) addNote(snare,39,b+2.75,.07,64+(int)(random01(seed,1210+bs)*18.f));
            }
        }

        const int hatSteps = dnb ? 16 : (house ? ((drop||build||chorus)?8:4) : (drop ? (hatMode>=2?16:8) : ((build||chorus||energetic)?8:4)));
        for(int h=0;h<hatSteps;++h)
        {
            const int quarter=juce::jmax(1,hatSteps/4);
            const bool strong=(h%quarter==0);
            float skip=breakdown?.48f:(drop?.08f:.16f);
            if(!strong && random01(seed,1300+bs+h)<skip) continue;
            double beat=b+h*(4.0/hatSteps);
            if(h%2==1) beat+=swing*(hatMode==1?1.0:.55);
            bool open=(drop||chorus) && ((h+groove)%8==3 || (hatMode>=2 && h%8==7));
            if(house&&hatSteps==8)open=(h%2==1)&&((h+bar+groove)%4==1);
            const int vel=juce::jlimit(32,108,46+(strong?15:0)+(int)(random01(seed,1400+bs+h)*32.f));
            addNote(hats,open?46:42,beat,.045+(open?.10:0.0),vel);
        }

        if(sectionStart&&(drop||chorus))
        {
            addNote(perc,49,b,.18,drop?112:98); // crash
            if(sectionName.contains("FINAL")||sectionName.contains("HOOK"))
                addNote(perc,57,b,.22,118); // impact
        }

        if(drop||chorus)
        {
            if(house)
            {
                if((bar+groove)%2==0)
                    addNote(perc,37,b+3.5,.055,58+(int)(random01(seed,1500+bs)*18.f));
                if((bar+groove)%4==3)
                    addNote(perc,39,b+2.75,.06,62+(int)(random01(seed,1501+bs)*16.f));
            }
            else
            {
                const int percCount=1+((bar+groove)%3);
                for(int k=0;k<percCount;++k)
                {
                    const double pos=.25*((k*5+groove+bar)%16);
                    if(pos<.1) continue;
                    addNote(perc,(k%2)?37:39,b+pos,.06,58+(int)(random01(seed,1500+bs+k)*26.f));
                }
            }
        }
        else if(breakdown && random01(seed,1510+bar)>.62f)
        {
            addNote(perc,39,b+2.5,.10,58+(int)(random01(seed,1520+bar)*18.f));
        }

        if(build && sectionEnd)
        {
            const int divisions = house ? (hatMode>=2?16:8) : (hatMode>=2?24:16);
            for(int s=0;s<divisions;++s)
            {
                if(s<4 && groove==3 && s%2==1) continue;
                addNote(snare,38,b+s*(4.0/divisions),.05,
                        juce::jlimit(48,127,54+s*(64/divisions)+(int)(random01(seed,1600+s+bar)*8.f)));
            }
        }
    }

    lanes.push_back(std::move(kick));
    lanes.push_back(std::move(snare));
    lanes.push_back(std::move(hats));
    lanes.push_back(std::move(perc));
}

void SongArrangement::addHarmony(uint64_t seed)
{
    ArrangementLane bass{"BASS",2,false}, sub{"SUB",3,false}, chords{"CHORDS",4,false}, pluck{"PLUCK",5,false}, pad{"PAD",6,false};

    const int bassMode=plan.bassMode;
    const int chordMode=plan.chordMode;
    const int arpMode=plan.arpMode;

    static constexpr int arpPatterns[6][8]={
        {0,1,2,1,3,1,2,1},
        {0,2,1,3,0,2,1,2},
        {0,1,3,0,2,1,0,3},
        {2,1,0,1,3,1,0,1},
        {0,3,0,1,2,0,3,1},
        {1,0,2,1,3,2,1,0}
    };

    auto sectionAtBeat=[&](double beat)->std::pair<const ArrangementSection*,int>
    {
        const int bar=juce::jlimit(0,bars-1,(int)std::floor(beat/beatsPerBar));
        for(size_t i=0;i<sections.size();++i)
            if(sectionContains(sections[i],bar))return {&sections[i],(int)i};
        return {nullptr,0};
    };

    auto foldBass=[&](int note)
    {
        while(note<32)note+=12;
        while(note>52)note-=12;
        return note;
    };

    // CHORDS and PAD interpret the same Harmony DNA differently.
    for(size_t hi=0;hi<harmonyEvents.size();++hi)
    {
        const auto& event=harmonyEvents[hi];
        const auto [section,sectionIndex]=sectionAtBeat(event.beat);
        if(section==nullptr)continue;

        const auto sectionName=section->name;
        const bool intro=sectionName.contains("INTRO");
        const bool verse=sectionName.contains("VERSE");
        const bool build=sectionName.contains("BUILD");
        const bool drop=sectionName.contains("DROP")||sectionName.contains("HOOK");
        const bool breakdown=sectionName.contains("BREAKDOWN");
        const bool chorus=sectionName.contains("CHORUS");
        const bool finalHook=sectionName.contains("FINAL");
        const float energy=section->energy;

        auto tones=chordTonesFor(event);
        for(auto& n:tones)n+=12;

        int repeats=1;
        if(drop||chorus||finalHook)
            repeats=(chordMode==0?1:(chordMode==1?2:(chordMode==2?4:2)));
        else if(build)
            repeats=chordMode>=3?2:1;

        const double unit=event.length/(double)repeats;
        for(int r=0;r<repeats;++r)
        {
            const uint64_t rs=(uint64_t)hi*97ULL+(uint64_t)r;
            if(verse&&r>0&&random01(domains.voicing,0x5100+rs)<.26f)continue;

            double startBeat=event.beat+r*unit;
            if((drop||chorus)&&chordMode==3&&r%2)startBeat+=.10;
            const double len=juce::jmin(unit*.88,drop||chorus?1.20:event.length*.92);
            const int baseVel=juce::jlimit(45,116,58+(int)(energy*28.f)+(int)(random01(domains.voicing,0x5200+rs)*10.f));

            addNote(chords,tones[0],startBeat,len,baseVel);
            addNote(chords,tones[1],startBeat,len,juce::jmax(42,baseVel-3));
            addNote(chords,tones[2],startBeat,len,juce::jmax(40,baseVel-5));

            if(event.extension>0&&random01(domains.voicing,0x5300+rs)<(.52f+harmonyPlan.extensionProbability))
                addNote(chords,tones[3],startBeat,juce::jmin(len*.92,1.6),juce::jmax(38,baseVel-9));

            if(finalHook&&r%2==0&&random01(domains.voicing,0x5400+rs)<.58f)
                addNote(chords,tones[2]+12,startBeat,juce::jmin(len*.78,1.1),juce::jmax(36,baseVel-13));
        }

        // PAD is not a copy of CHORDS: it uses upper voices, often omits the root,
        // holds through changes, and can emphasize extensions/suspensions.
        if(intro||verse||breakdown||chorus)
        {
            const float padChance=breakdown?.94f:(intro?.82f:.68f);
            if(random01(domains.pad,0x6100+hi)<padChance)
            {
                const double padStart=event.beat+(random01(domains.pad,0x6110+hi)<.22f?.25:0.0);
                const double padLen=juce::jmin(event.length*1.18,8.0);
                const int octave=random01(domains.pad,0x6120+hi)>.64f?12:0;

                addNote(pad,tones[1]+octave,padStart,padLen,42+(int)(random01(domains.pad,0x6130+hi)*10.f));
                addNote(pad,tones[2]+octave,padStart,padLen,40+(int)(random01(domains.pad,0x6140+hi)*9.f));

                if(event.extension>0||random01(domains.pad,0x6150+hi)<harmonyPlan.extensionProbability)
                    addNote(pad,tones[3]+octave,padStart,padLen*.94,37+(int)(random01(domains.pad,0x6160+hi)*9.f));

                if(random01(domains.pad,0x6170+hi)<.34f)
                    addNote(pad,tones[0]+12+octave,padStart,padLen*.82,36+(int)(random01(domains.pad,0x6180+hi)*8.f));
            }
        }
    }

    // BASS gets an independent groove. Harmony supplies legal roots/tension only;
    // the exact chord MIDI is never copied.
    for(int bar=0;bar<bars;++bar)
    {
        const double barBeat=bar*beatsPerBar;
        const auto [section,sectionIndex]=sectionAtBeat(barBeat);
        if(section==nullptr)continue;

        const auto name=section->name;
        const bool intro=name.contains("INTRO");
        const bool verse=name.contains("VERSE");
        const bool build=name.contains("BUILD");
        const bool drop=name.contains("DROP")||name.contains("HOOK");
        const bool breakdown=name.contains("BREAKDOWN");
        const bool chorus=name.contains("CHORUS");
        const bool finalHook=name.contains("FINAL");
        const int localBar=bar-section->startBar;
        const float energy=section->energy;
        const uint64_t bs=(uint64_t)bar*173ULL;

        if(!intro||localBar>=juce::jmax(1,section->bars/2))
        {
            static constexpr double pos0[4]={0.0,1.0,2.0,3.0};
            static constexpr double pos1[6]={0.0,.75,1.5,2.0,2.75,3.5};
            static constexpr double pos2[5]={0.0,1.0,1.75,2.5,3.25};
            static constexpr double pos3[7]={0.0,.5,1.25,2.0,2.5,3.0,3.75};
            static constexpr double pos4[5]={0.0,1.5,2.0,2.75,3.5};

            const double* positions=pos0;
            int count=drop||chorus?4:2;
            if(bassMode==1){positions=pos1;count=(drop||chorus)?6:4;}
            else if(bassMode==2){positions=pos2;count=(drop||finalHook)?5:3;}
            else if(bassMode==3){positions=pos3;count=(drop||chorus)?7:4;}
            else if(bassMode==4){positions=pos4;count=(drop||chorus)?5:3;}

            if(breakdown)count=juce::jmin(count,2);

            for(int i=0;i<count;++i)
            {
                const double pos=positions[i];
                if(i>0&&random01(domains.bass,0x7100+bs+i)<(verse?.20f:.08f))continue;
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;

                int degree=h->scaleDegree;
                int note=rootMidi+scaleSemitoneForDegree(degree)-12;

                const float move=random01(domains.bass,0x7200+bs+i);
                if(i>0&&move<harmonyPlan.passingProbability)
                {
                    // diatonic approach/passing note rather than a copied chord root
                    const int dir=random01(domains.bass,0x7210+bs+i)>.5f?1:-1;
                    note=rootMidi+scaleSemitoneForDegree(degree+dir)-12;
                }
                else if(move<.34f&&i%3==2)note+=7;
                else if(move>.84f&&(drop||finalHook))note+=12;

                note=foldBass(note);
                const double len=bassMode==3?.28:(drop||chorus?.48:.72);
                addNote(bass,note,barBeat+pos,len,
                        juce::jlimit(62,120,76+(int)(energy*28.f)+(int)(random01(domains.bass,0x7300+bs+i)*12.f)));
            }
        }

        // SUB owns its own rhythm domain and only follows harmonic fundamentals.
        if(!intro||localBar>=juce::jmax(1,section->bars/2))
        {
            int subCount=1;
            double positions[4]={0.0,2.0,3.0,1.0};
            if(drop||chorus||finalHook)
                subCount=random01(domains.sub,0x7400+bs)>.52f?2:3;
            else if(verse&&random01(domains.sub,0x7410+bs)>.62f)
                subCount=2;
            if(breakdown)subCount=(localBar%2==0)?1:0;

            for(int i=0;i<subCount;++i)
            {
                const double pos=positions[i];
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                int note=rootMidi+scaleSemitoneForDegree(h->scaleDegree)-24;
                while(note<24)note+=12;
                while(note>47)note-=12;
                const double len=subCount==1?juce::jmin(3.30,4.0-pos):juce::jmin(1.45,4.0-pos);
                addNote(sub,note,barBeat+pos,len,
                        juce::jlimit(52,96,60+(int)(energy*22.f)+(int)(random01(domains.sub,0x7420+bs+i)*8.f)));
            }
        }

        // PLUCK owns its rhythm; at each onset it asks Harmony DNA for the local
        // chord, then mixes chord tones with scale passing tones.
        if((energy>.5f||chorus)&&!breakdown)
        {
            const int steps=(drop||finalHook)?8:(chorus?6:(build?6:4));
            for(int i=0;i<steps;++i)
            {
                const uint64_t ps=bs+(uint64_t)i;
                if(i>0&&random01(domains.pluck,0x8100+ps)<(drop?.08f:.20f))continue;

                double pos=i*(4.0/steps);
                if(i%2&&random01(domains.pluck,0x8110+ps)<.45f)
                    pos+=((random01(domains.pluck,0x8120+ps)-.5f)*.08f);

                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                const auto tones=chordTonesFor(*h);
                const int patternIndex=arpPatterns[arpMode][(i+bar+sectionIndex)%8];

                int note=tones[(size_t)(patternIndex%4)]+24;
                if(random01(domains.pluck,0x8130+ps)<harmonyPlan.passingProbability)
                {
                    const int dir=random01(domains.pluck,0x8140+ps)>.5f?1:-1;
                    note=rootMidi+scaleSemitoneForDegree(h->scaleDegree+dir)+24;
                }
                if(finalHook&&i%4==3&&random01(domains.pluck,0x8150+ps)>.50f)note+=12;

                addNote(pluck,note,barBeat+juce::jlimit(0.0,3.90,pos),
                        .11+random01(domains.pluck,0x8160+ps)*.22,
                        juce::jlimit(48,108,54+(int)(energy*24.f)+(int)(random01(domains.pluck,0x8170+ps)*14.f)));
            }
        }
    }

    lanes.push_back(std::move(bass));
    lanes.push_back(std::move(sub));
    lanes.push_back(std::move(chords));
    lanes.push_back(std::move(pluck));
    lanes.push_back(std::move(pad));
}

void SongArrangement::addMelody(uint64_t seed, bool energetic)
{
    ArrangementLane lead{"LEAD",1,false}, counter{"COUNTER",7,false};

    static constexpr int minorScale[7]={0,2,3,5,7,8,10};
    static constexpr int majorScale[7]={0,2,4,5,7,9,11};
    const int* scale=minor?minorScale:majorScale;
    static constexpr double rhythmPos[10][8]={
        {0.0,1.0,2.0,3.0,0,0,0,0},
        {0.5,1.5,2.5,3.5,0,0,0,0},
        {0.0,.75,1.5,2.25,3.25,0,0,0},
        {0.0,1.5,3.0,0,0,0,0,0},
        {0.0,.5,1.0,1.5,2.0,2.5,3.0,3.5},
        {0.0,.75,1.5,2.5,3.25,0,0,0},
        {0.0,1.25,2.75,0,0,0,0,0},
        {0.0,.5,1.25,2.0,2.75,3.5,0,0},
        {.5,1.25,2.0,3.0,3.5,0,0,0},
        {0.0,.25,1.5,2.0,2.5,3.75,0,0}
    };
    static constexpr int rhythmCount[10]={4,4,5,3,8,5,3,6,5,6};

    const auto p=sourcePrompt.toLowerCase();
    const bool tech=p.contains("tech house")||p.contains("minimal house");
    const bool dnb=p.contains("drum and bass")||p.contains("dnb");
    const bool cinematic=p.contains("cinematic")||p.contains("film");
    const bool pop=p.contains("pop")||p.contains("radio");
    const bool progressive=p.contains("progressive house")||p.contains("melodic house");
    const bool trance=p.contains("trance");
    const bool tropical=p.contains("tropical");

    const int architecture=plan.melodyArchetype%12;
    const int rhythmBase=plan.rhythmFamily%10;

    auto scaleNote=[&](int degree,int registerSemitones)
    {
        int oct=0;
        while(degree<0){degree+=7;--oct;}
        while(degree>=7){degree-=7;++oct;}
        int note=rootMidi+registerSemitones+scale[degree]+12*oct;

        // Never hard-clamp MIDI pitch: that changes pitch class and can turn an
        // in-key melody into an out-of-key boundary note. Fold by octaves instead.
        while(note<52)note+=12;
        while(note>96)note-=12;
        return note;
    };

    auto sectionForBar=[&](int bar)->std::pair<const ArrangementSection*,int>
    {
        for(size_t i=0;i<sections.size();++i)
            if(sectionContains(sections[i],bar))return {&sections[i],(int)i};
        return {nullptr,0};
    };

    struct ChordInfo { int degreeIndex=0; int extension=0; bool borrowed=false; };
    auto chordInfoAtBeat=[&](double beat)
    {
        ChordInfo info;
        if(const auto* h=harmonyAtBeat(beat))
        {
            info.degreeIndex=h->scaleDegree;
            info.extension=h->extension;
            info.borrowed=h->borrowed;
        }
        return info;
    };

    for(int bar=0;bar<bars;++bar)
    {
        const auto [section,sectionIndex]=sectionForBar(bar);
        if(section==nullptr)continue;

        const juce::String sectionName=section->name;
        const bool intro=sectionName.contains("INTRO");
        const bool verse=sectionName.contains("VERSE");
        const bool build=sectionName.contains("BUILD");
        const bool drop=sectionName.contains("DROP");
        const bool breakdown=sectionName.contains("BREAKDOWN");
        const bool chorus=sectionName.contains("CHORUS");
        const bool finalHook=sectionName.contains("FINAL")||sectionName.contains("HOOK");
        const int localBar=bar-section->startBar;
        const float energy=section->energy;

        bool active=!intro;
        if(intro)active=localBar>=juce::jmax(2,section->bars/2);
        if(breakdown)active=(localBar%2==0);
        if(!active)continue;

        const int phraseBars=juce::jmax(2,plan.phraseBars);
        const int barInPhrase=localBar%phraseBars;
        const int phraseIndex=localBar/phraseBars;

        // DROP and FINAL HOOK share a recognizable song identity, but the final hook
        // develops it. Other sections get genuinely separate theme seeds.
        const int themeGroup=(drop||finalHook)?3:(chorus?5:sectionIndex);
        const int devPhrase=(plan.development>.72f)?phraseIndex:
                            (plan.development>.48f?phraseIndex/2:0);
        const uint64_t phraseSeed=mix64(seed
            ^ ((uint64_t)themeGroup+1ULL)*0x9e3779b97f4a7c15ULL
            ^ ((uint64_t)devPhrase+1ULL)*0xbf58476d1ce4e5b9ULL
            ^ ((uint64_t)architecture+1ULL)*0x94d049bb133111ebULL);

        const auto chord=chordInfoAtBeat(bar*beatsPerBar);
        const int registerBase=tech?12:((drop||chorus||finalHook)?24:12);
        const int startDegree=(plan.startingDegree
            +(int)(random01(phraseSeed,0x2001)*7.f)
            +barInPhrase)%7;

        int family=(rhythmBase+sectionIndex+(int)(random01(phraseSeed,0x2002)*3.f))%10;
        if(tech)family=(family%2)?1:3;
        else if(dnb)family=random01(phraseSeed,0x2003)>.5f?4:7;
        else if(cinematic)family=random01(phraseSeed,0x2004)>.5f?3:6;
        else if(trance)family=random01(phraseSeed,0x2005)>.45f?4:7;

        int count=rhythmCount[family];
        if(architecture==2)count=juce::jmin(count,4);      // octave anthem
        if(architecture==3)count=juce::jmin(count,3);      // sparse motif
        if(architecture==4)count=juce::jlimit(2,4,count);  // lyrical
        if(architecture==5)count=juce::jmax(count,6);      // sync riff
        if(architecture==10)count=juce::jmax(count,5);     // offbeat hook
        if(plan.density>.80f&&!cinematic)count=juce::jmin(8,count+1);
        if(plan.density<.52f)count=juce::jmax(2,count-1);

        double positions[8]{};
        for(int i=0;i<count;++i)positions[i]=rhythmPos[family][i];

        // Each architecture changes the actual rhythmic construction, not just pitches.
        if(architecture==2)
        {
            static constexpr double p2[4]={0.0,1.5,2.5,3.5};
            for(int i=0;i<count;++i)positions[i]=p2[i];
        }
        else if(architecture==3)
        {
            static constexpr double p3[3]={0.5,2.0,3.25};
            for(int i=0;i<count;++i)positions[i]=p3[i];
        }
        else if(architecture==4)
        {
            static constexpr double p4[4]={0.0,1.25,2.5,3.25};
            for(int i=0;i<count;++i)positions[i]=p4[i];
        }
        else if(architecture==5)
        {
            static constexpr double p5[8]={0.0,.5,1.25,1.75,2.25,2.75,3.25,3.75};
            count=juce::jmin(8,count);
            for(int i=0;i<count;++i)positions[i]=p5[i];
        }
        else if(architecture==10)
        {
            static constexpr double p10[6]={.5,1.0,1.75,2.5,3.0,3.5};
            count=juce::jmin(6,count);
            for(int i=0;i<count;++i)positions[i]=p10[i];
        }

        int lastDegree=startDegree;
        int repeated=0;
        for(int i=0;i<count;++i)
        {
            const uint64_t salt=(uint64_t)bar*256ULL+(uint64_t)i;
            const bool strong=positions[i]<.08||std::abs(std::fmod(positions[i],1.0))<.08;

            float restChance=plan.restAmount;
            if(drop||finalHook)restChance*=.34f;
            if(verse)restChance*=1.18f;
            if(breakdown)restChance=juce::jmax(restChance,.30f);
            if(tech)restChance=juce::jmax(restChance,.24f);
            if(architecture==3)restChance*=.55f; // already sparse by construction
            if(!strong&&random01(phraseSeed,0x2100+salt)<restChance)continue;

            int degree=startDegree;
            int octaveExtra=0;

            switch(architecture)
            {
                case 0: // CALL / RESPONSE
                {
                    const int cell=i%4;
                    const int call[]={0,2,4,3};
                    degree=startDegree+call[cell];
                    if(barInPhrase>=phraseBars/2)
                    {
                        const int answer[]={4,2,1,0};
                        degree=startDegree+answer[cell];
                        if(i==count-1)degree=chord.degreeIndex;
                    }
                    break;
                }
                case 1: // ARPEGGIATED HOOK
                {
                    static constexpr int chordDegrees[6]={0,2,4,2,4,1};
                    degree=chordInfoAtBeat(bar*beatsPerBar+positions[i]).degreeIndex+chordDegrees[(i+barInPhrase)%6];
                    if(i%3==2&&random01(phraseSeed,0x2200+salt)>.52f)octaveExtra=12;
                    break;
                }
                case 2: // OCTAVE ANTHEM
                {
                    const int cell=i%4;
                    const int anthem[]={0,0,4,2};
                    degree=startDegree+anthem[cell];
                    octaveExtra=(cell==1||((barInPhrase+i)%4==3))?12:0;
                    break;
                }
                case 3: // SPARSE SIGNATURE MOTIF
                {
                    const int sparse[]={0,3,1};
                    degree=startDegree+sparse[i%3];
                    if(barInPhrase%2==1&&i==1)degree+=2;
                    break;
                }
                case 4: // LONG LYRIC LINE
                {
                    const int direction=((phraseSeed>>8)&1ULL)?1:-1;
                    degree=startDegree+direction*(barInPhrase*2+i);
                    if(i==count-1&&barInPhrase==phraseBars-1)degree=chord.degreeIndex;
                    break;
                }
                case 5: // SYNCOPATED RIFF
                {
                    static constexpr int riff[8]={0,0,3,1,4,2,1,5};
                    degree=startDegree+riff[(i+barInPhrase)%8];
                    break;
                }
                case 6: // PEDAL TONE + MOVING ANSWER
                {
                    if(i%2==0)degree=startDegree;
                    else degree=startDegree+1+((barInPhrase+i*2)%5);
                    break;
                }
                case 7: // SEQUENCED CELL
                {
                    static constexpr int cell[3]={0,2,1};
                    const int transposition=(barInPhrase%4==0?0:(barInPhrase%4==1?2:(barInPhrase%4==2?4:1)));
                    degree=startDegree+cell[i%3]+transposition;
                    break;
                }
                case 8: // FALLING HOOK
                {
                    degree=startDegree+6-((i+barInPhrase*2)%7);
                    if(i==count-1)degree=chord.degreeIndex;
                    break;
                }
                case 9: // RISING LIFT + RESOLUTION
                {
                    const int span=i+barInPhrase*2;
                    degree=startDegree+(span%7);
                    if(barInPhrase==phraseBars-1&&i>=count-2)degree=chord.degreeIndex;
                    break;
                }
                case 10: // OFFBEAT HOOK
                {
                    static constexpr int off[6]={0,2,0,4,1,3};
                    degree=startDegree+off[i%6];
                    break;
                }
                default: // WIDE LEAP / RESOLVE
                {
                    static constexpr int leap[8]={0,4,1,5,2,0,4,1};
                    degree=startDegree+leap[(i+barInPhrase)%8];
                    if(i>0&&i%2==0)degree=lastDegree+(degree>lastDegree?-1:1);
                    break;
                }
            }

            // Harmony constrains the melody but does not dictate it. Only structural
            // anchors strongly prefer chord tones; ordinary beats are usually free
            // scale tones, allowing 2nd/4th/6th/7th tension before resolution.
            const auto localHarmony=chordInfoAtBeat(bar*beatsPerBar+positions[i]);
            const bool phraseEnding=(barInPhrase==phraseBars-1&&i==count-1);
            const bool barAnchor=positions[i]<.08;
            const float chordToneChance=phraseEnding?.90f:(barAnchor?.70f:(strong?.34f:.22f));
            if(random01(phraseSeed,0x2300+salt)<chordToneChance)
            {
                const int toneChoice=(int)(random01(phraseSeed,0x2310+salt)*3.f)%3;
                if(toneChoice==0)degree=localHarmony.degreeIndex;
                else if(toneChoice==1)degree=localHarmony.degreeIndex+2;
                else degree=localHarmony.degreeIndex+4;
            }
            else if(!strong&&random01(phraseSeed,0x2320+salt)<.24f)
            {
                const int dir=random01(phraseSeed,0x2330+salt)>.5f?1:-1;
                degree+=dir; // scale passing/approach tone
            }

            if(phraseEnding)
            {
                if(plan.cadenceStyle==0)degree=localHarmony.degreeIndex;
                else if(plan.cadenceStyle==1)degree=0;
                else if(plan.cadenceStyle==2)degree=2;
                else degree=4;
                octaveExtra=0;
            }

            while(degree<0)degree+=7;
            while(degree>=14)degree-=7;

            if(degree==lastDegree)++repeated;else repeated=0;
            if(repeated>=2){degree=(degree+2)%7;repeated=0;}

            int registerNow=registerBase+octaveExtra;
            if(cinematic&&breakdown)registerNow=12;
            if(finalHook&&plan.octaveRange>=2&&random01(phraseSeed,0x2400+salt)>.74f)registerNow+=12;
            if(verse&&random01(phraseSeed,0x2410+salt)<.16f)registerNow-=12;

            double pos=positions[i];
            if(!strong&&plan.syncopation>.45f&&random01(phraseSeed,0x2500+salt)<plan.syncopation*.42f)
                pos+=random01(phraseSeed,0x2510+salt)>.5f?.125:-.125;
            pos=juce::jlimit(0.0,3.90,pos);

            double length=.34;

            // Genre articulation outranks architecture. An architecture changes
            // phrase construction, but must not turn a tech-house riff into a
            // cinematic legato line or a cinematic theme into a staccato riff.
            if(tech)length=.10+random01(phraseSeed,0x2630+salt)*.13;
            else if(dnb)length=.08+random01(phraseSeed,0x2631+salt)*.14;
            else if(cinematic)length=.62+random01(phraseSeed,0x2632+salt)*.78;
            else if(architecture==4)length=.58+random01(phraseSeed,0x2600+salt)*.62;
            else if(architecture==3)length=.28+random01(phraseSeed,0x2610+salt)*.38;
            else if(architecture==2)length=.22+random01(phraseSeed,0x2620+salt)*.28;
            else if(architecture==5||architecture==10)length=.12+random01(phraseSeed,0x2633+salt)*.18;
            else length=.20+random01(phraseSeed,0x2640+salt)*.42;

            const int velocity=juce::jlimit(52,124,
                60+(int)(energy*38.f)+(strong?7:0)+(int)(random01(phraseSeed,0x2700+salt)*14.f)-7);

            addNote(lead,scaleNote(degree,registerNow),bar*beatsPerBar+pos,length,velocity);
            lastDegree=degree;
        }

        // Build sections climb toward the next section with a musically clear pickup.
        if(build&&barInPhrase>=phraseBars/2)
        {
            const int pickupDegree=juce::jlimit(0,13,startDegree+barInPhrase+2);
            addNote(lead,scaleNote(pickupDegree,registerBase),
                    bar*beatsPerBar+3.5,.16,juce::jlimit(72,122,80+(int)(energy*34.f)));
        }

        // Counter melody is generated from an independent rhythm/contour family and
        // deliberately occupies the second half of the bar so it answers the lead.
        if((drop||chorus||finalHook)&&!tech)
        {
            const uint64_t cs=mix64(domains.counter ^ ((uint64_t)sectionIndex+1ULL)*0x9e3779b97f4a7c15ULL ^ ((uint64_t)bar+1ULL)*0xbf58476d1ce4e5b9ULL);
            const int counterCount=finalHook?4:3;
            int cd=(startDegree+3+(int)(random01(cs,1)*4.f))%7;
            for(int i=0;i<counterCount;++i)
            {
                if(i>0&&random01(cs,20+bar*8+i)<.18f)continue;
                const double pos=1.75+i*(2.0/counterCount)
                    +(i%2?.08*(random01(cs,40+i+bar)-.5):0.0);
                const int move=((int)(random01(cs,60+i+bar)*5.f)-2);
                cd=juce::jlimit(0,6,cd+move);
                const int reg=finalHook?24:12;
                addNote(counter,scaleNote(cd,reg),bar*beatsPerBar+juce::jlimit(0.0,3.85,pos),
                        .14+.20*random01(cs,90+i+bar),
                        juce::jlimit(48,100,56+(int)(energy*25.f)+(int)(random01(cs,110+i+bar)*10.f)));
            }
        }
    }

    // Final musical polish: preserve pitch class and rhythm identity while
    // octave-normalizing the line into a playable range and removing accidental
    // section-boundary leaps/repeated-note runs.
    std::sort(lead.notes.begin(),lead.notes.end(),
              [](const ArrangementNote& a,const ArrangementNote& b){return a.beat<b.beat;});

    int previousNote=-1;
    int repeatedRun=0;
    for(auto& n:lead.notes)
    {
        while(n.note<52)n.note+=12;
        while(n.note>96)n.note-=12;

        if(previousNote>=0)
        {
            while(n.note-previousNote>12&&n.note-12>=52)n.note-=12;
            while(previousNote-n.note>12&&n.note+12<=96)n.note+=12;

            repeatedRun=(n.note==previousNote)?repeatedRun+1:0;
            if(repeatedRun>=3)
            {
                if(n.note+12<=96)n.note+=12;
                else if(n.note-12>=52)n.note-=12;
                repeatedRun=0;
            }
        }

        n.length=juce::jlimit(.08,1.55,n.length);
        n.velocity=juce::jlimit(48,124,n.velocity);
        previousNote=n.note;
    }

    lanes.push_back(std::move(lead));
    lanes.push_back(std::move(counter));
}


void SongArrangement::addFx(uint64_t seed)
{
    ArrangementLane fx{"FX / TRANSITIONS",8,false};
    const int style=static_cast<int>(random01(seed,801)*4.f)%4;
    for(size_t i=0;i<sections.size();++i)
    {
        const auto& s=sections[i];
        const double boundary=s.startBar*beatsPerBar;
        if(s.startBar>0)
        {
            const double leadIn=(style==0?1.0:style==1?2.0:style==2?.5:4.0);
            const int riserNote=81+(int)(random01(seed,810+i)*9.f);
            addNote(fx,riserNote,juce::jmax(0.0,boundary-leadIn),juce::jmax(.25,leadIn-.08),
                    62+(int)(s.energy*36.f)+(int)(random01(seed,820+i)*10.f));
            if(random01(seed,830+i)>.25f)
                addNote(fx,72+(int)(random01(seed,840+i)*12.f),boundary+.125,.35+random01(seed,850+i)*.8,
                        68+(int)(s.energy*28.f));
        }
        if(s.energy>.75f)
        {
            addNote(fx,48+(style%2)*12,boundary,.20+random01(seed,860+i)*.35,
                    juce::jlimit(70,127,90+(int)(s.energy*30.f)));
            if(s.name.contains("FINAL") && random01(seed,870+i)>.3f)
                addNote(fx,36,boundary+.5,.55,108);
        }
    }
    lanes.push_back(std::move(fx));
}

bool SongArrangement::writeMidiFile(const juce::File& destination) const
{
    if (lanes.empty()) return false;
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(static_cast<int>(tpq));

    juce::MidiMessageSequence conductor;
    auto tempoMessage = juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::llround(60000000.0 / tempo)));
    tempoMessage.setTimeStamp(0); conductor.addEvent(tempoMessage);
    auto timeSig = juce::MidiMessage::timeSignatureMetaEvent(4,4); timeSig.setTimeStamp(0); conductor.addEvent(timeSig);
    for (const auto& section : sections)
    {
        auto marker = juce::MidiMessage::textMetaEvent(6, section.name);
        marker.setTimeStamp(section.startBar * beatsPerBar * tpq); conductor.addEvent(marker);
    }
    midi.addTrack(conductor);

    for (const auto& lane : lanes)
    {
        juce::MidiMessageSequence seq;
        auto name = juce::MidiMessage::textMetaEvent(3, lane.name); name.setTimeStamp(0); seq.addEvent(name);
        for (const auto& n : lane.notes)
        {
            auto on = juce::MidiMessage::noteOn(lane.midiChannel, n.note, static_cast<juce::uint8>(n.velocity));
            auto off = juce::MidiMessage::noteOff(lane.midiChannel, n.note);
            on.setTimeStamp(n.beat * tpq); off.setTimeStamp((n.beat + n.length) * tpq);
            seq.addEvent(on); seq.addEvent(off);
        }
        seq.updateMatchedPairs(); midi.addTrack(seq);
    }

    destination.deleteFile();
    juce::FileOutputStream out(destination);
    if (!out.openedOk()) return false;
    return midi.writeTo(out);
}


juce::ValueTree SongArrangement::toValueTree() const
{
    juce::ValueTree root("SONARA_ARRANGEMENT");
    root.setProperty("schema",1,nullptr);root.setProperty("prompt",sourcePrompt,nullptr);root.setProperty("bpm",tempo,nullptr);root.setProperty("bars",bars,nullptr);root.setProperty("rootMidi",rootMidi,nullptr);root.setProperty("minor",minor,nullptr);
    juce::ValueTree sectionTree("SECTIONS");
    for(const auto& s:sections){juce::ValueTree v("SECTION");v.setProperty("name",s.name,nullptr);v.setProperty("startBar",s.startBar,nullptr);v.setProperty("bars",s.bars,nullptr);v.setProperty("energy",s.energy,nullptr);sectionTree.addChild(v,-1,nullptr);}root.addChild(sectionTree,-1,nullptr);
    juce::ValueTree laneTree("LANES");
    for(const auto& lane:lanes){juce::ValueTree l("LANE");l.setProperty("name",lane.name,nullptr);l.setProperty("channel",lane.midiChannel,nullptr);l.setProperty("drums",lane.drums,nullptr);l.addChild(lane.sound.toValueTree(),-1,nullptr);juce::ValueTree notes("NOTES");for(const auto& n:lane.notes){juce::ValueTree v("NOTE");v.setProperty("note",n.note,nullptr);v.setProperty("velocity",n.velocity,nullptr);v.setProperty("beat",n.beat,nullptr);v.setProperty("length",n.length,nullptr);notes.addChild(v,-1,nullptr);}l.addChild(notes,-1,nullptr);laneTree.addChild(l,-1,nullptr);}root.addChild(laneTree,-1,nullptr);
    return root;
}

SongArrangement SongArrangement::fromValueTree(const juce::ValueTree& root)
{
    SongArrangement a;
    if(!root.isValid()||root.getType().toString()!="SONARA_ARRANGEMENT")return a;
    a.sourcePrompt=root.getProperty("prompt","").toString();a.tempo=juce::jlimit(60.0,200.0,(double)root.getProperty("bpm",128.0));a.bars=juce::jlimit(1,512,(int)root.getProperty("bars",defaultBars));a.rootMidi=juce::jlimit(0,127,(int)root.getProperty("rootMidi",53));a.minor=(bool)root.getProperty("minor",true);a.sections.clear();a.lanes.clear();
    auto st=root.getChildWithName("SECTIONS");for(int i=0;i<st.getNumChildren();++i){auto v=st.getChild(i);a.sections.push_back({v.getProperty("name","").toString(),(int)v.getProperty("startBar",0),(int)v.getProperty("bars",8),(float)v.getProperty("energy",.5)});}if(a.sections.empty())a.buildSections(0x7a1f2d4bULL);
    auto lt=root.getChildWithName("LANES");for(int i=0;i<lt.getNumChildren();++i){auto l=lt.getChild(i);ArrangementLane lane;lane.name=l.getProperty("name","Lane").toString();lane.midiChannel=juce::jlimit(1,16,(int)l.getProperty("channel",1));lane.drums=(bool)l.getProperty("drums",false);auto dna=l.getChildWithName("SoundDNA");if(dna.isValid())lane.sound=SoundDNA::fromValueTree(dna);auto notes=l.getChildWithName("NOTES");for(int j=0;j<notes.getNumChildren();++j){auto n=notes.getChild(j);lane.notes.push_back({juce::jlimit(0,127,(int)n.getProperty("note",60)),juce::jlimit(1,127,(int)n.getProperty("velocity",100)),juce::jmax(0.0,(double)n.getProperty("beat",0.0)),juce::jmax(.03,(double)n.getProperty("length",.5))});}std::sort(lane.notes.begin(),lane.notes.end(),[](const ArrangementNote&x,const ArrangementNote&y){return x.beat<y.beat;});a.lanes.push_back(std::move(lane));}
    return a;
}

} // namespace sonara