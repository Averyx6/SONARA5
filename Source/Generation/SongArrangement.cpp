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

std::vector<int> SongArrangement::getProgressionFingerprint() const
{
    std::vector<int> fp;
    const int n=juce::jlimit(1,8,harmonyPlan.progressionLength);
    fp.reserve((size_t)(2+n*3));
    fp.push_back(n);
    fp.push_back(harmonyPlan.cadenceStyle);
    for(int i=0;i<n;++i)
    {
        // Scale degrees make this invariant to key/transposition. Harmonic rhythm
        // is quantized to eighth-bars so an identical loop at another key cannot
        // masquerade as a new progression.
        fp.push_back(harmonyPlan.mainDegrees[(size_t)i]);
        fp.push_back(harmonyPlan.alternateDegrees[(size_t)i]);
        fp.push_back((int)std::llround(harmonyPlan.rhythmBars[(size_t)i]*8.0));
    }
    return fp;
}

std::vector<int> SongArrangement::getHarmonyFingerprint() const
{
    auto fp=getProgressionFingerprint();
    fp.reserve(fp.size()+harmonyEvents.size()*7+8);
    fp.push_back(harmonyPlan.rhythmMode);
    fp.push_back(harmonyPlan.pedalIntro?1:0);
    fp.push_back(harmonyPlan.pedalVerse?1:0);
    for(const auto& event:harmonyEvents)
    {
        const auto& section=sections[(size_t)juce::jlimit(0,(int)sections.size()-1,event.sectionIndex)];
        const double localBeat=event.beat-(double)section.startBar*beatsPerBar;
        fp.push_back(event.sectionIndex);
        fp.push_back((int)std::llround(localBeat*4.0));
        fp.push_back((int)std::llround(event.length*4.0));
        fp.push_back(event.scaleDegree);
        fp.push_back(event.inversion);
        fp.push_back(event.voicingStyle);
        fp.push_back(event.extension+(event.borrowed?8:0));
    }
    return fp;
}

std::vector<int> SongArrangement::getMelodyFingerprint() const
{
    std::vector<int> fp;
    const ArrangementLane* lead=nullptr;
    for(const auto& lane:lanes)if(lane.name=="LEAD"){lead=&lane;break;}
    if(lead==nullptr||lead->notes.empty())return fp;

    constexpr int stride=7;
    const int limit=juce::jmin(128,(int)lead->notes.size());
    fp.reserve((size_t)(limit*stride+1));
    const int tonic=((rootMidi%12)+12)%12;
    int previousScaleRelative=0;
    double previousBeat=lead->notes.front().beat;
    for(int i=0;i<limit;++i)
    {
        const auto& note=lead->notes[(size_t)i];
        const int scaleRelative=(((note.note-tonic)%12)+12)%12;
        int interval=i==0?0:scaleRelative-previousScaleRelative;
        // Compare melodic motion in a canonical key-relative register. Absolute
        // octave folding at MIDI safety boundaries must not make a transposed
        // copy look novel; pitch class, contour and rhythm remain identical.
        while(interval>6)interval-=12;
        while(interval<-6)interval+=12;
        fp.push_back(scaleRelative);
        fp.push_back(interval+18);
        fp.push_back(interval>0?2:(interval<0?0:1));
        fp.push_back(0);
        fp.push_back((int)std::llround(std::fmod(juce::jmax(0.0,note.beat),16.0)*4.0));
        fp.push_back(i==0?0:juce::jlimit(0,64,(int)std::llround((note.beat-previousBeat)*8.0)));
        fp.push_back(juce::jlimit(1,32,(int)std::llround(note.length*8.0)));
        previousScaleRelative=scaleRelative;
        previousBeat=note.beat;
    }
    fp.push_back((int)lead->notes.size());
    return fp;
}

std::vector<int> SongArrangement::getStructureFingerprint() const
{
    std::vector<int> fp;
    fp.reserve(sections.size()*3+1);
    fp.push_back((int)sections.size());
    for(const auto& section:sections)
    {
        fp.push_back(section.startBar);
        fp.push_back(section.bars);
        fp.push_back((int)std::llround(section.energy*100.f));
    }
    return fp;
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

    // v0.8 production intent: genre/production words shape the composition plan,
    // not only the final synth palette.
    const bool festival=p.contains("festival")||p.contains("mainstage")||p.contains("big room");
    const bool progressive=p.contains("progressive house")||p.contains("melodic house");
    if(festival)
    {
        plan.phraseBars=4;
        plan.motifLength=juce::jlimit(4,6,plan.motifLength);
        plan.density=juce::jlimit(.70f,.88f,juce::jmax(plan.density,.72f));
        plan.restAmount=juce::jmin(plan.restAmount,.16f);
        plan.syncopation=juce::jlimit(.28f,.62f,plan.syncopation);
        plan.chordMode=2+(plan.chordMode%2);
        plan.hatMode=juce::jmax(1,plan.hatMode);
        if(plan.bassMode==3)plan.bassMode=2; // avoid hyper-busy bass under anthem leads
    }
    if(progressive)
    {
        plan.development=juce::jlimit(.60f,.82f,juce::jmax(plan.development,.64f));
        plan.motifLength=juce::jlimit(4,7,plan.motifLength);
        plan.restAmount=juce::jmin(plan.restAmount,.20f);
    }
    if(p.contains("radio edit")||p.contains("radio friendly"))
    {
        plan.phraseBars=4;
        plan.development=juce::jlimit(.48f,.68f,plan.development);
    }

    // v1.5 explicit songwriter controls. These phrases are also exposed as UI
    // suggestion chips, so the prompt can function like production direction.
    if(p.contains("strong hook")||p.contains("clear hook")||p.contains("main melody"))
    {
        plan.phraseBars=4;plan.motifLength=juce::jlimit(4,5,plan.motifLength);
        plan.development=juce::jmin(plan.development,.44f);
        plan.density=juce::jlimit(.48f,.66f,plan.density);
        plan.restAmount=juce::jmax(plan.restAmount,.14f);
    }
    if(p.contains("simple melody")||p.contains("clean melody")||p.contains("no random notes"))
    {
        plan.phraseBars=4;plan.motifLength=juce::jlimit(3,5,plan.motifLength);
        plan.density=juce::jmin(plan.density,.56f);
        plan.syncopation=juce::jmin(plan.syncopation,.28f);
        plan.restAmount=juce::jmax(plan.restAmount,.18f);
        plan.development=juce::jmin(plan.development,.38f);
        plan.octaveRange=1;
        plan.melodyArchetype=(plan.melodyArchetype%2==0)?0:4;
    }
    if(p.contains("less busy")||p.contains("more space"))
    {
        plan.density=juce::jmin(plan.density,.54f);
        plan.restAmount=juce::jmax(plan.restAmount,.25f);
        plan.syncopation=juce::jmin(plan.syncopation,.34f);
    }
    if(p.contains("radio structure")||p.contains("song structure"))
        plan.structureStyle=0;
    if(p.contains("powerful drop")||p.contains("massive drop"))
    {
        plan.chordMode=juce::jmax(plan.chordMode,2);
        plan.hatMode=juce::jmax(plan.hatMode,1);
        plan.bassMode=juce::jmin(plan.bassMode,2);
    }
}

void SongArrangement::buildSections(uint64_t seed)
{
    struct Layout { int lengths[8]; };
    static constexpr Layout layouts[] = {
        // INTRO, VERSE, BUILD, CHORUS, DROP, BREAKDOWN, BUILD 2, FINAL HOOK
        {{8,8,8,8,16,8,8,16}},
        {{4,12,8,8,16,8,8,12}},
        {{8,8,4,8,16,12,8,12}},
        {{4,8,8,8,12,8,8,12}}
    };

    const int style = plan.structureStyle;
    std::array<int,8> lengths{};
    for(int i=0;i<8;++i)lengths[(size_t)i]=layouts[style].lengths[i];

    const auto p=sourcePrompt.toLowerCase();
    const bool festival=p.contains("festival")||p.contains("mainstage")||p.contains("big room");
    if(festival)
    {
        lengths[2]=juce::jmax(lengths[2],8);   // BUILD
        lengths[3]=juce::jmax(lengths[3],8);   // CHORUS
        lengths[4]=juce::jmax(lengths[4],16);  // DROP
        lengths[6]=juce::jmax(lengths[6],8);   // BUILD 2
        lengths[7]=juce::jmax(lengths[7],12);  // FINAL HOOK
    }
    if(p.contains("early drop"))
    {
        lengths[0]=juce::jmin(lengths[0],4);
        lengths[1]=juce::jmin(lengths[1],4);
        lengths[2]=juce::jmin(lengths[2],4);
    }
    if(p.contains("long build"))
    {
        lengths[2]=juce::jmax(lengths[2],12);
        lengths[6]=juce::jmax(lengths[6],8);
    }
    if(p.contains("radio edit")||p.contains("short song"))
    {
        lengths[0]=juce::jmin(lengths[0],4);
        lengths[1]=juce::jmin(lengths[1],8);
        lengths[3]=juce::jmin(lengths[3],8);
        lengths[4]=juce::jmin(lengths[4],12);
        lengths[5]=juce::jmin(lengths[5],8);
        lengths[7]=juce::jmin(lengths[7],12);
    }
    if(p.contains("cinematic"))
    {
        lengths[0]=juce::jmax(lengths[0],8);
        lengths[5]=juce::jmax(lengths[5],12);
    }
    if(p.contains("short intro")||p.contains("minimal intro"))lengths[0]=juce::jmin(lengths[0],4);
    if(p.contains("big chorus")||p.contains("long chorus"))lengths[3]=juce::jmax(lengths[3],12);
    if(p.contains("long drop")||p.contains("extended drop"))lengths[4]=juce::jmax(lengths[4],20);
    if(p.contains("short breakdown"))lengths[5]=juce::jmin(lengths[5],4);
    if(p.contains("long breakdown"))lengths[5]=juce::jmax(lengths[5],12);

    static constexpr const char* names[8] = {
        "INTRO","VERSE","BUILD","CHORUS","DROP","BREAKDOWN","BUILD 2","FINAL HOOK"
    };
    static constexpr float energy[8] = {
        .18f,.40f,.68f,.80f,.98f,.30f,.84f,1.00f
    };

    sections.clear();
    int start = 0;
    for (int i = 0; i < 8; ++i)
    {
        sections.push_back({ names[i], start, lengths[(size_t)i], energy[i] });
        start += lengths[(size_t)i];
    }
    bars = start;
}

void SongArrangement::generate(const juce::String& prompt, double bpm, uint64_t seed)
{
    generateComposition(prompt,bpm,seed);
    finalizeSoundPalette();
}

void SongArrangement::generateComposition(const juce::String& prompt, double bpm, uint64_t seed)
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
}

void SongArrangement::finalizeSoundPalette()
{
    if(lanes.empty())return;
    PromptGenerator designer;
    const auto productionPrompt=sourcePrompt.toLowerCase();
    const bool festival=productionPrompt.contains("festival")||productionPrompt.contains("mainstage")||productionPrompt.contains("big room");
    const bool progressive=productionPrompt.contains("progressive house")||productionPrompt.contains("melodic house");
    const bool tech=productionPrompt.contains("tech house")||productionPrompt.contains("minimal house");
    const bool dnb=productionPrompt.contains("drum and bass")||productionPrompt.contains("dnb");
    const bool trance=productionPrompt.contains("trance");
    const bool cinematic=productionPrompt.contains("cinematic")||productionPrompt.contains("film");
    const bool tropical=productionPrompt.contains("tropical");

    for (size_t i = 0; i < lanes.size(); ++i)
    {
        auto& lane = lanes[i];
        juce::String soundPrompt = sourcePrompt + " " + lane.name + " ";

        // Role descriptions intentionally use the semantic vocabulary understood by
        // PromptGenerator. The palette can now choose topology based on musical job,
        // not merely lane name + a random adjective.
        if(lane.name=="KICK")
            soundPrompt += festival?"festival mainstage punchy kick tight dry hard transient ":
                           (dnb?"tight fast punchy kick dry transient ":"tight punchy kick dry transient ");
        else if(lane.name=="SNARE / CLAP")
            soundPrompt += festival?"festival layered snare clap sharp wide transient ":
                           "snare clap crisp transient controlled ";
        else if(lane.name=="HATS")
            soundPrompt += "bright crisp metallic hats short dry airy ";
        else if(lane.name=="PERCUSSION")
            soundPrompt += festival?"festival impact crash tom transition percussion powerful ":
                           "clean percussion impact crash transition ";
        else if(lane.name=="BASS")
        {
            if(dnb)soundPrompt += "aggressive moving reese bass controlled mono low end ";
            else if(tech)soundPrompt += "clean mono bass short punchy dry lowpass ";
            else if(festival)soundPrompt += "festival punchy bass controlled mono harmonic body ";
            else soundPrompt += "deep controlled bass mono harmonic body ";
        }
        else if(lane.name=="SUB")
            soundPrompt += "pure clean sine sub mono lowpass controlled dry no highs ";
        else if(lane.name=="CHORDS")
        {
            if(festival||progressive)soundPrompt += "festival supersaw chords wide bright controlled anthem ";
            else if(tech)soundPrompt += "short house chord stab warm tight ";
            else if(trance)soundPrompt += "wide trance supersaw chords bright ";
            else soundPrompt += "wide musical harmony chords ";
        }
        else if(lane.name=="PLUCK")
        {
            if(festival||progressive)soundPrompt += "bright festival pluck rhythmic crisp emotional ";
            else if(tropical)soundPrompt += "organic woody marimba mallet pluck rhythmic ";
            else soundPrompt += "pluck rhythmic clean articulated ";
        }
        else if(lane.name=="PAD")
        {
            if(cinematic)soundPrompt += "emotional string ensemble choir pad wide evolving cinematic ";
            else if(progressive)soundPrompt += "emotional airy wide pad warm evolving ";
            else soundPrompt += "wide musical harmony pad soft evolving ";
        }
        else if(lane.name=="LEAD")
        {
            if(festival)soundPrompt += "huge mainstage festival supersaw lead memorable emotional anthem ";
            else if(progressive)soundPrompt += "emotional progressive supersaw lead memorable expressive ";
            else if(trance)soundPrompt += "bright trance supersaw lead wide energetic ";
            else soundPrompt += "memorable emotional lead expressive ";
        }
        else if(lane.name=="COUNTER")
        {
            if(cinematic)soundPrompt += "airy expressive flute counter melody thin complementary ";
            else if(tropical)soundPrompt += "organic mallet counter melody light ";
            else soundPrompt += "bright thin pluck counter melody complementary ";
        }
        else if(lane.name=="FX / TRANSITIONS")
            soundPrompt += festival?"festival riser uplifter impact transition airy noise ":
                           "riser impact transition atmospheric texture ";
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

        const auto semanticName=lane.sound.name.replace("Generated ","").trim();
        lane.sound.name=lane.name+" • "+(semanticName.isEmpty()?juce::String("Custom"):semanticName);
    }
}

void SongArrangement::addDrums(uint64_t seed, bool energetic)
{
    ArrangementLane kick{"KICK",10,true}, snare{"SNARE / CLAP",10,true}, hats{"HATS",10,true}, perc{"PERCUSSION",10,true};
    const auto p = sourcePrompt.toLowerCase();
    const bool house = p.contains("house") || p.contains("future rave") || p.contains("edm");
    const bool trap = p.contains("trap") || p.contains("hip hop");
    const bool dnb = p.contains("drum and bass") || p.contains("dnb");
    const bool festival = p.contains("festival") || p.contains("mainstage") || p.contains("big room");
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
        const bool preDropGap=chorus&&sectionEnd&&(house||festival);
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
                    if((!intro || q==0 || q==2 || bar-section->startBar>=section->bars-2)
                       && !(preDropGap&&q==3))
                        addNote(kick,36,b+q,.11,
                                drop?123:(chorus?116:(build?111:105+(q==0?5:0))));
                if(!preDropGap&&(drop||build||chorus) && ((bar+groove)%4==3))
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
                const int backbeat=drop?122:(chorus?116:(build?110:104));
                addNote(snare,38,b+1.0,.13,backbeat);
                addNote(snare,38,b+3.0,.13,juce::jmin(127,backbeat+2));
                if(drop||chorus)
                {
                    addNote(snare,39,b+1.018,.075,drop?108:101);
                    addNote(snare,39,b+3.018,.075,drop?111:103);
                }
                if((bar+groove)%4==2) addNote(snare,39,b+2.75,.07,68+(int)(random01(seed,1210+bs)*18.f));
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
            if(preDropGap&&beat>=b+3.5)continue;
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

        // BUILD -> CHORUS gets a restrained lift. The true festival-style drum
        // tension belongs at CHORUS -> DROP, where the listener expects the payoff.
        if(build && sectionEnd)
        {
            const int divisions=8;
            for(int s=0;s<divisions;++s)
            {
                const double rollBeat=b+2.0+s*(2.0/divisions);
                addNote(snare,38,rollBeat,.05,
                        juce::jlimit(54,104,58+s*6+(int)(random01(seed,1600+s+bar)*5.f)));
            }
        }

        if(preDropGap)
        {
            // Two-beat accelerating snare/clap build, followed by a half-beat of
            // air. The DROP itself already receives a crash/impact on beat one.
            constexpr int rollHits=8;
            for(int s=0;s<rollHits;++s)
            {
                const double pos=2.0+s*(1.5/rollHits);
                const int velocity=juce::jlimit(68,124,70+s*7);
                addNote(snare,(s%2==0)?38:39,b+pos,.045,velocity);
            }
            addNote(perc,45,b+2.75,.07,88);
            addNote(perc,47,b+3.10,.065,98);
            addNote(perc,50,b+3.35,.055,108);
        }

        if(sectionEnd&&!breakdown&&(build||chorus||drop)&&!preDropGap)
        {
            addNote(perc,45,b+3.00,.075,82+(drop?8:0));
            addNote(perc,47,b+3.50,.070,90+(drop?8:0));
            addNote(perc,50,b+3.75,.060,100+(drop?10:0));
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

    const auto productionPrompt=sourcePrompt.toLowerCase();
    const bool mainstreamSong=productionPrompt.contains("progressive house")
        ||productionPrompt.contains("melodic house")||productionPrompt.contains("edm")
        ||productionPrompt.contains("pop")||productionPrompt.contains("trance")
        ||productionPrompt.contains("festival")||productionPrompt.contains("mainstage")
        ||productionPrompt.contains("future rave");

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
        if(mainstreamSong)
        {
            if(drop||chorus||finalHook)repeats=juce::jmin(repeats,2);
            else if(build||verse)repeats=juce::jmin(repeats,1);
        }

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
        if(intro||verse||breakdown)
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
        const int supportTheme=(chorus||drop||finalHook)?3:sectionIndex;
        const int supportMotifBar=localBar%2;
        const uint64_t bassMotifSeed=mix64(domains.bass
            ^ ((uint64_t)supportTheme+1ULL)*0x9e3779b97f4a7c15ULL
            ^ ((uint64_t)supportMotifBar+1ULL)*0xbf58476d1ce4e5b9ULL);
        const uint64_t subMotifSeed=mix64(domains.sub
            ^ ((uint64_t)supportTheme+1ULL)*0x94d049bb133111ebULL
            ^ ((uint64_t)supportMotifBar+1ULL)*0x517cc1b727220a95ULL);
        const uint64_t pluckMotifSeed=mix64(domains.pluck
            ^ ((uint64_t)supportTheme+1ULL)*0xd1342543de82ef95ULL
            ^ ((uint64_t)supportMotifBar+1ULL)*0xa24baed4963ee407ULL);
        const bool preDropGap=chorus&&localBar==section->bars-1
            && (sourcePrompt.containsIgnoreCase("house")||sourcePrompt.containsIgnoreCase("edm")
                ||sourcePrompt.containsIgnoreCase("festival")||sourcePrompt.containsIgnoreCase("mainstage"));

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
            if(mainstreamSong)
            {
                if(drop||chorus||finalHook)count=juce::jmin(count,4);
                else if(verse||build)count=juce::jmin(count,3);
                if(breakdown)count=juce::jmin(count,1);
            }

            for(int i=0;i<count;++i)
            {
                const double pos=positions[i];
                if(preDropGap&&pos>=3.45)continue;
                if(i>0&&random01(bassMotifSeed,0x7100+i)<(verse?.20f:.08f))continue;
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;

                int degree=h->scaleDegree;
                int note=rootMidi+scaleSemitoneForDegree(degree)-12;

                const float move=random01(bassMotifSeed,0x7200+i);
                const float bassPassing=mainstreamSong?juce::jmin(.10f,harmonyPlan.passingProbability):harmonyPlan.passingProbability;
                if(i>0&&move<bassPassing)
                {
                    const int dir=random01(bassMotifSeed,0x7210+i)>.5f?1:-1;
                    note=rootMidi+scaleSemitoneForDegree(degree+dir)-12;
                }
                else if(move<.26f&&i%3==2)note+=7;
                else if(!mainstreamSong&&move>.84f&&(drop||finalHook))note+=12;

                note=foldBass(note);
                const double rawLen=bassMode==3?.28:(drop||chorus?.48:.72);
                const double len=preDropGap?juce::jmin(rawLen,juce::jmax(.08,3.45-pos)):rawLen;
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
                subCount=random01(subMotifSeed,0x7400)>.52f?2:3;
            else if(verse&&random01(subMotifSeed,0x7410)>.62f)
                subCount=2;
            if(breakdown)subCount=(localBar%2==0)?1:0;
            if(mainstreamSong)
            {
                subCount=juce::jmin(subCount,(drop||chorus||finalHook)?2:1);
                if(build&&localBar<section->bars/2)subCount=0;
            }

            for(int i=0;i<subCount;++i)
            {
                const double pos=positions[i];
                if(preDropGap&&pos>=3.45)continue;
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                int note=rootMidi+scaleSemitoneForDegree(h->scaleDegree)-24;
                while(note<24)note+=12;
                while(note>47)note-=12;
                const double rawLen=subCount==1?juce::jmin(3.30,4.0-pos):juce::jmin(1.45,4.0-pos);
                const double len=preDropGap?juce::jmin(rawLen,juce::jmax(.08,3.45-pos)):rawLen;
                addNote(sub,note,barBeat+pos,len,
                        juce::jlimit(52,96,60+(int)(energy*22.f)+(int)(random01(domains.sub,0x7420+bs+i)*8.f)));
            }
        }

        // PLUCK owns its rhythm; at each onset it asks Harmony DNA for the local
        // chord, then mixes chord tones with scale passing tones.
        if((energy>.5f||chorus)&&!breakdown)
        {
            // Supporting plucks must frame the hook, not machine-gun underneath it.
            int steps=(finalHook?6:(drop?4:(chorus?4:(build?4:3))));
            if(mainstreamSong)
                steps=(drop||chorus||finalHook)?3:(build?3:2);
            for(int i=0;i<steps;++i)
            {
                const uint64_t ps=(uint64_t)i;
                if(i>0&&random01(pluckMotifSeed,0x8100+ps)<((drop||chorus)?.16f:.24f))continue;

                double pos=i*(4.0/steps);
                if(i%2&&random01(pluckMotifSeed,0x8110+ps)<.45f)
                    pos+=((random01(pluckMotifSeed,0x8120+ps)-.5f)*.08f);
                if(preDropGap&&pos>=3.45)continue;

                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                const auto tones=chordTonesFor(*h);
                const int patternIndex=arpPatterns[arpMode][(i+supportMotifBar+supportTheme)%8];

                int note=tones[(size_t)(patternIndex%4)]+24;
                const float pluckPassing=mainstreamSong?juce::jmin(.08f,harmonyPlan.passingProbability):harmonyPlan.passingProbability;
                if(random01(pluckMotifSeed,0x8130+ps)<pluckPassing)
                {
                    const int dir=random01(pluckMotifSeed,0x8140+ps)>.5f?1:-1;
                    note=rootMidi+scaleSemitoneForDegree(h->scaleDegree+dir)+24;
                }
                if(!mainstreamSong&&finalHook&&i%4==3&&random01(pluckMotifSeed,0x8150+ps)>.50f)note+=12;

                addNote(pluck,note,barBeat+juce::jlimit(0.0,3.90,pos),
                        .11+random01(pluckMotifSeed,0x8160+ps)*.22,
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
    const bool mainstreamEdm=progressive||pop||trance||p.contains("festival")||p.contains("mainstage")
        ||p.contains("future rave")||(p.contains("edm")&&!tech&&!dnb);
    const bool requestCounter=p.contains("counter melody")||p.contains("secondary lead")
        ||p.contains("call and response")||p.contains("answer melody");
    const bool disableCounter=p.contains("no counter")||p.contains("no counter melody")
        ||p.contains("main melody only")||p.contains("single lead")
        ||(mainstreamEdm&&!requestCounter);

    int architecture=plan.melodyArchetype%12;
    if(mainstreamEdm)
    {
        static constexpr int songArchetypes[6]={0,2,3,4,8,9}; // call/response, anthem, sparse, lyric, fall, rise
        architecture=songArchetypes[plan.melodyArchetype%6];
    }
    else if(dnb)
    {
        static constexpr int dnbArchetypes[4]={0,5,7,10};
        architecture=dnbArchetypes[plan.melodyArchetype%4];
    }
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
        const bool preDropGap=chorus&&localBar==section->bars-1
            && (p.contains("house")||p.contains("edm")||p.contains("festival")||p.contains("mainstage"));

        // v1.3 section roles: the main melody is deliberately absent in places.
        // Silence is part of the arrangement, so the chorus/drop hook feels like
        // an arrival instead of one continuous intro loop.
        bool active=true;
        if(intro)active=localBar>=juce::jmax(0,section->bars-2);       // 2-bar teaser only
        else if(verse)active=(localBar%4!=2);                          // 3-bar phrase + one breathing bar
        else if(build)active=localBar>=juce::jmax(1,section->bars/2);  // melody enters only in second half
        else if(breakdown)active=(localBar%4==0||localBar%4==2);       // sparse replies
        if(!active)continue;

        const int phraseBars=(chorus||drop||finalHook)?4:juce::jmax(2,plan.phraseBars);
        const int barInPhrase=localBar%phraseBars;
        const int phraseIndex=localBar/phraseBars;

        // DROP and FINAL HOOK share a recognizable song identity, but the final hook
        // develops it. Other sections get genuinely separate theme seeds.
        // CHORUS introduces the song's primary hook. DROP and FINAL HOOK develop
        // that same identity instead of inventing another melody.
        const int themeGroup=(chorus||drop||finalHook)?3:sectionIndex;

        // One section/theme owns a stable motif. Earlier builds re-rolled the
        // starting degree and rhythm family every bar, creating technically
        // in-key but musically unrelated notes.
        const int phraseGeneration=(chorus||drop)?0:
            (finalHook?(localBar>=juce::jmax(4,section->bars-4)?1:0):
             ((plan.development>.80f)?phraseIndex/2:(plan.development>.58f?phraseIndex/3:0)));
        const uint64_t motifSeed=mix64(seed
            ^ ((uint64_t)themeGroup+1ULL)*0x9e3779b97f4a7c15ULL
            ^ ((uint64_t)architecture+1ULL)*0x94d049bb133111ebULL);
        const uint64_t phraseSeed=mix64(motifSeed
            ^ ((uint64_t)phraseGeneration+1ULL)*0xbf58476d1ce4e5b9ULL);

        const auto chord=chordInfoAtBeat(bar*beatsPerBar);
        const int registerBase=tech?12:((drop||chorus||finalHook)?24:12);
        const int startDegree=(plan.startingDegree
            +(int)(random01(motifSeed,0x2001)*7.f))%7;

        const int motifBar=barInPhrase%2;
        const bool developmentBar=barInPhrase>=juce::jmax(1,phraseBars/2);
        int family=(rhythmBase+themeGroup+(int)(random01(motifSeed,0x2002)*3.f))%10;
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
        if(plan.density>.80f&&!cinematic)count=juce::jmin(6,count+1);
        if(plan.density<.52f)count=juce::jmax(2,count-1);

        // v1.1 note-budget: the hook must be readable at a glance and singable.
        // No section is allowed to become an eight-note-per-bar event cloud.
        if(intro)count=juce::jlimit(1,2,count);
        else if(verse)count=juce::jlimit(2,3,count-1);
        else if(breakdown)count=juce::jlimit(1,2,count);
        else if(build)count=juce::jlimit(2,3,count);
        else if(chorus)count=juce::jlimit(3,4,count);
        else if(drop||finalHook)count=4;
        else count=juce::jmin(count,3);

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

        const uint64_t motifDecisionSeed=mix64(motifSeed
            ^ ((uint64_t)motifBar+1ULL)*0x517cc1b727220a95ULL);
        const size_t leadBeforeBar=lead.notes.size();
        int lastDegree=startDegree;
        int repeated=0;
        for(int i=0;i<count;++i)
        {
            // Base motif decisions repeat with the two-bar motif. Absolute bar
            // number is intentionally excluded so later repetitions sound like
            // the same hook, not a re-roll.
            const uint64_t salt=(uint64_t)motifBar*256ULL+(uint64_t)i;
            const bool strong=positions[i]<.08||std::abs(std::fmod(positions[i],1.0))<.08;

            float restChance=plan.restAmount;
            if(chorus)restChance=juce::jmax(.10f,restChance*.70f);
            if(drop||finalHook)restChance=juce::jmax(.08f,restChance*.58f);
            if(verse)restChance=juce::jmax(.16f,restChance*1.28f);
            if(breakdown)restChance=juce::jmax(restChance,.30f);
            if(tech)restChance=juce::jmax(restChance,.24f);
            if(architecture==3)restChance*=.55f; // already sparse by construction
            if(!strong&&random01(motifDecisionSeed,0x2100+salt)<restChance)continue;

            int degree=startDegree;
            int octaveExtra=0;

            switch(architecture)
            {
                case 0: // CALL / RESPONSE
                {
                    const int cell=i%4;
                    const int call[]={0,2,4,3};
                    degree=startDegree+call[cell];
                    if(motifBar==1)
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
                    degree=chordInfoAtBeat(bar*beatsPerBar+positions[i]).degreeIndex+chordDegrees[(i+motifBar)%6];
                    if(i%3==2&&random01(motifDecisionSeed,0x2200+salt)>.52f)octaveExtra=12;
                    break;
                }
                case 2: // OCTAVE ANTHEM
                {
                    const int cell=i%4;
                    const int anthem[]={0,0,4,2};
                    degree=startDegree+anthem[cell];
                    octaveExtra=(cell==1||((motifBar+i)%4==3))?12:0;
                    break;
                }
                case 3: // SPARSE SIGNATURE MOTIF
                {
                    const int sparse[]={0,3,1};
                    degree=startDegree+sparse[i%3];
                    if(motifBar==1&&i==1)degree+=2;
                    break;
                }
                case 4: // LONG LYRIC LINE
                {
                    const int direction=((phraseSeed>>8)&1ULL)?1:-1;
                    degree=startDegree+direction*(motifBar*2+i);
                    if(i==count-1&&barInPhrase==phraseBars-1)degree=chord.degreeIndex;
                    break;
                }
                case 5: // SYNCOPATED RIFF
                {
                    static constexpr int riff[8]={0,0,3,1,4,2,1,5};
                    degree=startDegree+riff[(i+motifBar)%8];
                    break;
                }
                case 6: // PEDAL TONE + MOVING ANSWER
                {
                    if(i%2==0)degree=startDegree;
                    else degree=startDegree+1+((motifBar+i*2)%5);
                    break;
                }
                case 7: // SEQUENCED CELL
                {
                    static constexpr int cell[3]={0,2,1};
                    const int transposition=motifBar==0?0:2;
                    degree=startDegree+cell[i%3]+transposition;
                    break;
                }
                case 8: // FALLING HOOK
                {
                    degree=startDegree+6-((i+motifBar*2)%7);
                    if(i==count-1)degree=chord.degreeIndex;
                    break;
                }
                case 9: // RISING LIFT + RESOLUTION
                {
                    const int span=i+motifBar*2;
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
                    degree=startDegree+leap[(i+motifBar)%8];
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
            const bool hookSection=chorus||drop||finalHook;
            const float chordToneChance=hookSection
                ?(phraseEnding?.96f:(barAnchor?.52f:(strong?.28f:.10f)))
                :(phraseEnding?.98f:(barAnchor?.88f:(strong?.62f:.30f)));
            if(random01(motifDecisionSeed,0x2300+salt)<chordToneChance)
            {
                const int toneChoice=(int)(random01(motifDecisionSeed,0x2310+salt)*3.f)%3;
                if(toneChoice==0)degree=localHarmony.degreeIndex;
                else if(toneChoice==1)degree=localHarmony.degreeIndex+2;
                else degree=localHarmony.degreeIndex+4;
            }
            else if(!strong&&random01(motifDecisionSeed,0x2320+salt)<.14f)
            {
                const int dir=random01(motifDecisionSeed,0x2330+salt)>.5f?1:-1;
                degree+=dir; // controlled scale passing/approach tone
            }

            // Keep free motion singable. Large jumps belong to the explicit
            // anthem/arpeggio/wide-leap archetypes rather than appearing randomly.
            if(i>0&&architecture!=1&&architecture!=2&&architecture!=11)
            {
                int delta=degree-lastDegree;
                while(delta>4){degree-=7;delta=degree-lastDegree;}
                while(delta<-4){degree+=7;delta=degree-lastDegree;}
                if(std::abs(delta)>3&&random01(motifDecisionSeed,0x2340+salt)<.78f)
                    degree=lastDegree+(delta>0?2:-2);
            }

            // Phrase development changes one recognisable note instead of
            // replacing the motif with a new random contour.
            if(developmentBar&&i==juce::jmax(0,count-2)
               &&architecture!=1&&architecture!=2
               &&random01(phraseSeed,0x2350+salt)<plan.development*.55f)
                degree+=random01(phraseSeed,0x2360+salt)>.5f?2:-1;

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
            if(finalHook&&plan.octaveRange>=2&&random01(motifDecisionSeed,0x2400+salt)>.74f)registerNow+=12;
            if(verse&&random01(motifDecisionSeed,0x2410+salt)<.16f)registerNow-=12;

            double pos=positions[i];
            if(!(chorus||drop||finalHook)&&!strong&&plan.syncopation>.45f
               &&random01(motifDecisionSeed,0x2500+salt)<plan.syncopation*.42f)
                pos+=random01(motifDecisionSeed,0x2510+salt)>.5f?.125:-.125;
            pos=juce::jlimit(0.0,3.90,pos);
            if(preDropGap&&pos>=3.5)continue;

            double length=.34;

            // Genre articulation outranks architecture. An architecture changes
            // phrase construction, but must not turn a tech-house riff into a
            // cinematic legato line or a cinematic theme into a staccato riff.
            if(tech)length=.10+random01(motifDecisionSeed,0x2630+salt)*.13;
            else if(dnb)length=.08+random01(motifDecisionSeed,0x2631+salt)*.14;
            else if(cinematic)length=.62+random01(motifDecisionSeed,0x2632+salt)*.78;
            else if(architecture==4)length=.58+random01(motifDecisionSeed,0x2600+salt)*.62;
            else if(architecture==3)length=.28+random01(motifDecisionSeed,0x2610+salt)*.38;
            else if(architecture==2)length=.22+random01(motifDecisionSeed,0x2620+salt)*.28;
            else if(architecture==5||architecture==10)length=.12+random01(motifDecisionSeed,0x2633+salt)*.18;
            else if(chorus||drop||finalHook)length=strong?.38:.28;
            else length=.20+random01(motifDecisionSeed,0x2640+salt)*.42;

            const float phraseAccent=random01(phraseSeed,0x2700+salt);
            const int velocity=juce::jlimit(52,124,
                60+(int)(energy*38.f)+(strong?7:0)+(int)(phraseAccent*10.f)-5);

            addNote(lead,scaleNote(degree,registerNow),bar*beatsPerBar+pos,length,velocity);
            lastDegree=degree;
        }

        // Build sections climb toward the next section with a musically clear pickup.
        if(build&&barInPhrase>=phraseBars/2)
        {
            const int pickupDegree=juce::jlimit(0,13,startDegree+barInPhrase+2);
            const double pickupPos=preDropGap?3.25:3.5;
            addNote(lead,scaleNote(pickupDegree,registerBase),
                    bar*beatsPerBar+pickupPos,.16,juce::jlimit(72,122,80+(int)(energy*34.f)));
        }

        // Counter melody behaves like an answering musician, not another lead
        // playing continuously. It enters primarily on motif answer bars and only
        // when the main lead has enough space.
        const int leadNotesThisBar=(int)(lead.notes.size()-leadBeforeBar);
        const bool answerBar=motifBar==1;
        const bool counterSpace=leadNotesThisBar<=(finalHook?6:5);
        if(!disableCounter&&(drop||finalHook)&&!tech&&answerBar&&counterSpace)
        {
            const uint64_t cs=mix64(domains.counter
                ^ ((uint64_t)themeGroup+1ULL)*0x9e3779b97f4a7c15ULL
                ^ ((uint64_t)motifBar+1ULL)*0xbf58476d1ce4e5b9ULL);
            const int counterCount=finalHook?3:2;
            int cd=(startDegree+3+(int)(random01(cs,1)*4.f))%7;
            for(int i=0;i<counterCount;++i)
            {
                if(i>0&&random01(cs,20+i)<.18f)continue;
                const double pos=1.75+i*(2.0/counterCount)
                    +(i%2?.08*(random01(cs,40+i)-.5):0.0);
                const int move=((int)(random01(cs,60+i)*5.f)-2);
                cd=juce::jlimit(0,6,cd+move);
                const int reg=finalHook?24:12;
                addNote(counter,scaleNote(cd,reg),bar*beatsPerBar+juce::jlimit(0.0,3.85,pos),
                        .14+.20*random01(cs,90+i),
                        juce::jlimit(48,100,56+(int)(energy*25.f)+(int)(random01(phraseSeed,110+i+bar)*8.f)));
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

    if(mainstreamEdm)
    {
        auto sectionForBeat=[&](double beat)->const ArrangementSection*
        {
            const int bar=juce::jlimit(0,bars-1,(int)std::floor(beat/beatsPerBar));
            for(const auto& s:sections)if(sectionContains(s,bar))return &s;
            return nullptr;
        };

        auto nearestScalePitch=[&](int target,int low,int high)
        {
            int best=juce::jlimit(low,high,target),bestDistance=999;
            for(int note=low;note<=high;++note)
            {
                const int pc=((note-rootMidi)%12+12)%12;
                bool legal=false;
                for(int si=0;si<7;++si)if(scale[si]==pc){legal=true;break;}
                if(!legal)continue;
                const int distance=std::abs(note-target);
                if(distance<bestDistance){bestDistance=distance;best=note;}
            }
            return best;
        };

        // Section-aware register lanes stop isolated notes from randomly jumping
        // an octave up/down while preserving scale and melodic direction.
        const ArrangementSection* previousSection=nullptr;
        previousNote=-1;
        for(auto& n:lead.notes)
        {
            const auto* s=sectionForBeat(n.beat);
            const auto name=s?s->name:juce::String();
            const bool hook=name.contains("CHORUS")||name.contains("DROP")||name.contains("FINAL");
            const bool lowEnergy=name.contains("INTRO")||name.contains("BREAKDOWN");
            const int low=hook?62:(lowEnergy?55:58);
            const int high=hook?86:(lowEnergy?76:82);
            while(n.note<low&&n.note+12<=high)n.note+=12;
            while(n.note>high&&n.note-12>=low)n.note-=12;

            if(previousNote>=0&&s==previousSection)
            {
                while(n.note-previousNote>7&&n.note-12>=low)n.note-=12;
                while(previousNote-n.note>7&&n.note+12<=high)n.note+=12;
            }
            previousNote=n.note;
            previousSection=s;
        }

        // Repair one-off pitch spikes that sit far away from both neighbours.
        for(size_t i=1;i+1<lead.notes.size();++i)
        {
            auto& current=lead.notes[i];
            const auto& prev=lead.notes[i-1];
            const auto& next=lead.notes[i+1];
            const auto* section=sectionForBeat(current.beat);
            if(section!=sectionForBeat(prev.beat)||section!=sectionForBeat(next.beat))continue;
            if(std::abs(current.note-prev.note)>7&&std::abs(current.note-next.note)>7
               &&std::abs(prev.note-next.note)<=5&&current.length<=.55)
            {
                const bool hook=section&&(section->name.contains("CHORUS")||section->name.contains("DROP")||section->name.contains("FINAL"));
                const int target=(prev.note+next.note)/2;
                current.note=nearestScalePitch(target,hook?62:56,hook?86:82);
            }
        }

        // Remove tiny grace-note accidents that crowd an otherwise clear phrase.
        std::vector<ArrangementNote> cleaned;
        cleaned.reserve(lead.notes.size());
        for(const auto& n:lead.notes)
        {
            if(!cleaned.empty())
            {
                const auto& prev=cleaned.back();
                const auto* s=sectionForBeat(n.beat);
                if(s==sectionForBeat(prev.beat)&&n.beat-prev.beat<.18
                   &&n.length<.24&&std::abs(n.note-prev.note)>4)
                    continue;
            }
            cleaned.push_back(n);
        }
        lead.notes=std::move(cleaned);

        // Song hook memory: write one clean four-bar chorus phrase, then reuse that
        // exact musical idea in DROP and FINAL HOOK. Production changes create the
        // lift; random new lead notes do not.
        const ArrangementSection *chorusSection=nullptr,*dropSection=nullptr,*finalSection=nullptr;
        for(const auto& s:sections)
        {
            if(s.name=="CHORUS")chorusSection=&s;
            else if(s.name=="DROP")dropSection=&s;
            else if(s.name=="FINAL HOOK")finalSection=&s;
        }

        if(chorusSection&&dropSection&&finalSection&&chorusSection->bars>=4)
        {
            const double chorusStart=chorusSection->startBar*beatsPerBar;
            const double templateEnd=chorusStart+16.0;
            std::vector<ArrangementNote> hookTemplate;
            for(const auto& n:lead.notes)
                if(n.beat>=chorusStart&&n.beat<templateEnd)
                {
                    auto copy=n;
                    copy.beat-=chorusStart;
                    hookTemplate.push_back(copy);
                }

            if(hookTemplate.size()>=6&&hookTemplate.size()<=20)
            {
                std::vector<ArrangementNote> rebuilt;
                rebuilt.reserve(lead.notes.size()+32);
                auto insideHookSection=[&](double beat)
                {
                    const auto* s=sectionForBeat(beat);
                    return s&&(s==chorusSection||s==dropSection||s==finalSection);
                };
                for(const auto& n:lead.notes)if(!insideHookSection(n.beat))rebuilt.push_back(n);

                auto writeHook=[&](const ArrangementSection& section,int velocityLift,bool preservePreDropGap)
                {
                    const double start=section.startBar*beatsPerBar;
                    const double end=(section.startBar+section.bars)*beatsPerBar;
                    for(int block=0;block<section.bars;block+=4)
                    {
                        for(const auto& t:hookTemplate)
                        {
                            const double beat=start+block*beatsPerBar+t.beat;
                            if(beat>=end)continue;
                            const bool finalChorusBlock=preservePreDropGap&&block+4>=section.bars;
                            if(finalChorusBlock&&t.beat>=15.5)continue;
                            auto n=t;
                            n.beat=beat;
                            n.length=juce::jmin(n.length,juce::jmax(.08,end-beat-.02));
                            n.velocity=juce::jlimit(48,124,n.velocity+velocityLift);
                            rebuilt.push_back(n);
                        }
                    }
                };

                writeHook(*chorusSection,0,true);
                writeHook(*dropSection,7,false);
                writeHook(*finalSection,10,false);
                std::sort(rebuilt.begin(),rebuilt.end(),
                          [](const ArrangementNote& a,const ArrangementNote& b){return a.beat<b.beat;});
                lead.notes=std::move(rebuilt);
            }
        }
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
        const float previousEnergy=i>0?sections[i-1].energy:s.energy;
        const float energyDelta=s.energy-previousEnergy;
        const bool lift=i>0&&energyDelta>.12f;
        const bool release=i>0&&energyDelta<-.12f;
        const bool majorArrival=s.name.contains("DROP")||s.name.contains("FINAL")||s.name.contains("CHORUS");

        if(s.startBar>0&&lift)
        {
            const double leadIn=s.name.contains("DROP")||s.name.contains("FINAL")?
                (style==0?4.0:style==1?8.0:style==2?2.0:6.0):
                (style==0?2.0:style==1?4.0:1.0);
            const int riserNote=81+(int)(random01(seed,810+i)*9.f);
            addNote(fx,riserNote,juce::jmax(0.0,boundary-leadIn),juce::jmax(.35,leadIn-.08),
                    juce::jlimit(58,122,66+(int)(s.energy*38.f)+(int)(random01(seed,820+i)*8.f)));

            // Short pre-impact suck/air cue in the final half-beat.
            if(majorArrival)
                addNote(fx,92+(int)(random01(seed,825+i)*5.f),juce::jmax(0.0,boundary-.5),.38,
                        juce::jlimit(60,112,72+(int)(s.energy*26.f)));
        }
        else if(s.startBar>0&&release)
        {
            // Downlifters only happen when the arrangement actually releases energy.
            addNote(fx,55+(int)(random01(seed,830+i)*8.f),boundary,.75+random01(seed,831+i)*1.2,
                    64+(int)(previousEnergy*25.f));
        }
        else if(s.startBar>0&&random01(seed,832+i)>.72f)
        {
            // Neutral section change gets a subtle marker, not a full riser every time.
            addNote(fx,72+(int)(random01(seed,840+i)*8.f),boundary+.125,.30+random01(seed,850+i)*.45,
                    58+(int)(s.energy*20.f));
        }

        if(majorArrival)
        {
            addNote(fx,48+(style%2)*12,boundary,.24+random01(seed,860+i)*.34,
                    juce::jlimit(78,127,94+(int)(s.energy*28.f)));
            if(s.name.contains("FINAL")||s.name.contains("DROP"))
                addNote(fx,36,boundary+.5,.48+random01(seed,870+i)*.20,
                        juce::jlimit(90,124,103+(int)(s.energy*18.f)));
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
    root.setProperty("schema",2,nullptr);root.setProperty("prompt",sourcePrompt,nullptr);root.setProperty("bpm",tempo,nullptr);root.setProperty("bars",bars,nullptr);root.setProperty("rootMidi",rootMidi,nullptr);root.setProperty("minor",minor,nullptr);
    root.setProperty("songId",juce::String::toHexString((juce::int64)masterSeed),nullptr);
    root.setProperty("harmonyId",juce::String::toHexString((juce::int64)harmonyId),nullptr);
    root.setProperty("melodyId",juce::String::toHexString((juce::int64)melodyId),nullptr);

    juce::ValueTree songPlan("COMPOSITION_DNA");
    songPlan.setProperty("structureStyle",plan.structureStyle,nullptr);songPlan.setProperty("drumGroove",plan.drumGroove,nullptr);songPlan.setProperty("hatMode",plan.hatMode,nullptr);
    songPlan.setProperty("bassMode",plan.bassMode,nullptr);songPlan.setProperty("chordMode",plan.chordMode,nullptr);songPlan.setProperty("arpMode",plan.arpMode,nullptr);
    songPlan.setProperty("melodyArchetype",plan.melodyArchetype,nullptr);songPlan.setProperty("rhythmFamily",plan.rhythmFamily,nullptr);songPlan.setProperty("startingDegree",plan.startingDegree,nullptr);
    songPlan.setProperty("cadenceStyle",plan.cadenceStyle,nullptr);songPlan.setProperty("motifLength",plan.motifLength,nullptr);songPlan.setProperty("phraseBars",plan.phraseBars,nullptr);songPlan.setProperty("octaveRange",plan.octaveRange,nullptr);
    songPlan.setProperty("density",plan.density,nullptr);songPlan.setProperty("syncopation",plan.syncopation,nullptr);songPlan.setProperty("restAmount",plan.restAmount,nullptr);songPlan.setProperty("development",plan.development,nullptr);
    root.addChild(songPlan,-1,nullptr);

    juce::ValueTree harmony("HARMONY_DNA");
    harmony.setProperty("progressionLength",harmonyPlan.progressionLength,nullptr);harmony.setProperty("cadenceStyle",harmonyPlan.cadenceStyle,nullptr);harmony.setProperty("rhythmMode",harmonyPlan.rhythmMode,nullptr);harmony.setProperty("registerBase",harmonyPlan.registerBase,nullptr);
    harmony.setProperty("tension",harmonyPlan.tension,nullptr);harmony.setProperty("borrowedProbability",harmonyPlan.borrowedProbability,nullptr);harmony.setProperty("passingProbability",harmonyPlan.passingProbability,nullptr);harmony.setProperty("suspensionProbability",harmonyPlan.suspensionProbability,nullptr);harmony.setProperty("extensionProbability",harmonyPlan.extensionProbability,nullptr);
    harmony.setProperty("pedalIntro",harmonyPlan.pedalIntro,nullptr);harmony.setProperty("pedalVerse",harmonyPlan.pedalVerse,nullptr);
    for(int i=0;i<8;++i)
    {
        harmony.setProperty("main"+juce::String(i),harmonyPlan.mainDegrees[(size_t)i],nullptr);
        harmony.setProperty("alternate"+juce::String(i),harmonyPlan.alternateDegrees[(size_t)i],nullptr);
        harmony.setProperty("rhythm"+juce::String(i),harmonyPlan.rhythmBars[(size_t)i],nullptr);
        harmony.setProperty("inversion"+juce::String(i),harmonyPlan.inversions[(size_t)i],nullptr);
        harmony.setProperty("voicing"+juce::String(i),harmonyPlan.voicingStyles[(size_t)i],nullptr);
    }
    for(const auto& event:harmonyEvents)
    {
        juce::ValueTree value("HARMONY_EVENT");
        value.setProperty("beat",event.beat,nullptr);value.setProperty("length",event.length,nullptr);value.setProperty("degree",event.scaleDegree,nullptr);
        value.setProperty("inversion",event.inversion,nullptr);value.setProperty("voicing",event.voicingStyle,nullptr);value.setProperty("extension",event.extension,nullptr);
        value.setProperty("section",event.sectionIndex,nullptr);value.setProperty("borrowed",event.borrowed,nullptr);
        harmony.addChild(value,-1,nullptr);
    }
    root.addChild(harmony,-1,nullptr);
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
    a.masterSeed=(uint64_t)root.getProperty("songId","0").toString().getHexValue64();
    a.harmonyId=(uint64_t)root.getProperty("harmonyId","0").toString().getHexValue64();
    a.melodyId=(uint64_t)root.getProperty("melodyId","0").toString().getHexValue64();
    auto composition=root.getChildWithName("COMPOSITION_DNA");
    if(composition.isValid())
    {
        a.plan.structureStyle=(int)composition.getProperty("structureStyle",0);a.plan.drumGroove=(int)composition.getProperty("drumGroove",0);a.plan.hatMode=(int)composition.getProperty("hatMode",0);
        a.plan.bassMode=(int)composition.getProperty("bassMode",0);a.plan.chordMode=(int)composition.getProperty("chordMode",0);a.plan.arpMode=(int)composition.getProperty("arpMode",0);
        a.plan.melodyArchetype=(int)composition.getProperty("melodyArchetype",0);a.plan.rhythmFamily=(int)composition.getProperty("rhythmFamily",0);a.plan.startingDegree=(int)composition.getProperty("startingDegree",0);
        a.plan.cadenceStyle=(int)composition.getProperty("cadenceStyle",0);a.plan.motifLength=(int)composition.getProperty("motifLength",8);a.plan.phraseBars=(int)composition.getProperty("phraseBars",4);a.plan.octaveRange=(int)composition.getProperty("octaveRange",2);
        a.plan.density=(float)composition.getProperty("density",.65f);a.plan.syncopation=(float)composition.getProperty("syncopation",.35f);a.plan.restAmount=(float)composition.getProperty("restAmount",.18f);a.plan.development=(float)composition.getProperty("development",.55f);
    }
    auto harmony=root.getChildWithName("HARMONY_DNA");
    if(harmony.isValid())
    {
        a.harmonyPlan.progressionLength=juce::jlimit(1,8,(int)harmony.getProperty("progressionLength",4));a.harmonyPlan.cadenceStyle=(int)harmony.getProperty("cadenceStyle",0);a.harmonyPlan.rhythmMode=(int)harmony.getProperty("rhythmMode",0);a.harmonyPlan.registerBase=(int)harmony.getProperty("registerBase",60);
        a.harmonyPlan.tension=(float)harmony.getProperty("tension",.35f);a.harmonyPlan.borrowedProbability=(float)harmony.getProperty("borrowedProbability",.06f);a.harmonyPlan.passingProbability=(float)harmony.getProperty("passingProbability",.10f);a.harmonyPlan.suspensionProbability=(float)harmony.getProperty("suspensionProbability",.12f);a.harmonyPlan.extensionProbability=(float)harmony.getProperty("extensionProbability",.18f);
        a.harmonyPlan.pedalIntro=(bool)harmony.getProperty("pedalIntro",false);a.harmonyPlan.pedalVerse=(bool)harmony.getProperty("pedalVerse",false);
        for(int i=0;i<8;++i)
        {
            a.harmonyPlan.mainDegrees[(size_t)i]=(int)harmony.getProperty("main"+juce::String(i),a.harmonyPlan.mainDegrees[(size_t)i]);
            a.harmonyPlan.alternateDegrees[(size_t)i]=(int)harmony.getProperty("alternate"+juce::String(i),a.harmonyPlan.alternateDegrees[(size_t)i]);
            a.harmonyPlan.rhythmBars[(size_t)i]=(double)harmony.getProperty("rhythm"+juce::String(i),a.harmonyPlan.rhythmBars[(size_t)i]);
            a.harmonyPlan.inversions[(size_t)i]=(int)harmony.getProperty("inversion"+juce::String(i),a.harmonyPlan.inversions[(size_t)i]);
            a.harmonyPlan.voicingStyles[(size_t)i]=(int)harmony.getProperty("voicing"+juce::String(i),a.harmonyPlan.voicingStyles[(size_t)i]);
        }
        for(int i=0;i<harmony.getNumChildren();++i)
        {
            const auto value=harmony.getChild(i);if(value.getType().toString()!="HARMONY_EVENT")continue;
            HarmonyEvent event;event.beat=(double)value.getProperty("beat",0.0);event.length=(double)value.getProperty("length",4.0);event.scaleDegree=(int)value.getProperty("degree",0);event.inversion=(int)value.getProperty("inversion",0);event.voicingStyle=(int)value.getProperty("voicing",0);event.extension=(int)value.getProperty("extension",0);event.sectionIndex=(int)value.getProperty("section",0);event.borrowed=(bool)value.getProperty("borrowed",false);a.harmonyEvents.push_back(event);
        }
    }
    auto st=root.getChildWithName("SECTIONS");for(int i=0;i<st.getNumChildren();++i){auto v=st.getChild(i);a.sections.push_back({v.getProperty("name","").toString(),(int)v.getProperty("startBar",0),(int)v.getProperty("bars",8),(float)v.getProperty("energy",.5)});}if(a.sections.empty())a.buildSections(0x7a1f2d4bULL);
    auto lt=root.getChildWithName("LANES");for(int i=0;i<lt.getNumChildren();++i){auto l=lt.getChild(i);ArrangementLane lane;lane.name=l.getProperty("name","Lane").toString();lane.midiChannel=juce::jlimit(1,16,(int)l.getProperty("channel",1));lane.drums=(bool)l.getProperty("drums",false);auto dna=l.getChildWithName("SoundDNA");if(dna.isValid())lane.sound=SoundDNA::fromValueTree(dna);auto notes=l.getChildWithName("NOTES");for(int j=0;j<notes.getNumChildren();++j){auto n=notes.getChild(j);lane.notes.push_back({juce::jlimit(0,127,(int)n.getProperty("note",60)),juce::jlimit(1,127,(int)n.getProperty("velocity",100)),juce::jmax(0.0,(double)n.getProperty("beat",0.0)),juce::jmax(.03,(double)n.getProperty("length",.5))});}std::sort(lane.notes.begin(),lane.notes.end(),[](const ArrangementNote&x,const ArrangementNote&y){return x.beat<y.beat;});a.lanes.push_back(std::move(lane));}
    return a;
}

} // namespace sonara
