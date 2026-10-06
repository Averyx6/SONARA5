#include "SongArrangement.h"
#include "PromptGenerator.h"
#include "ProducerPrompt.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <initializer_list>

namespace sonara {
namespace {
void canonicaliseLane(ArrangementLane& lane,double total)
{
    std::stable_sort(lane.notes.begin(),lane.notes.end(),[](const ArrangementNote& a,const ArrangementNote& b){return a.beat<b.beat;});
    std::array<int,128> previous{};previous.fill(-1);
    std::vector<ArrangementNote> notes;notes.reserve(lane.notes.size());
    for(auto n:lane.notes)
    {
        if(!std::isfinite(n.beat)||!std::isfinite(n.length)||n.beat<0||n.beat>=total||n.length<=0||n.note<0||n.note>127)continue;
        n.length=std::min(n.length,total-n.beat);
        const int index=previous[(size_t)n.note];
        if(index>=0)
        {
            auto& last=notes[(size_t)index];
            if(std::abs(last.beat-n.beat)<.00001){last.velocity=std::max(last.velocity,n.velocity);last.length=std::max(last.length,n.length);continue;}
            if(last.beat+last.length>n.beat)last.length=n.beat-last.beat;
        }
        previous[(size_t)n.note]=(int)notes.size();notes.push_back(n);
    }
    lane.notes=std::move(notes);
}
}
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


SongArrangement::PromptIntent SongArrangement::parsePromptIntent(const juce::String& raw, double fallbackBpm)
{
    PromptIntent result;
    result.tempo=juce::jlimit(60.0,200.0,fallbackBpm);

    const auto prepared=ProducerPrompt::parse(raw);
    auto normalized=prepared.positive;
    result.exclusionMask=prepared.exclusions;
    juce::StringArray tokens;
    tokens.addTokens(normalized," ,;:/\t\r\n()[]{}","\"'");
    tokens.trim();tokens.removeEmptyStrings();

    auto phrase=[&](const juce::String& value){return normalized.contains(value);};
    auto positive=[&](const juce::String& value)
    {
        if(!normalized.contains(value))return false;
        return !normalized.contains("not "+value)
            &&!normalized.contains("no "+value)
            &&!normalized.contains("without "+value)
            &&!normalized.contains("avoid "+value);
    };

    for(int i=0;i<tokens.size();++i)
    {
        const auto token=tokens[i].trim();
        double value=0.0;
        bool found=false;
        if(token.endsWithIgnoreCase("bpm")&&token.length()>3)
        {
            value=token.dropLastCharacters(3).getDoubleValue();found=true;
        }
        else if(token.equalsIgnoreCase("bpm")&&i+1<tokens.size())
        {
            value=tokens[i+1].getDoubleValue();found=true;
        }
        else if(token.equalsIgnoreCase("tempo")&&i+1<tokens.size())
        {
            value=tokens[i+1].getDoubleValue();found=true;
        }
        else if(i+1<tokens.size()&&tokens[i+1].equalsIgnoreCase("bpm"))
        {
            value=token.getDoubleValue();found=true;
        }
        if(found&&value>=60.0&&value<=200.0)
        {
            result.tempo=value;result.hasExplicitTempo=true;break;
        }
    }

    auto pitchClass=[](juce::String token)
    {
        token=token.trim().toLowerCase();
        if(token.length()<1||token.length()>2)return -1;
        const juce::juce_wchar letter=token[0];
        int pc=letter=='c'?0:(letter=='d'?2:(letter=='e'?4:(letter=='f'?5:
               (letter=='g'?7:(letter=='a'?9:(letter=='b'?11:-1))))));
        if(pc<0)return -1;
        if(token.length()==2)
        {
            if(token[1]=='#')pc=(pc+1)%12;
            else if(token[1]=='b')pc=(pc+11)%12;
            else return -1;
        }
        return pc;
    };

    for(int i=0;i<tokens.size();++i)
    {
        auto noteToken=tokens[i].trim().toLowerCase();
        int mode=-1; // 0 major, 1 minor
        if(noteToken.endsWith("maj")&&noteToken.length()>3)
        {
            noteToken=noteToken.dropLastCharacters(3);mode=0;
        }
        else if(noteToken.endsWith("min")&&noteToken.length()>3)
        {
            noteToken=noteToken.dropLastCharacters(3);mode=1;
        }
        else if(noteToken.endsWithChar('m')&&noteToken.length()>1)
        {
            const auto candidate=noteToken.dropLastCharacters(1);
            if(pitchClass(candidate)>=0){noteToken=candidate;mode=1;}
        }
        if(i+1<tokens.size())
        {
            const auto next=tokens[i+1].toLowerCase();
            if(next=="major"||next=="maj")mode=0;
            else if(next=="minor"||next=="min")mode=1;
        }
        const int pc=pitchClass(noteToken);
        if(pc>=0&&mode>=0)
        {
            result.rootMidi=48+pc;result.minor=mode==1;result.hasExplicitKey=true;break;
        }
    }

    const bool festival=positive("festival")||positive("mainstage")||positive("big room")||positive("future rave");
    const bool progressive=positive("progressive house")||positive("melodic house");
    const bool tech=positive("tech house")||positive("minimal house");
    const bool trance=positive("trance");
    const bool dnb=positive("drum and bass")||positive("dnb");
    const bool cinematic=positive("cinematic")||positive("film score")||positive("soundtrack");
    const bool pop=positive("pop")||positive("radio");
    result.genreFamily=progressive?1:(festival?2:(tech?3:(trance?4:(dnb?5:(cinematic?6:(pop?7:0))))));

    if(positive("aggressive")||positive("hard"))result.emotionProfile=5;
    else if(positive("dreamy")||positive("ethereal"))result.emotionProfile=4;
    else if(positive("dark")||positive("moody"))result.emotionProfile=3;
    else if(positive("uplifting")||positive("hopeful")||positive("happy"))result.emotionProfile=2;
    else if(positive("emotional")||positive("warm")||positive("melancholic"))result.emotionProfile=1;

    const bool sparse=positive("sparse")||positive("minimal")||positive("less busy")
        ||positive("more space")||phrase("not too busy");
    const bool dense=positive("dense")||positive("complex")||positive("busy melody")
        ||positive("many layers")||positive("full arrangement");
    result.densityDirection=sparse&&!dense?-1:(dense&&!sparse?1:0);

    const bool restrained=positive("low energy")||positive("calm")||positive("gentle")
        ||positive("chill")||positive("restrained");
    const bool energetic=positive("high energy")||positive("energetic")||positive("powerful")
        ||positive("aggressive")||positive("huge")||positive("massive")
        ||festival;
    result.energyDirection=restrained&&!energetic?-1:(energetic?1:0);

    if(phrase("radio edit")||phrase("radio structure")||phrase("short song"))result.structureStyle=0;
    else if(cinematic)result.structureStyle=3;
    else if(progressive||trance)result.structureStyle=2;
    else if(festival)result.structureStyle=1;
    else if(pop||positive("house"))result.structureStyle=0;

    for(int i=0;i+1<tokens.size();++i)
        if(tokens[i+1].startsWithIgnoreCase("bar"))
        {
            const int value=tokens[i].getIntValue();
            if(value>=48&&value<=512){result.targetBars=4*((value+2)/4);break;}
        }
    if(result.targetBars==0&&prepared.durationSeconds>0.0)
        result.targetBars=juce::jlimit(48,512,4*(int)std::llround(prepared.durationSeconds*result.tempo/960.0));

    if(positive("rising motif")||positive("rising hook")||positive("rising melody"))result.hookShape=1;
    else if(positive("falling motif")||positive("falling hook")||positive("falling melody"))result.hookShape=2;
    else if(positive("arch melody")||positive("arched motif"))result.hookShape=3;
    else if(positive("call and response")||positive("call / response"))result.hookShape=0;

    constexpr unsigned noCounter=1u,noPad=2u,noArp=4u,noFx=8u,noHats=16u;
    if(phrase("no counter")||phrase("without counter")||phrase("main melody only")||phrase("single lead"))result.exclusionMask|=noCounter;
    if(phrase("no pad")||phrase("without pad"))result.exclusionMask|=noPad;
    if(phrase("no arp")||phrase("no arpeggio")||phrase("without arp")||phrase("without arpeggio"))result.exclusionMask|=noArp;
    if(phrase("no fx")||phrase("without fx")||phrase("no transition fx"))result.exclusionMask|=noFx;
    if(phrase("no hats")||phrase("without hats"))result.exclusionMask|=noHats;

    constexpr unsigned earlyDrop=1u,longBuild=2u,shortIntro=4u,longDrop=8u;
    constexpr unsigned shortBreak=16u,longBreak=32u,bigChorus=64u;
    constexpr unsigned strongDrop=128u,softDrop=256u,quietBreak=512u,intenseBreak=1024u;
    if(phrase("early drop")||phrase("drop early"))result.sectionDirections|=earlyDrop;
    if(phrase("long build")||phrase("extended build"))result.sectionDirections|=longBuild;
    if(phrase("short intro")||phrase("minimal intro"))result.sectionDirections|=shortIntro;
    if(phrase("long drop")||phrase("extended drop"))result.sectionDirections|=longDrop;
    if(phrase("short breakdown"))result.sectionDirections|=shortBreak;
    if(phrase("long breakdown")||phrase("extended breakdown"))result.sectionDirections|=longBreak;
    if(phrase("big chorus")||phrase("long chorus"))result.sectionDirections|=bigChorus;
    if(positive("powerful drop")||positive("huge drop")||positive("massive drop")||positive("explosive drop"))result.sectionDirections|=strongDrop;
    if(positive("soft drop")||positive("restrained drop")||positive("gentle drop"))result.sectionDirections|=softDrop;
    if(positive("quiet breakdown")||positive("calm breakdown")||positive("sparse breakdown"))result.sectionDirections|=quietBreak;
    if(positive("intense breakdown")||positive("energetic breakdown"))result.sectionDirections|=intenseBreak;

    if(phrase("straight rhythm")||phrase("straight groove")||phrase("no swing"))
        result.rhythmicFeel=-1;
    else if(positive("swing")||positive("swung")||positive("shuffle"))
        result.rhythmicFeel=2;
    else if(positive("syncopated")||positive("offbeat")||positive("off-beat"))
        result.rhythmicFeel=1;

    if(positive("bright")||positive("airy")||positive("open tone"))result.brightnessDirection=1;
    else if(positive("dark")||positive("muted")||positive("muffled"))result.brightnessDirection=-1;
    if(positive("spacious")||positive("wide")||positive("ambient")||positive("large space"))result.spaceDirection=1;
    else if(positive("dry")||positive("intimate")||positive("tight space"))result.spaceDirection=-1;
    if(positive("aggressive")||positive("hard")||positive("heavy"))result.aggressionDirection=1;
    else if(positive("soft")||positive("gentle")||positive("controlled"))result.aggressionDirection=-1;

    if(positive("euphoric drop")||positive("anthem drop"))result.dropCharacter=3;
    else if(positive("driving drop")||positive("rhythmic drop"))result.dropCharacter=2;
    else if(positive("heavy drop")||positive("hard drop")||positive("bass drop"))result.dropCharacter=4;
    else if(positive("melodic drop"))result.dropCharacter=1;

    if(phrase("simple harmony")||phrase("static harmony")||phrase("simple chords"))result.harmonicMotionDirection=-1;
    else if(positive("moving harmony")||positive("passing harmony")||positive("borrowed harmony")||positive("harmonic movement"))result.harmonicMotionDirection=1;

    if(phrase("same final drop")||phrase("repeat final drop")||phrase("faithful final drop"))result.finalEvolutionDirection=-1;
    else if(positive("evolving final drop")||positive("developed final drop")||positive("bigger second drop")||positive("evolving second drop")||positive("final drop variation"))result.finalEvolutionDirection=1;
    return result;
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

    // Harmonic-minor V and borrowed minor iv are the two mainstream-safe modal
    // colours. Other borrowed qualities stay out until a later harmony system can
    // represent their altered roots explicitly.
    if(event.borrowed&&minor&&d==4)third=root+4;
    else if(event.borrowed&&!minor&&d==3)third=root+3;

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
    const auto p=resolvedPrompt.toLowerCase();
    const bool tech=p.contains("tech house")||p.contains("minimal house");
    const bool cinematic=p.contains("cinematic")||p.contains("film");
    const bool pop=p.contains("pop")||p.contains("radio");
    const bool progressive=p.contains("progressive")||p.contains("melodic house");
    const bool futureRave=p.contains("future rave")||p.contains("mainstage")||p.contains("festival");
    const bool electro=p.contains("electro house")||p.contains("big room");
    const bool trance=p.contains("trance");
    const bool dnb=p.contains("drum and bass")||p.contains("dnb");
    const bool dreamy=p.contains("dreamy");
    const bool aggressive=p.contains("aggressive")||p.contains("powerful")||p.contains("hard");
    const bool uplifting=p.contains("uplifting")||p.contains("bright");

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

    const float harmonicMotion=juce::jlimit(0.f,1.f,plan.harmonicMotion);
    harmonyPlan.tension=juce::jlimit(.12f,.92f,harmonyPlan.tension+.14f*(harmonicMotion-.5f)+.10f*(plan.aggression-.5f));
    harmonyPlan.extensionProbability=juce::jlimit(0.f,.82f,harmonyPlan.extensionProbability*(.55f+.95f*harmonicMotion));
    harmonyPlan.suspensionProbability=juce::jlimit(0.f,.72f,harmonyPlan.suspensionProbability*(.50f+1.10f*harmonicMotion));
    harmonyPlan.passingProbability=juce::jlimit(0.f,.62f,harmonyPlan.passingProbability*(.55f+1.25f*harmonicMotion));
    harmonyPlan.borrowedProbability=juce::jlimit(0.f,.68f,harmonyPlan.borrowedProbability*(.62f+.95f*harmonicMotion));

    if(dreamy){harmonyPlan.extensionProbability=juce::jmax(harmonyPlan.extensionProbability,.28f);harmonyPlan.suspensionProbability=juce::jmax(harmonyPlan.suspensionProbability,.24f);}
    if(aggressive){harmonyPlan.extensionProbability*=.45f;harmonyPlan.tension=juce::jmax(harmonyPlan.tension,.58f);}
    if(uplifting)harmonyPlan.cadenceStyle=0;
    if(p.contains("simple chords")||p.contains("clean chords"))
    {
        harmonyPlan.extensionProbability=0.f;
        harmonyPlan.suspensionProbability=0.f;
        harmonyPlan.borrowedProbability=0.f;
    }
    if(p.contains("7th chord")||p.contains("extended chord"))
        harmonyPlan.extensionProbability=juce::jmax(harmonyPlan.extensionProbability,.68f);
    if(p.contains("suspended chord")||p.contains("sus chord"))
        harmonyPlan.suspensionProbability=juce::jmax(harmonyPlan.suspensionProbability,.58f);
    if(p.contains("borrowed chord")||p.contains("modal interchange"))
        harmonyPlan.borrowedProbability=juce::jmax(harmonyPlan.borrowedProbability,.62f);

    // v2.4 starts from a genre-aware functional progression family. Variation is
    // made with controlled substitutions below, not by drawing unrelated degrees
    // from a bag. Scale-degree representation keeps every family key invariant.
    harmonyPlan.mainDegrees.fill(0);
    harmonyPlan.alternateDegrees.fill(0);
    auto setProgression=[&](std::initializer_list<int> degrees)
    {
        harmonyPlan.progressionLength=juce::jlimit(2,8,(int)degrees.size());
        int i=0;for(const int degree:degrees)
            if(i<harmonyPlan.progressionLength)harmonyPlan.mainDegrees[(size_t)i++]=juce::jlimit(0,6,degree);
    };

    const int variant=(int)(random01(seed,0x3100)*8.f)%8;
    if(tech)
    {
        switch(variant%4)
        {
            case 0:setProgression({0,5});break;
            case 1:setProgression({0,6});break;
            case 2:setProgression({0,3});break;
            default:setProgression({0,5,6});break;
        }
    }
    else if(cinematic)
    {
        switch(variant%4)
        {
            case 0:setProgression({0,5,2,6,0,3});break;
            case 1:setProgression({0,3,5,4,0,6,3,4});break;
            case 2:setProgression({5,2,0,6,3,4});break;
            default:setProgression({0,6,5,3,0,2,3,4});break;
        }
    }
    else if(dnb)
    {
        switch(variant%4)
        {
            case 0:setProgression({0,5,3,6});break;
            case 1:setProgression({0,3,6,5});break;
            case 2:setProgression({5,0,4,3});break;
            default:setProgression({0,6,3,4});break;
        }
    }
    else if(trance)
    {
        switch(variant%4)
        {
            case 0:setProgression({0,5,3,4});break;
            case 1:setProgression({5,3,0,4});break;
            case 2:setProgression({0,3,5,4});break;
            default:setProgression({0,5,2,3,0,6,5,4});break;
        }
    }
    else if(futureRave||electro)
    {
        switch(variant%4)
        {
            case 0:setProgression({0,5,3,4});break;
            case 1:setProgression({0,3,5,4});break;
            case 2:setProgression({5,0,3,4});break;
            default:setProgression({0,6,5,4});break;
        }
    }
    else if(progressive)
    {
        switch(variant%6)
        {
            case 0:setProgression({0,5,2,6});break;
            case 1:setProgression({0,3,5,4});break;
            case 2:setProgression({5,3,0,4});break;
            case 3:setProgression({0,6,5,3});break;
            case 4:setProgression({0,2,5,4,0,3});break;
            default:setProgression({0,5,3,6,0,2,3,4});break;
        }
    }
    else if(pop)
    {
        switch(variant%5)
        {
            case 0:setProgression({0,4,5,3});break;
            case 1:setProgression({5,3,0,4});break;
            case 2:setProgression({0,5,3,4});break;
            case 3:setProgression({0,3,4,5});break;
            default:setProgression({5,3,0,4,5,4});break;
        }
    }
    else
    {
        // Generic EDM still follows tonic -> departure -> predominant/dominant
        // function, with enough seed variation to avoid a hidden fixed loop.
        switch(variant%5)
        {
            case 0:setProgression({0,5,3,4});break;
            case 1:setProgression({0,2,5,4});break;
            case 2:setProgression({0,6,3,4});break;
            case 3:setProgression({5,3,0,4});break;
            default:setProgression({0,5,2,6,3,4});break;
        }
    }

    const int n=harmonyPlan.progressionLength;

    if(!tech&&n>1)
    {
        // Cadence style changes the loop's actual harmonic destination: dominant,
        // leading-tone tension, plagal return, or dominant with a deceptive colour
        // in the related chorus sequence.
        const int ending=harmonyPlan.cadenceStyle==1?6:
                         (harmonyPlan.cadenceStyle==2?3:4);
        harmonyPlan.mainDegrees[(size_t)n-1]=ending;
    }

    // Give template families many identities without destroying their function.
    // Only interior tonic/predominant substitutions are allowed; the turnaround
    // is preserved so the loop still resolves intentionally.
    if(n>=4&&random01(seed,0x3150)<.72f)
    {
        const int position=1+(int)(random01(seed,0x3151)*(float)(n-2))%(n-2);
        const int d=harmonyPlan.mainDegrees[(size_t)position];
        static constexpr int substitutes[7]={5,3,5,1,6,3,4};
        static constexpr int secondChoice[7]={2,5,3,5,6,1,3};
        int replacement=substitutes[d];
        if(replacement==harmonyPlan.mainDegrees[(size_t)position-1]
           ||replacement==harmonyPlan.mainDegrees[(size_t)position+1])
            replacement=secondChoice[d];
        if(replacement!=harmonyPlan.mainDegrees[(size_t)position-1]
           &&replacement!=harmonyPlan.mainDegrees[(size_t)position+1])
            harmonyPlan.mainDegrees[(size_t)position]=replacement;
    }
    if(n>=5&&random01(seed,0x3152)<harmonyPlan.passingProbability)
    {
        const int target=harmonyPlan.mainDegrees[(size_t)n-1];
        harmonyPlan.mainDegrees[(size_t)n-2]=(target+(random01(seed,0x3153)<.5f?6:1))%7;
    }

    // Alternate progression remains recognisably related for chorus/final-hook
    // development, but uses functional substitutes instead of random offsets.
    for(int i=0;i<n;++i)
    {
        int d=harmonyPlan.mainDegrees[(size_t)i];
        const float r=random01(seed,0x3200+i*23);
        if(i==0)d=(r<.72f?0:d);
        else if(i<n-1&&r<.42f)
        {
            static constexpr int substitutes[7]={5,3,5,1,6,3,4};
            d=substitutes[d];
        }
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
    const auto prompt=resolvedPrompt.toLowerCase();
    const bool explicitBorrowed=prompt.contains("borrowed chord")||prompt.contains("modal interchange");
    const bool explicitExtended=prompt.contains("7th chord")||prompt.contains("extended chord");
    bool placedBorrowedColour=false,placedExtendedColour=false;

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
            // An explicit borrowed-colour request must produce one real modal
            // event even when the seeded progression contains no eligible iv.
            // Place it at the first chorus/breakdown opportunity; subsequent
            // events retain the planned functional progression.
            if(explicitBorrowed&&!placedBorrowedColour&&(chorus||breakdown))
                event.scaleDegree=minor?4:3;
            const bool borrowedCandidate=(minor&&event.scaleDegree==4)
                                      ||(!minor&&event.scaleDegree==3);
            event.borrowed=borrowedCandidate
                &&((explicitBorrowed&&!placedBorrowedColour)
                   ||random01(seed,0x4200+(uint64_t)harmonyEvents.size())<harmonyPlan.borrowedProbability);
            placedBorrowedColour|=event.borrowed;

            const float ext=random01(seed,0x4300+(uint64_t)harmonyEvents.size());
            const int nextDegree=sequence[(size_t)((step+1)%n)];
            const bool suspensionResolves=nextDegree==0||nextDegree==3||nextDegree==4;
            if(explicitExtended&&!placedExtendedColour&&(chorus||breakdown))
                event.extension=random01(seed,0x4301+(uint64_t)harmonyEvents.size())>.5f?1:2;
            else if(suspensionResolves&&ext<harmonyPlan.suspensionProbability)
                event.extension=random01(seed,0x4310+(uint64_t)harmonyEvents.size())>.5f?3:4;
            else if(ext<harmonyPlan.suspensionProbability+harmonyPlan.extensionProbability)
                event.extension=random01(seed,0x4320+(uint64_t)harmonyEvents.size())>.5f?1:2;
            placedExtendedColour|=event.extension==1||event.extension==2;

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
                  ^ ((uint64_t)e.extension<<32) ^ ((uint64_t)(e.borrowed?1:0)<<36));
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


juce::String SongArrangement::getPromptIntentSummary() const
{
    static constexpr const char* genres[]={"GENERAL","PROGRESSIVE","FESTIVAL","TECH HOUSE","TRANCE","DRUM & BASS","CINEMATIC","POP"};
    static constexpr const char* emotions[]={"NEUTRAL","EMOTIONAL","UPLIFTING","DARK","DREAMY","AGGRESSIVE"};
    static constexpr const char* densities[]={"SPARSE","NATURAL","DENSE"};
    static constexpr const char* structures[]={"AUTO","RADIO","FESTIVAL","PROGRESSIVE","CINEMATIC"};
    static constexpr const char* pitchNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    juce::String exclusions="NONE";
    juce::StringArray excluded;
    if((promptIntent.exclusionMask&1u)!=0u)excluded.add("COUNTER");
    if((promptIntent.exclusionMask&2u)!=0u)excluded.add("PAD");
    if((promptIntent.exclusionMask&4u)!=0u)excluded.add("ARP");
    if((promptIntent.exclusionMask&8u)!=0u)excluded.add("FX");
    if((promptIntent.exclusionMask&16u)!=0u)excluded.add("HATS");
    static constexpr const char* otherNames[]={"BASS","SUB","CHORDS","LEAD","KICK","SNARE","PERCUSSION"};
    for(int i=0;i<7;++i)if((promptIntent.exclusionMask&(32u<<i))!=0u)excluded.add(otherNames[i]);
    if(!excluded.isEmpty())exclusions=excluded.joinIntoString(",");
    return "TEMPO: "+juce::String(promptIntent.tempo,1)
        +" | KEY: "+pitchNames[((promptIntent.rootMidi%12)+12)%12]+" "+(promptIntent.minor?"MINOR":"MAJOR")
        +" | GENRE: "+genres[juce::jlimit(0,7,promptIntent.genreFamily)]
        +" | EMOTION: "+emotions[juce::jlimit(0,5,promptIntent.emotionProfile)]
        +" | DENSITY: "+densities[juce::jlimit(0,2,promptIntent.densityDirection+1)]
        +" | STRUCTURE: "+structures[juce::jlimit(0,4,promptIntent.structureStyle+1)]
        +" | EXCLUDE: "+exclusions;
}

juce::String SongArrangement::getProducerPlanSummary() const
{
    static constexpr const char* genres[]={"GENERAL","PROGRESSIVE","FESTIVAL","TECH HOUSE","TRANCE","DRUM & BASS","CINEMATIC","POP"};
    static constexpr const char* emotions[]={"NEUTRAL","EMOTIONAL","UPLIFTING","DARK","DREAMY","AGGRESSIVE"};
    static constexpr const char* hooks[]={"CALL / RESPONSE","RISING","FALLING","ARCH"};
    static constexpr const char* chords[]={"SUSTAINED","RHYTHMIC","ANTHEM","DRY STABS"};
    static constexpr const char* pads[]={"VERSE + BREAKDOWN","BREAKDOWN ONLY","ATMOSPHERIC","OFF"};
    static constexpr const char* counters[]={"FINAL ONLY","DROP + FINAL","HOOK SECTIONS","OFF"};
    static constexpr const char* feels[]={"STRAIGHT","SYNCOPATED","SWUNG"};
    static constexpr const char* drops[]={"MELODIC","DRIVING","EUPHORIC","HEAVY"};
    return "STYLE: "+juce::String(genres[juce::jlimit(0,7,plan.genreFamily)])
        +" | EMOTION: "+emotions[juce::jlimit(0,5,plan.emotionProfile)]
        +" | HOOK: "+hooks[juce::jlimit(0,3,plan.hookShape)]
        +" | CHORDS: "+chords[juce::jlimit(0,3,plan.chordTexture)]
        +" | PAD: "+pads[juce::jlimit(0,3,plan.padPolicy)]
        +" | COUNTER: "+counters[juce::jlimit(0,3,plan.counterPolicy)]
        +" | FEEL: "+feels[juce::jlimit(0,2,plan.rhythmicFeel)]
        +" | DROP TYPE: "+drops[juce::jlimit(0,3,plan.dropCharacter-1)]
        +" | BRIGHT: "+juce::String((int)std::llround(plan.brightness*100.f))+"%"
        +" | SPACE: "+juce::String((int)std::llround(plan.space*100.f))+"%"
        +" | AGGRESSION: "+juce::String((int)std::llround(plan.aggression*100.f))+"%"
        +" | HARMONIC MOTION: "+juce::String((int)std::llround(plan.harmonicMotion*100.f))+"%"
        +" | FINAL EVOLUTION: "+juce::String((int)std::llround(plan.finalEvolution*100.f))+"%"
        +" | DROP: "+juce::String((int)std::llround(plan.dropIntensity*100.f))+"%";
}

juce::String SongArrangement::getSectionGoalSummary() const
{
    juce::StringArray parts;
    const size_t count=juce::jmin(sections.size(),sectionGoals.size());
    for(size_t i=0;i<count;++i)
    {
        const auto& g=sectionGoals[i];
        parts.add(sections[i].name+": E"+juce::String((int)std::lround(g.energy*100.f))
            +" D"+juce::String((int)std::lround(g.density*100.f))
            +" M"+juce::String((int)std::lround(g.melodyActivity*100.f))
            +" B"+juce::String((int)std::lround(g.bassDrive*100.f))
            +" R"+juce::String((int)std::lround(g.drumDrive*100.f))
            +" S"+juce::String((int)std::lround(g.space*100.f)));
    }
    return parts.joinIntoString(" | ");
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

    // v2.3 fingerprints the musical hook in section-local time. The old flat
    // first-128-notes comparison could miss a recycled chorus whenever a shorter
    // intro shifted note indices, or call a relocated hook "new" because its
    // absolute beat changed. Prefer CHORUS, then DROP, and compare one four-bar
    // phrase independent of where it occurs in the arrangement.
    const ArrangementSection* hook=nullptr;
    for(const auto& section:sections)if(section.name=="CHORUS"){hook=&section;break;}
    if(hook==nullptr)
        for(const auto& section:sections)if(section.name.contains("DROP")){hook=&section;break;}

    const double phraseStart=hook!=nullptr?hook->startBar*beatsPerBar:lead->notes.front().beat;
    const double phraseEnd=hook!=nullptr
        ?juce::jmin((double)(hook->startBar+hook->bars)*beatsPerBar,phraseStart+16.0)
        :phraseStart+16.0;

    std::vector<const ArrangementNote*> phrase;
    phrase.reserve(32);
    for(const auto& note:lead->notes)
        if(note.beat>=phraseStart-.001&&note.beat<phraseEnd-.001)
            phrase.push_back(&note);
    if(phrase.empty())return fp;
    if(phrase.size()>64)phrase.resize(64);

    const int* scale=nullptr;
    static constexpr int minorScale[7]={0,2,3,5,7,8,10};
    static constexpr int majorScale[7]={0,2,4,5,7,9,11};
    scale=minor?minorScale:majorScale;
    auto degreeFor=[&](int midi)
    {
        const int pc=((midi-rootMidi)%12+12)%12;
        for(int degree=0;degree<7;++degree)if(scale[degree]==pc)return degree;
        return 7+pc; // defensive marker for a non-scale pitch
    };
    auto chordRoleFor=[&](const ArrangementNote& note)
    {
        const auto* event=harmonyAtBeat(note.beat);
        if(event==nullptr)return 4;
        const auto tones=chordTonesFor(*event);
        const int pc=((note.note%12)+12)%12;
        for(int role=0;role<4;++role)
            if(((tones[(size_t)role]%12)+12)%12==pc)return role;
        return 4; // controlled non-chord tone
    };

    fp.reserve(2+phrase.size()*melodyFingerprintStride+melodyFingerprintSummarySize);
    fp.push_back(melodyFingerprintVersion);
    fp.push_back((int)phrase.size());

    int previousNote=phrase.front()->note;
    double previousBeat=phrase.front()->beat;
    int repeatedRun=0,repeatedTransitions=0;
    int canonicalHeight=0,minCanonicalHeight=0,maxCanonicalHeight=0;
    bool usedDegree[16]{};
    std::array<int,4> firstOnsets{32,32,32,32};
    int cadenceDegree=degreeFor(phrase.back()->note);
    int cadenceChordRole=chordRoleFor(*phrase.back());

    for(size_t i=0;i<phrase.size();++i)
    {
        const auto& note=*phrase[i];
        const int degree=degreeFor(note.note);
        // Octave repair near the playable-range boundaries must not turn a
        // transposed copy into a new melody. Keep the signed pitch-class motion
        // and reconstruct a canonical relative register from that motion.
        int interval=i==0?0:(note.note-previousNote)%12;
        while(interval>6)interval-=12;
        while(interval<-6)interval+=12;
        if(i>0)canonicalHeight+=interval;
        minCanonicalHeight=juce::jmin(minCanonicalHeight,canonicalHeight);
        maxCanonicalHeight=juce::jmax(maxCanonicalHeight,canonicalHeight);
        const double relativeBeat=juce::jmax(0.0,note.beat-phraseStart);
        const int onset=juce::jlimit(0,127,(int)std::llround(relativeBeat*8.0));
        const int bar=juce::jlimit(0,3,(int)std::floor(relativeBeat/beatsPerBar));
        const int onsetInBar=onset-bar*32;
        firstOnsets[(size_t)bar]=juce::jmin(firstOnsets[(size_t)bar],onsetInBar);

        repeatedRun=(i>0&&note.note==previousNote)?repeatedRun+1:0;
        if(repeatedRun>0)++repeatedTransitions;
        int cadence=0;
        if(relativeBeat>=15.0)cadence=2;
        else if(std::fmod(relativeBeat,8.0)>=7.0)cadence=1;

        fp.push_back(degree);
        fp.push_back(interval+24);
        fp.push_back(interval>0?2:(interval<0?0:1));
        fp.push_back(chordRoleFor(note));
        fp.push_back(onset);
        fp.push_back(i==0?0:juce::jlimit(0,96,(int)std::llround((note.beat-previousBeat)*8.0)));
        fp.push_back(juce::jlimit(1,32,(int)std::llround(note.length*8.0)));
        fp.push_back(bar);
        fp.push_back(juce::jlimit(0,3,repeatedRun));
        fp.push_back(cadence);
        fp.push_back(juce::jlimit(0,6,canonicalHeight/12+3));

        if(degree>=0&&degree<16)usedDegree[degree]=true;
        previousNote=note.note;previousBeat=note.beat;
    }

    int uniqueDegrees=0;for(bool used:usedDegree)if(used)++uniqueDegrees;
    fp.push_back(maxCanonicalHeight-minCanonicalHeight);
    fp.push_back(uniqueDegrees);
    fp.push_back(repeatedTransitions);
    for(const int onset:firstOnsets)fp.push_back(onset);
    fp.push_back(cadenceDegree);
    fp.push_back(cadenceChordRole);
    return fp;
}

std::vector<int> SongArrangement::getStructureFingerprint() const
{
    std::vector<int> fp;
    fp.reserve(sections.size()*7+1);
    fp.push_back((int)sections.size());
    for(size_t i=0;i<sections.size();++i)
    {
        const auto& section=sections[i];
        fp.push_back(section.startBar);
        fp.push_back(section.bars);
        fp.push_back((int)std::llround(section.energy*100.f));
        if(i<sectionGoals.size())
        {
            const auto& goal=sectionGoals[i];
            fp.push_back((int)std::llround(goal.density*100.f));
            fp.push_back((int)std::llround(goal.melodyActivity*100.f));
            fp.push_back((int)std::llround(goal.bassDrive*100.f));
            fp.push_back((int)std::llround(goal.drumDrive*100.f));
        }
    }
    return fp;
}

void SongArrangement::clear()
{
    lanes.clear();
    sections.clear();
    sectionGoals.clear();
    harmonyEvents.clear();
    palettePlan=SoundPalettePlan{};
    harmonyId=0;
    melodyId=0;
}

int SongArrangement::parseRootMidi(const juce::String& raw, bool& minorOut)
{
    const auto parsed=parsePromptIntent(raw,128.0);
    minorOut=parsed.minor;
    return parsed.rootMidi;
}

void SongArrangement::buildSongPlan(uint64_t seed)
{
    juce::ignoreUnused(seed);
    const auto p=resolvedPrompt.toLowerCase();
    plan.structureVariant=(int)(random01(domains.structure,0x1001)*8.f)%8;
    plan.structureStyle=plan.structureVariant%4;
    // Structure families: radio/songwriter, festival, progressive and cinematic.
    // Genre supplies the architecture while the independent structure seed still
    // changes section lengths, so a prompt does not collapse to one fixed song.
    const bool festivalStructure=p.contains("festival")||p.contains("mainstage")
        ||p.contains("big room");
    const bool cinematicStructure=p.contains("cinematic")||p.contains("film");
    const bool progressiveStructure=p.contains("progressive house")
        ||p.contains("melodic house")||p.contains("trance");
    const bool radioStructure=p.contains("radio")||p.contains("pop")
        ||(p.contains("house")&&!festivalStructure&&!progressiveStructure);
    if(radioStructure)plan.structureStyle=0;
    if(festivalStructure)plan.structureStyle=1;
    if(progressiveStructure)plan.structureStyle=2;
    if(cinematicStructure)plan.structureStyle=3;
    if(promptIntent.structureStyle>=0)plan.structureStyle=promptIntent.structureStyle;
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
    // v2.9 energy is planned before any lane renders. A little seed variation
    // keeps songs from sharing an identical contour, while explicit intent can
    // widen or soften the complete curve without flattening section roles.
    plan.energyContrast=.94f+.12f*random01(domains.structure,0x1016);
    plan.energyBias=-.015f+.03f*random01(domains.structure,0x1017);
    plan.transitionIntensity=.92f+.16f*random01(domains.fx,0x1018);

    // v3.0 producer brain: choose one semantic production direction before any
    // lane is rendered. Downstream systems consume these decisions instead of
    // independently guessing their role from the raw prompt.
    const bool producerFestival=p.contains("festival")||p.contains("mainstage")||p.contains("big room")||p.contains("future rave");
    const bool producerProgressive=p.contains("progressive house")||p.contains("melodic house");
    const bool producerTech=p.contains("tech house")||p.contains("minimal house");
    const bool producerTrance=p.contains("trance");
    const bool producerDnb=p.contains("drum and bass")||p.contains("dnb");
    const bool producerCinematic=p.contains("cinematic")||p.contains("film");
    const bool producerPop=p.contains("pop")||p.contains("radio");
    plan.genreFamily=promptIntent.genreFamily;
    plan.emotionProfile=promptIntent.emotionProfile;
    plan.hookShape=(int)(random01(domains.melody,0x1019)*4.f)%4;
    const bool explicitRising=promptIntent.hookShape==1;
    const bool explicitFalling=promptIntent.hookShape==2;
    if(explicitRising){plan.hookShape=1;plan.melodyArchetype=9;}
    else if(explicitFalling){plan.hookShape=2;plan.melodyArchetype=8;}
    else if(promptIntent.hookShape==0){plan.hookShape=0;plan.melodyArchetype=0;}
    else if(promptIntent.hookShape==3){plan.hookShape=3;plan.melodyArchetype=3;}
    plan.hookStrength=.62f+.20f*random01(domains.melody,0x101a);
    if(p.contains("memorable")||p.contains("catchy")||p.contains("strong hook"))plan.hookStrength=juce::jmax(plan.hookStrength,.90f);
    if(p.contains("subtle hook"))plan.hookStrength=juce::jmin(plan.hookStrength,.58f);
    plan.dropIntensity=.82f+.18f*random01(domains.structure,0x101b);
    if(p.contains("powerful drop")||p.contains("huge drop")||p.contains("massive drop"))plan.dropIntensity=1.15f;
    if(p.contains("soft drop")||p.contains("restrained drop"))plan.dropIntensity=.72f;
    plan.drumDrive=.82f+.20f*random01(domains.drums,0x101c);
    if(p.contains("huge drums")||p.contains("powerful drums")||p.contains("hard drums"))plan.drumDrive=1.15f;
    if(p.contains("soft drums")||p.contains("restrained drums"))plan.drumDrive=.72f;
    plan.chordTexture=producerTech?3:((producerFestival||producerProgressive||producerTrance)?2:0);
    if(p.contains("rhythmic chords")||p.contains("pulsing chords"))plan.chordTexture=1;
    if(p.contains("warm chords")||p.contains("sustained chords"))plan.chordTexture=0;
    plan.padPolicy=producerTech?3:(producerFestival?1:(producerCinematic?2:0));
    plan.counterPolicy=producerTech?3:((producerProgressive&&plan.emotionProfile==1)?0:1);
    if(p.contains("counter melody")||p.contains("secondary lead")
       ||p.contains("call and response")||p.contains("answer melody"))plan.counterPolicy=2;
    if((promptIntent.exclusionMask&1u)!=0u)plan.counterPolicy=3;
    if((promptIntent.exclusionMask&2u)!=0u)plan.padPolicy=3;

    const bool lowEnergy=promptIntent.energyDirection<0;
    const bool highEnergy=promptIntent.energyDirection>0;
    if(lowEnergy)
    {
        plan.energyContrast=juce::jmin(plan.energyContrast,.82f);
        plan.energyBias=juce::jmin(plan.energyBias,-.055f);
        plan.transitionIntensity=juce::jmin(plan.transitionIntensity,.78f);
    }
    if(highEnergy)
    {
        plan.energyContrast=juce::jmax(plan.energyContrast,1.10f);
        plan.energyBias=juce::jmax(plan.energyBias,.035f);
        plan.transitionIntensity=juce::jmax(plan.transitionIntensity,1.12f);
    }

    if(promptIntent.densityDirection<0){plan.density*=.62f;plan.restAmount=juce::jmax(plan.restAmount,.30f);}
    if(promptIntent.densityDirection>0){plan.density=juce::jmin(1.f,plan.density+.18f);plan.development=juce::jmax(plan.development,.72f);}
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
    if(p.contains("short hook"))
    {
        plan.phraseBars=4;
        // motifLength is a real two-bar note budget, not descriptive metadata.
        // A short hook therefore means two concise notes per bar.
        plan.motifLength=juce::jlimit(3,4,plan.motifLength);
    }
    if(p.contains("long melody"))
    {
        plan.phraseBars=8;
        plan.motifLength=juce::jmax(plan.motifLength,7);
    }
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
    }

    // v2.5 bass direction is a composition decision, not a side effect of a
    // random seed. Genre supplies a useful default and explicit wording wins.
    // Modes: sustained root, offbeat house, octave pickup, rolling, syncopated.
    const bool techHouse=p.contains("tech house")||p.contains("minimal house");
    const bool drumAndBass=p.contains("drum and bass")||p.contains("dnb");
    const bool trance=p.contains("trance");
    const bool futureRave=p.contains("future rave")||p.contains("electro house")
        ||p.contains("big room")||p.contains("mainstage");
    if(techHouse)plan.bassMode=3;
    else if(drumAndBass)plan.bassMode=4;
    else if(trance)plan.bassMode=1;
    else if(futureRave)plan.bassMode=2;
    else if(progressive)plan.bassMode=1;
    else if(p.contains("pop"))plan.bassMode=0;

    if(p.contains("sustained bass")||p.contains("long bass"))plan.bassMode=0;
    else if(p.contains("offbeat bass")||p.contains("off-beat bass"))plan.bassMode=1;
    else if(p.contains("octave pickup")||p.contains("progressive bass"))plan.bassMode=2;
    else if(p.contains("rolling bass")||p.contains("rolling low end"))plan.bassMode=3;
    else if(p.contains("syncopated bass")||p.contains("melodic bass")
            ||p.contains("moving bass")||p.contains("reese bass"))plan.bassMode=4;

    // Resolve explicit high-level directions last so lower-level genre defaults
    // cannot accidentally overwrite the producer's central decision.
    if(explicitRising)plan.melodyArchetype=9;
    else if(explicitFalling)plan.melodyArchetype=8;
    if(plan.chordTexture==0&&(p.contains("warm chords")||p.contains("sustained chords")))plan.chordMode=0;
    else if(plan.chordTexture==1)plan.chordMode=1;
    else if(plan.chordTexture==2)plan.chordMode=juce::jmax(plan.chordMode,2);

    const float feelRoll=random01(domains.drums,0x4010);
    plan.rhythmicFeel=feelRoll>.78f?2:(feelRoll>.44f?1:0);
    if(promptIntent.rhythmicFeel<0)plan.rhythmicFeel=0;
    else if(promptIntent.rhythmicFeel>0)plan.rhythmicFeel=juce::jlimit(1,2,promptIntent.rhythmicFeel);

    plan.dropCharacter=promptIntent.dropCharacter>0?promptIntent.dropCharacter:
        (producerFestival?3:(producerTech?2:(producerDnb?4:1)));
    plan.brightness=.30f+.40f*random01(domains.soundPalette,0x4011);
    plan.space=.28f+.44f*random01(domains.soundPalette,0x4012);
    plan.aggression=.24f+.52f*random01(domains.structure,0x4013);
    plan.harmonicMotion=.30f+.48f*random01(domains.harmony,0x4014);
    plan.finalEvolution=.50f+.34f*random01(domains.melody,0x4015);
    plan.callResponse=.34f+.42f*random01(domains.counter,0x4016);

    if(plan.emotionProfile==2)plan.brightness=juce::jmax(plan.brightness,.72f);
    if(plan.emotionProfile==3)plan.brightness=juce::jmin(plan.brightness,.34f);
    if(plan.emotionProfile==4){plan.space=juce::jmax(plan.space,.74f);plan.aggression=juce::jmin(plan.aggression,.42f);}
    if(plan.emotionProfile==5)plan.aggression=juce::jmax(plan.aggression,.80f);

    if(promptIntent.brightnessDirection<0)plan.brightness=juce::jmin(plan.brightness,.28f);
    else if(promptIntent.brightnessDirection>0)plan.brightness=juce::jmax(plan.brightness,.80f);
    if(promptIntent.spaceDirection<0)plan.space=juce::jmin(plan.space,.22f);
    else if(promptIntent.spaceDirection>0)plan.space=juce::jmax(plan.space,.80f);
    if(promptIntent.aggressionDirection<0)plan.aggression=juce::jmin(plan.aggression,.30f);
    else if(promptIntent.aggressionDirection>0)plan.aggression=juce::jmax(plan.aggression,.84f);
    if(promptIntent.harmonicMotionDirection<0)plan.harmonicMotion=juce::jmin(plan.harmonicMotion,.24f);
    else if(promptIntent.harmonicMotionDirection>0)plan.harmonicMotion=juce::jmax(plan.harmonicMotion,.82f);
    if(promptIntent.finalEvolutionDirection<0)plan.finalEvolution=.18f;
    else if(promptIntent.finalEvolutionDirection>0)plan.finalEvolution=.94f;

    if(plan.rhythmicFeel==1)plan.syncopation=juce::jmax(plan.syncopation,.68f);
    else if(plan.rhythmicFeel==2)plan.syncopation=juce::jmax(plan.syncopation,.52f);
    if(promptIntent.rhythmicFeel<0)plan.syncopation=juce::jmin(plan.syncopation,.20f);
    if(plan.hookShape==0||p.contains("call and response")||p.contains("answer melody"))plan.callResponse=juce::jmax(plan.callResponse,.86f);

    if(plan.dropCharacter==1){plan.hookStrength=juce::jmax(plan.hookStrength,.86f);plan.development=juce::jmax(plan.development,.62f);}
    else if(plan.dropCharacter==2){plan.density=juce::jmax(plan.density,.70f);plan.drumDrive=juce::jmax(plan.drumDrive,1.02f);plan.restAmount=juce::jmin(plan.restAmount,.16f);}
    else if(plan.dropCharacter==3){plan.hookStrength=juce::jmax(plan.hookStrength,.92f);plan.space=juce::jmax(plan.space,.70f);plan.finalEvolution=juce::jmax(plan.finalEvolution,.72f);}
    else if(plan.dropCharacter==4){plan.aggression=juce::jmax(plan.aggression,.88f);plan.dropIntensity=juce::jmax(plan.dropIntensity,1.08f);plan.drumDrive=juce::jmax(plan.drumDrive,1.08f);plan.space=juce::jmin(plan.space,.42f);}
    if(promptIntent.densityDirection<0){plan.density=juce::jmin(plan.density,.48f);plan.restAmount=juce::jmax(plan.restAmount,.30f);}
    if(promptIntent.spaceDirection<0)plan.space=juce::jmin(plan.space,.22f);
    else if(promptIntent.spaceDirection>0)plan.space=juce::jmax(plan.space,.80f);
    if(promptIntent.aggressionDirection<0)plan.aggression=juce::jmin(plan.aggression,.30f);
    if(promptIntent.finalEvolutionDirection<0)plan.finalEvolution=.18f;
}

void SongArrangement::buildSections(uint64_t seed)
{
    juce::ignoreUnused(seed);
    std::array<juce::String,11> names;
    std::array<int,11> lengths{};

    // A SongPlan selects architecture before any MIDI is emitted. All families
    // retain stable role labels for the UI/export path, but their actual order
    // and duration are genre appropriate instead of one renamed fixed template.
    switch(plan.structureStyle)
    {
        case 1: // festival: two drop statements around the breakdown
            names={"INTRO","BUILD","CHORUS","DROP","VERSE","BREAKDOWN","VERSE 2","BUILD 2","DROP 2","FINAL HOOK","OUTRO"};
            lengths={4,4,8,12,8,4,8,4,12,12,4};
            break;
        case 2: // progressive: patient full songwriter arc
            names={"INTRO","VERSE","BUILD","CHORUS","DROP","BREAKDOWN","VERSE 2","BUILD 2","DROP 2","FINAL HOOK","OUTRO"};
            lengths={4,8,4,8,12,8,8,4,12,12,4};
            break;
        case 3: // cinematic: release before the late full-impact drop
            names={"INTRO","VERSE","BUILD","CHORUS","BREAKDOWN","VERSE 2","BUILD 2","DROP","DROP 2","FINAL HOOK","OUTRO"};
            lengths={8,8,4,8,8,8,4,8,8,12,4};
            break;
        default: // radio/songwriter
            names={"INTRO","VERSE","BUILD","CHORUS","DROP","BREAKDOWN","VERSE 2","BUILD 2","DROP 2","FINAL HOOK","OUTRO"};
            lengths={4,8,4,8,8,4,8,4,8,8,4};
            break;
    }

    auto indexNamed=[&](const juce::String& wanted)
    {
        for(size_t i=0;i<names.size();++i)if(names[i]==wanted)return (int)i;
        return -1;
    };
    auto firstContaining=[&](const juce::String& wanted)
    {
        for(size_t i=0;i<names.size();++i)if(names[i].contains(wanted))return (int)i;
        return -1;
    };
    auto setMin=[&](const juce::String& name,int value)
    {
        const int i=indexNamed(name);if(i>=0)lengths[(size_t)i]=juce::jmax(lengths[(size_t)i],value);
    };
    auto setMax=[&](const juce::String& name,int value)
    {
        const int i=indexNamed(name);if(i>=0)lengths[(size_t)i]=juce::jmin(lengths[(size_t)i],value);
    };

    // Seed variation develops a family; it does not replace it with another
    // unrelated order. Every adjustment stays phrase aligned in four-bar units.
    if(plan.structureVariant&1)
    {
        const int i=firstContaining("VERSE");
        if(i>=0)lengths[(size_t)i]+=4;else setMin("CHORUS",12);
    }
    if(plan.structureVariant&2)
    {
        const int i=firstContaining("DROP");if(i>=0)lengths[(size_t)i]+=4;
    }
    if(plan.structureVariant&4)setMin("BREAKDOWN",12);

    const auto p=resolvedPrompt.toLowerCase();
    const bool festival=p.contains("festival")||p.contains("mainstage")||p.contains("big room");
    if(festival)
    {
        setMin("BUILD",8);setMin("CHORUS",8);setMin("DROP",16);
        setMin("BUILD 2",8);setMin("FINAL HOOK",12);
    }
    if((promptIntent.sectionDirections&1u)!=0u)
    {
        const int dropIndex=firstContaining("DROP");
        for(int i=0;i<dropIndex;++i)
        {
            // Compress the lead-in, not the hook statement itself. Shortening a
            // CHORUS to four bars turns its fourth bar into the pre-drop gap and
            // silently changes an otherwise identical melody fingerprint.
            if(names[(size_t)i].contains("CHORUS"))continue;
            lengths[(size_t)i]=juce::jmin(lengths[(size_t)i],4);
        }
    }
    if((promptIntent.sectionDirections&2u)!=0u){setMin("BUILD",12);setMin("BUILD 2",8);}
    if(p.contains("radio edit")||p.contains("short song"))
    {
        setMax("INTRO",4);setMax("VERSE",8);setMax("CHORUS",8);setMax("DROP",12);
        setMax("BREAKDOWN",8);setMax("FINAL HOOK",12);
    }
    if(p.contains("cinematic")){setMin("INTRO",8);setMin("BREAKDOWN",12);}
    if((promptIntent.sectionDirections&4u)!=0u)setMax("INTRO",4);
    if((promptIntent.sectionDirections&64u)!=0u)setMin("CHORUS",12);
    if((promptIntent.sectionDirections&8u)!=0u)setMin("DROP",20);
    if((promptIntent.sectionDirections&16u)!=0u)setMax("BREAKDOWN",4);
    if((promptIntent.sectionDirections&32u)!=0u)setMin("BREAKDOWN",12);

    // Explicit "N bars" sets the planned full-song duration. Round to a musical
    // four-bar grid, then distribute the difference across content sections.
    const int requestedBars=promptIntent.targetBars;
    auto totalBars=[&](){int total=0;for(const int value:lengths)total+=value;return total;};
    if(requestedBars>0)
    {
        static constexpr int priority[11]={9,4,8,1,6,5,3,7,2,0,10};
        int cursor=0;
        while(totalBars()<requestedBars)
        {
            const int i=priority[cursor++%11];
            const bool protectedPreDrop=(promptIntent.sectionDirections&1u)!=0u
                &&(names[(size_t)i]=="INTRO"||names[(size_t)i]=="VERSE"||names[(size_t)i]=="BUILD");
            if(!protectedPreDrop)lengths[(size_t)i]+=4;
        }
        cursor=0;
        while(totalBars()>requestedBars&&cursor<1536)
        {
            const int i=priority[cursor++%11];
            if(lengths[(size_t)i]>4)lengths[(size_t)i]-=4;
        }
    }

    auto energyFor=[&](const juce::String& name)
    {
        float base=.50f;
        if(name.contains("INTRO"))base=.18f;
        else if(name.contains("OUTRO"))base=.16f;
        else if(name.contains("BREAKDOWN"))base=.30f;
        else if(name.contains("VERSE"))base=.40f;
        else if(name.contains("BUILD 2"))base=.84f;
        else if(name.contains("BUILD"))base=.68f;
        else if(name.contains("CHORUS"))base=.80f;
        else if(name.contains("FINAL"))base=1.00f;
        else if(name.contains("DROP 2"))base=1.00f;
        else if(name.contains("DROP"))base=.98f;
        if(name.contains("DROP")&&(promptIntent.sectionDirections&128u)!=0u)base=1.00f;
        if(name.contains("DROP")&&(promptIntent.sectionDirections&256u)!=0u)base=.70f;
        if(name.contains("BREAKDOWN")&&(promptIntent.sectionDirections&512u)!=0u)base=.18f;
        if(name.contains("BREAKDOWN")&&(promptIntent.sectionDirections&1024u)!=0u)base=.65f;
        return juce::jlimit(.08f,1.f,.5f+(base-.5f)*plan.energyContrast+plan.energyBias);
    };

    sections.clear();
    int start=0;
    for(size_t i=0;i<names.size();++i)
    {
        sections.push_back({names[i],start,lengths[i],energyFor(names[i])});
        start+=lengths[i];
    }
    bars=start;
    plan.targetBars=bars;
}

void SongArrangement::buildSectionGoals(uint64_t seed)
{
    sectionGoals.clear();
    sectionGoals.reserve(sections.size());
    const auto prompt=resolvedPrompt.toLowerCase();

    for(size_t i=0;i<sections.size();++i)
    {
        const auto& section=sections[i];
        const auto name=section.name;
        const bool intro=name.contains("INTRO");
        const bool verse=name.contains("VERSE");
        const bool build=name.contains("BUILD");
        const bool chorus=name.contains("CHORUS");
        const bool drop=name.contains("DROP");
        const bool breakdown=name.contains("BREAKDOWN");
        const bool finalHook=name.contains("FINAL")||name.contains("HOOK");

        SectionGoal g;
        g.energy=section.energy;
        g.density=intro?.28f:(verse?.46f:(build?.68f:(chorus?.76f:(drop?.88f:(breakdown?.30f:(finalHook?.92f:.55f))))));
        g.melodyActivity=intro?.28f:(verse?.56f:(build?.62f:(chorus?.88f:(drop?.82f:(breakdown?.38f:(finalHook?.96f:.58f))))));
        g.harmonicTension=intro?.28f:(verse?.42f:(build?.72f:(chorus?.68f:(drop?.78f:(breakdown?.52f:(finalHook?.86f:.48f))))));
        g.bassDrive=intro?.24f:(verse?.56f:(build?.62f:(chorus?.80f:(drop?.96f:(breakdown?.24f:(finalHook?1.00f:.58f))))));
        g.drumDrive=intro?.24f:(verse?.54f:(build?.82f:(chorus?.80f:(drop?1.00f:(breakdown?.22f:(finalHook?1.00f:.58f))))));
        g.space=intro?.78f:(verse?.60f:(build?.42f:(chorus?.36f:(drop?.24f:(breakdown?.94f:(finalHook?.30f:.55f))))));
        g.development=intro?.24f:(verse?.44f:(build?.62f:(chorus?.54f:(drop?.66f:(breakdown?.56f:(finalHook?.96f:.50f))))));

        // Global producer decisions bias every section without flattening the
        // intended contrast curve. Seed jitter is deliberately tiny so two seeds
        // vary production feel while INTRO/DROP/BREAKDOWN roles remain obvious.
        const float jitter=(random01(seed,0x3600+(uint64_t)i*17ULL)-.5f)*.06f;
        g.density=juce::jlimit(.08f,1.f,g.density*.62f+plan.density*.38f+jitter);
        g.melodyActivity=juce::jlimit(.08f,1.f,g.melodyActivity*(.78f+.22f*plan.hookStrength)+jitter*.5f);
        g.harmonicTension=juce::jlimit(.08f,1.f,g.harmonicTension+.18f*(harmonyPlan.tension-.35f)+.12f*(plan.harmonicMotion-.5f));
        g.bassDrive=juce::jlimit(.05f,1.f,g.bassDrive*(.88f+.12f*plan.dropIntensity)+jitter*.35f);
        g.drumDrive=juce::jlimit(.05f,1.f,g.drumDrive*(.78f+.22f*plan.drumDrive)+.10f*(plan.aggression-.5f)+jitter*.35f);
        g.space=juce::jlimit(.05f,1.f,g.space+.28f*(plan.space-.5f)-jitter*.25f);
        g.development=juce::jlimit(.08f,1.f,g.development*.70f+plan.development*.30f+jitter*.5f);

        if(drop||finalHook)
        {
            if(plan.dropCharacter==1){g.melodyActivity=juce::jmin(1.f,g.melodyActivity+.08f);g.harmonicTension=juce::jmin(1.f,g.harmonicTension+.05f);}
            else if(plan.dropCharacter==2){g.density=juce::jmin(1.f,g.density+.07f);g.bassDrive=juce::jmin(1.f,g.bassDrive+.07f);g.drumDrive=juce::jmin(1.f,g.drumDrive+.08f);g.space=juce::jmax(.05f,g.space-.08f);}
            else if(plan.dropCharacter==3){g.melodyActivity=juce::jmin(1.f,g.melodyActivity+.10f);g.space=juce::jmin(1.f,g.space+.08f);g.development=juce::jmin(1.f,g.development+.06f);}
            else if(plan.dropCharacter==4){g.harmonicTension=juce::jmin(1.f,g.harmonicTension+.08f);g.bassDrive=juce::jmin(1.f,g.bassDrive+.10f);g.drumDrive=juce::jmin(1.f,g.drumDrive+.12f);g.space=juce::jmax(.05f,g.space-.12f);}
        }
        if(finalHook)g.development=juce::jmax(g.development,.42f+.58f*plan.finalEvolution);
        if(build)g.harmonicTension=juce::jmin(1.f,g.harmonicTension+.08f*plan.harmonicMotion);

        if(prompt.contains("more space")||prompt.contains("spacious")||prompt.contains("wide"))
            g.space=juce::jmin(1.f,g.space+.10f);
        if(prompt.contains("dry")||prompt.contains("intimate"))
            g.space=juce::jmax(.05f,g.space-.14f);
        if(promptIntent.densityDirection<0)g.density*=.78f;
        else if(promptIntent.densityDirection>0)g.density=juce::jmin(1.f,g.density+.10f);

        // Explicit section energy is authoritative. Use it to reinforce the
        // related goals rather than independently reinterpreting the prompt.
        g.drumDrive=juce::jlimit(.05f,1.f,g.drumDrive*.72f+g.energy*.28f);
        g.bassDrive=juce::jlimit(.05f,1.f,g.bassDrive*.78f+g.energy*.22f);

        sectionGoals.push_back(g);
    }
}

const SectionGoal& SongArrangement::sectionGoalFor(const ArrangementSection* section) const noexcept
{
    static const SectionGoal fallback{};
    if(section==nullptr||sectionGoals.empty())return fallback;
    for(size_t i=0;i<sections.size()&&i<sectionGoals.size();++i)
        if(&sections[i]==section||sections[i].startBar==section->startBar)
            return sectionGoals[i];
    return fallback;
}

bool SongArrangement::sectionFlowsIntoImpact(const ArrangementSection* section) const noexcept
{
    if(section==nullptr)return false;
    for(size_t i=0;i+1<sections.size();++i)
    {
        if(&sections[i]!=section&&sections[i].startBar!=section->startBar)continue;
        const auto next=sections[i+1].name;
        return next.contains("DROP")||next.contains("FINAL")||next.contains("HOOK");
    }
    return false;
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
    resolvedPrompt=ProducerPrompt::parse(prompt).positive;
    promptIntent=parsePromptIntent(prompt,bpm);
    tempo=promptIntent.tempo;
    rootMidi=promptIntent.rootMidi;
    minor=promptIntent.minor;
    bars = defaultBars;
    buildSeedDomains(seed);
    buildSongPlan(domains.structure);
    buildSections(domains.structure);
    buildHarmonyPlan(domains.harmony);
    buildSectionGoals(domains.structure);
    buildHarmonyTimeline(domains.harmony);

    const auto lower = prompt.toLowerCase();
    const bool energetic = lower.contains("energetic") || lower.contains("powerful") || lower.contains("festival") || lower.contains("hard") || lower.contains("edm");
    addDrums(domains.drums, energetic);
    addHarmony(domains.harmony);
    addMelody(domains.melody, energetic);
    alignPitchedLanesToLead();
    addFx(domains.fx);

    for(auto& lane:lanes)
    {
        const auto& ending=sections.back();
        if(ending.name!="OUTRO")continue;
        const double begin=ending.startBar*beatsPerBar,end=getTotalBeats();
        lane.notes.erase(std::remove_if(lane.notes.begin(),lane.notes.end(),[&](const ArrangementNote& n){
            if(n.beat<begin)return false;
            if(n.beat>=end-beatsPerBar)return true;
            if(lane.name=="BASS"||lane.name=="SUB"||lane.name=="KICK")return n.beat>=begin+ending.bars*2.0;
            if(lane.name=="COUNTER"||lane.name=="PLUCK")return true;
            if(lane.drums)return std::fmod(n.beat-begin,1.0)>.05;
            return false;
        }),lane.notes.end());
        for(auto& n:lane.notes)if(n.beat>=begin)
        {
            const float fade=(float)juce::jlimit(.25,1.0,1.0-(n.beat-begin)/(end-begin));
            n.velocity=juce::jlimit(24,110,(int)std::lround(n.velocity*fade));
            n.length=juce::jmin(n.length,end-beatsPerBar-n.beat);
        }
    }

    // Exclusions are semantic hard constraints. Keep lane identities stable for
    // export/UI routing, but publish no forbidden MIDI events.
    for(auto& lane:lanes)
    {
        const bool excluded=(lane.name=="COUNTER"&&(promptIntent.exclusionMask&1u)!=0u)
            ||(lane.name=="PAD"&&(promptIntent.exclusionMask&2u)!=0u)
            ||(lane.name=="PLUCK"&&(promptIntent.exclusionMask&4u)!=0u)
            ||(lane.name=="FX / TRANSITIONS"&&(promptIntent.exclusionMask&8u)!=0u)
            ||(lane.name=="HATS"&&(promptIntent.exclusionMask&16u)!=0u);
        const bool otherExcluded=(lane.name=="BASS"&&(promptIntent.exclusionMask&32u)!=0u)
            ||(lane.name=="SUB"&&(promptIntent.exclusionMask&64u)!=0u)
            ||(lane.name=="CHORDS"&&(promptIntent.exclusionMask&128u)!=0u)
            ||(lane.name=="LEAD"&&(promptIntent.exclusionMask&256u)!=0u)
            ||(lane.name=="KICK"&&(promptIntent.exclusionMask&512u)!=0u)
            ||(lane.name=="SNARE / CLAP"&&(promptIntent.exclusionMask&1024u)!=0u)
            ||(lane.name=="PERCUSSION"&&(promptIntent.exclusionMask&2048u)!=0u);
        if(excluded||otherExcluded)lane.notes.clear();
        // Writers may append fills after a bar's main hits. Publish one canonical
        // event order for the realtime scheduler, MIDI and project round-trip.
        canonicaliseLane(lane,getTotalBeats());
        for(auto& n:lane.notes)n.length=juce::jmin(n.length,getTotalBeats()-n.beat);
    }

    harmonyId=computeHarmonyId();
    melodyId=computeMelodyId();
}

void SongArrangement::regenerateDrumsOnly(const juce::String& drumPrompt, uint64_t seed)
{
    if(lanes.size()<4||sections.empty()||bars<=0)return;

    const auto preservedLanes=lanes;
    const auto originalPrompt=sourcePrompt;
    const auto originalResolved=resolvedPrompt;
    const auto originalPlan=plan;
    const auto originalDomains=domains;

    // Keep the current song architecture/harmony/melody intact. Only the drum
    // generation domains and drum-specific plan fields are refreshed.
    const auto extra=drumPrompt.trim();
    sourcePrompt=originalPrompt+(extra.isNotEmpty()?juce::String(", ")+extra:juce::String());
    resolvedPrompt=ProducerPrompt::parse(sourcePrompt).positive;
    domains.drums=mix64(seed^0x4452554d5f4e4557ULL);
    domains.soundPalette=mix64(seed^0x4452554d5f534e44ULL);
    plan.drumGroove=(int)(random01(domains.drums,0x1002)*6.f)%6;
    plan.hatMode=(int)(random01(domains.drums,0x1003)*4.f)%4;

    const auto p=resolvedPrompt.toLowerCase();
    const bool energetic=p.contains("energetic")||p.contains("powerful")
        ||p.contains("festival")||p.contains("hard")||p.contains("edm");

    lanes.clear();
    addDrums(domains.drums,energetic);

    // Defensive fallback: never damage an existing arrangement if drum
    // generation fails to produce the four expected drum lanes.
    if(lanes.size()<4)
    {
        lanes=preservedLanes;
        sourcePrompt=originalPrompt;
        resolvedPrompt=originalResolved;
        plan=originalPlan;
        domains=originalDomains;
        return;
    }

    for(size_t i=4;i<preservedLanes.size();++i)
        lanes.push_back(preservedLanes[i]);

    // Generate a genuinely new drum kit while keeping every non-drum SoundDNA
    // patch exactly as it was before the drum-only operation.
    finalizeSoundPalette();
    for(size_t i=4;i<lanes.size()&&i<preservedLanes.size();++i)
        lanes[i].sound=preservedLanes[i].sound;

    const int newGroove=plan.drumGroove;
    const int newHatMode=plan.hatMode;
    sourcePrompt=originalPrompt;
    resolvedPrompt=originalResolved;
    plan=originalPlan;
    plan.drumGroove=newGroove;
    plan.hatMode=newHatMode;
    domains=originalDomains;
}


void SongArrangement::finalizeSoundPalette()
{
    if(lanes.empty())return;
    PromptGenerator designer;
    const auto productionPrompt=resolvedPrompt.toLowerCase();
    const bool festival=productionPrompt.contains("festival")||productionPrompt.contains("mainstage")||productionPrompt.contains("big room");
    const bool progressive=productionPrompt.contains("progressive house")||productionPrompt.contains("melodic house");
    const bool tech=productionPrompt.contains("tech house")||productionPrompt.contains("minimal house");
    const bool dnb=productionPrompt.contains("drum and bass")||productionPrompt.contains("dnb");
    const bool trance=productionPrompt.contains("trance");
    const bool cinematic=productionPrompt.contains("cinematic")||productionPrompt.contains("film");
    const bool tropical=productionPrompt.contains("tropical");

    // v3.2: choose one song-level timbral world before designing individual
    // instruments. Previously every lane independently drew a flavour adjective,
    // so a song could combine unrelated bright/dark/airy/thick patches by chance.
    // These axes remain seed-variable, but explicit prompt intent and the shared
    // producer plan constrain them into one coherent palette.
    palettePlan.character=(int)(random01(domains.soundPalette,0x3200)*4.f)%4;
    const float seededBrightness=.30f+.40f*random01(domains.soundPalette,0x3201);
    const float seededMovement=.24f+.48f*random01(domains.soundPalette,0x3202);
    const float seededSpace=.24f+.48f*random01(domains.soundPalette,0x3203);
    const float seededImpact=.30f+.45f*random01(domains.soundPalette,0x3204);
    palettePlan.brightness=juce::jlimit(0.f,1.f,seededBrightness*.35f+plan.brightness*.65f);
    const float feelMovement=plan.rhythmicFeel==2?.72f:(plan.rhythmicFeel==1?.62f:.44f);
    palettePlan.movement=juce::jlimit(0.f,1.f,seededMovement*.58f+feelMovement*.42f);
    palettePlan.space=juce::jlimit(0.f,1.f,seededSpace*.35f+plan.space*.65f);
    palettePlan.impact=juce::jlimit(0.f,1.f,seededImpact*.38f+plan.aggression*.62f);
    if(plan.emotionProfile==2)palettePlan.brightness=juce::jmax(.68f,palettePlan.brightness);
    if(plan.emotionProfile==3)palettePlan.brightness=juce::jmin(.36f,palettePlan.brightness);
    if(plan.emotionProfile==4)palettePlan.space=juce::jmax(.72f,palettePlan.space);
    if(plan.emotionProfile==5)palettePlan.impact=juce::jmax(.78f,palettePlan.impact);
    if(productionPrompt.contains("bright")||productionPrompt.contains("airy"))palettePlan.brightness=juce::jmax(.78f,palettePlan.brightness);
    if(productionPrompt.contains("dark")||productionPrompt.contains("warm"))palettePlan.brightness=juce::jmin(.38f,palettePlan.brightness);
    if(productionPrompt.contains("dry")||productionPrompt.contains("intimate"))palettePlan.space=juce::jmin(.24f,palettePlan.space);
    if(productionPrompt.contains("spacious")||productionPrompt.contains("wide"))palettePlan.space=juce::jmax(.76f,palettePlan.space);
    if(productionPrompt.contains("aggressive")||productionPrompt.contains("hard"))palettePlan.impact=juce::jmax(.80f,palettePlan.impact);
    static constexpr const char* paletteWords[4]={"vintage analog","clean digital","organic natural","hybrid modern"};

    for (size_t i = 0; i < lanes.size(); ++i)
    {
        auto& lane = lanes[i];
        juce::String soundPrompt = resolvedPrompt + " " + lane.name + " ";
        // Drum synthesis has its own material vocabulary; applying e.g. the
        // generic "digital" family after the kick rule could relabel and reshape
        // a kick as a melodic digital patch. Drums share palette impact/macros,
        // while tonal lanes also inherit the oscillator-material character.
        if(!lane.drums)soundPrompt += juce::String(paletteWords[palettePlan.character])+" ";

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
            if(plan.chordTexture==0)soundPrompt += "warm sustained chords soft attack musical harmony ";
            else if(plan.chordTexture==1)soundPrompt += "rhythmic pulsing chords controlled attack ";
            else if(plan.chordTexture==2)soundPrompt += "wide bright supersaw anthem chords controlled ";
            else soundPrompt += "short dry house chord stabs tight transient ";
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
            // The transition lane is the sustained/swept layer. Arrival impacts
            // live in PERCUSSION, where note 57 has a dedicated rendered impact.
            // Asking one SoundDNA patch for both "riser" and "impact" made the
            // later impact rule overwrite the riser envelope/pitch behaviour.
            soundPrompt += festival?"festival riser uplifter transition airy noise sweep reverse ":
                           "riser uplifter transition atmospheric noise sweep reverse ";
        else soundPrompt += "clean atmospheric texture ";
        if(plan.emotionProfile==1)soundPrompt += "warm emotional expressive hopeful ";
        else if(plan.emotionProfile==2)soundPrompt += "uplifting bright open ";
        else if(plan.emotionProfile==3)soundPrompt += "dark restrained moody ";
        else if(plan.emotionProfile==4)soundPrompt += "dreamy soft spacious ";
        else if(plan.emotionProfile==5)soundPrompt += "aggressive hard driven ";
        if(lane.drums)
            soundPrompt += plan.drumDrive>1.05f?"powerful hard transient ":
                           (plan.drumDrive<.80f?"soft restrained transient ":"controlled transient ");
        const float flavour = random01(domains.soundPalette, 0x9000ULL + static_cast<uint64_t>(i) * 0x9e37ULL);
        soundPrompt += flavour < .25f ? "warm soft spacious long release" :
                       flavour < .50f ? "bright crisp wide fast attack" :
                       flavour < .75f ? "dark thick body subtle modulation" :
                                        "airy animated wide heavy modulation";
        lane.sound = designer.generate(soundPrompt, mix64(domains.soundPalette ^ mix64((static_cast<uint64_t>(i)+1ULL) * 0x517cc1b727220a95ULL)));

        // Publish the common palette axes in every SoundDNA. Small deterministic
        // role offsets keep instruments distinguishable without dissolving the
        // shared production identity.
        const float roleOffset=(random01(domains.soundPalette,0xa200ULL+i*17ULL)-.5f)*.16f;
        lane.sound.macroBrightness=juce::jlimit(0.f,1.f,palettePlan.brightness+roleOffset);
        lane.sound.macroMovement=juce::jlimit(0.f,1.f,palettePlan.movement-roleOffset*.35f);
        lane.sound.macroSpace=juce::jlimit(0.f,1.f,palettePlan.space+roleOffset*.45f);
        lane.sound.macroImpact=juce::jlimit(0.f,1.f,palettePlan.impact-roleOffset*.25f);
        lane.sound.cutoff=juce::jlimit(80.f,19000.f,lane.sound.cutoff*(.72f+.56f*lane.sound.macroBrightness));
        lane.sound.drive=juce::jlimit(0.f,1.f,lane.sound.drive*(.72f+.62f*lane.sound.macroImpact));
        lane.sound.lfoRate=juce::jlimit(.02f,8.f,lane.sound.lfoRate*(.72f+.60f*lane.sound.macroMovement));
        lane.sound.lfoCutoff=juce::jlimit(-1.f,1.f,lane.sound.lfoCutoff*(.55f+.90f*lane.sound.macroMovement));
        lane.sound.width=juce::jlimit(0.f,1.f,lane.sound.width*(.76f+.40f*lane.sound.macroSpace));
        lane.sound.chorus=juce::jlimit(0.f,1.f,lane.sound.chorus*(.68f+.62f*lane.sound.macroSpace));
        lane.sound.reverb=juce::jlimit(0.f,1.f,lane.sound.reverb*(.62f+.74f*lane.sound.macroSpace));
        lane.sound.delay=juce::jlimit(0.f,1.f,lane.sound.delay*(.62f+.74f*lane.sound.macroSpace));

        // Seed selects an audible instrument identity within the producer's
        // material family, rather than only changing a label/phase seed.
        if(!lane.drums&&lane.name!="SUB"&&lane.name!="FX / TRANSITIONS")
        {
            const int material=(int)(random01(domains.soundPalette,0xb100+i*31)*4.f)%4;
            const float envelope=random01(domains.soundPalette,0xb101+i*31);
            const float colour=random01(domains.soundPalette,0xb102+i*31);
            const bool harmonicBody=lane.name=="BASS"||lane.name=="CHORDS"||lane.name=="LEAD";
            lane.sound.oscA=harmonicBody
                ?(material==0?WaveShape::softSaw:(material==1?WaveShape::saw:(material==2?WaveShape::square:WaveShape::triangle)))
                :(material==0?WaveShape::triangle:(material==1?WaveShape::sine:(material==2?WaveShape::softSaw:WaveShape::square)));
            lane.sound.oscB=material%2==0?WaveShape::triangle:WaveShape::softSaw;
            lane.sound.oscMix=.12f+.30f*colour;
            lane.sound.oscBTranspose=material==2&&lane.name!="BASS"?12.f:0.f;
            lane.sound.oscAMorph=.05f+.24f*colour;
            lane.sound.oscBMorph=.05f+.18f*(1.f-colour);
            lane.sound.fmAmount=palettePlan.character==1||palettePlan.character==3?.025f+.10f*colour:0.f;
            lane.sound.fmRatio=material%2==0?2.f:3.f;
            lane.sound.ringMod=palettePlan.character==3?.035f*colour:0.f;
            lane.sound.detune=.025f+.085f*colour;
            lane.sound.phaseRandom=.15f+.65f*envelope;
            lane.sound.decay*=.65f+.70f*envelope;
            lane.sound.attack*=.75f+.50f*(1.f-envelope);
            lane.sound.cutoff=juce::jlimit(100.f,17500.f,lane.sound.cutoff*(.65f+.55f*colour));
            lane.sound.resonance=juce::jlimit(.05f,.48f,.08f+.26f*colour+.10f*plan.aggression);
            lane.sound.drive=juce::jlimit(0.f,.42f,.025f+.26f*plan.aggression+.08f*colour);
            lane.sound.lfoShape=static_cast<LfoShape>(material%3);
            lane.sound.lfoCutoff*=.60f+.65f*envelope;
            // Explicit direction wins over seeded flavour words and genre defaults.
            if(promptIntent.brightnessDirection<0)lane.sound.cutoff=juce::jmin(4800.f,lane.sound.cutoff);
            if(promptIntent.brightnessDirection>0)lane.sound.cutoff=juce::jmax(7800.f,lane.sound.cutoff);
            if(promptIntent.spaceDirection<0){lane.sound.reverb*=.20f;lane.sound.delay*=.20f;lane.sound.chorus*=.35f;}
            if(promptIntent.aggressionDirection<0){lane.sound.drive=juce::jmin(.10f,lane.sound.drive);lane.sound.fmAmount*=.30f;}
        }

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
            lane.sound.oscMix=0.f; lane.sound.unison=1; lane.sound.detune=0.f;
            lane.sound.oscAMorph=lane.sound.oscBMorph=lane.sound.fmAmount=lane.sound.ringMod=0.f;
            lane.sound.oscBTranspose=lane.sound.pitchEnv=lane.sound.transientLevel=lane.sound.bitCrush=lane.sound.downsample=0.f;
            lane.sound.phaseRandom=0.f;
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

        // Stereo/space remains role-safe even when the song palette is lush.
        if(lane.name=="SUB"||lane.name=="KICK")
        {
            lane.sound.macroSpace=0.f;lane.sound.width=0.f;
            lane.sound.chorus=0.f;lane.sound.reverb=0.f;lane.sound.delay=0.f;
        }
        else if(lane.name=="BASS")lane.sound.macroSpace=juce::jmin(.12f,lane.sound.macroSpace);

        const auto semanticName=lane.sound.name.replace("Generated ","").trim();
        lane.sound.name=lane.name+" • "+(semanticName.isEmpty()?juce::String("Custom"):semanticName);
    }
}

juce::String SongArrangement::getSoundPaletteSummary() const
{
    static constexpr const char* names[4]={"ANALOG","DIGITAL","ORGANIC","HYBRID"};
    return "CHARACTER: "+juce::String(names[juce::jlimit(0,3,palettePlan.character)])
        +" | BRIGHTNESS: "+juce::String((int)std::lround(palettePlan.brightness*100.f))
        +" | MOVEMENT: "+juce::String((int)std::lround(palettePlan.movement*100.f))
        +" | SPACE: "+juce::String((int)std::lround(palettePlan.space*100.f))
        +" | IMPACT: "+juce::String((int)std::lround(palettePlan.impact*100.f));
}

std::vector<int> SongArrangement::getSoundPaletteFingerprint() const
{
    std::vector<int> fingerprint;
    fingerprint.reserve(5+lanes.size()*12);
    fingerprint.push_back(palettePlan.character);
    for(const float value:{palettePlan.brightness,palettePlan.movement,palettePlan.space,palettePlan.impact})
        fingerprint.push_back((int)std::lround(value*1000.f));
    for(const auto& lane:lanes)
    {
        const auto& d=lane.sound;
        fingerprint.insert(fingerprint.end(),{
            (int)d.oscA,(int)d.oscB,(int)std::lround(d.oscMix*100.f),d.unison,
            (int)d.filterMode,(int)std::lround(d.cutoff/40.f),(int)std::lround(d.drive*200.f),
            (int)std::lround(d.width*100.f),(int)std::lround(d.fmAmount*100.f),
            (int)std::lround(d.macroBrightness*100.f),(int)std::lround(d.macroSpace*100.f),
            (int)std::lround(d.macroImpact*100.f)});
    }
    return fingerprint;
}

void SongArrangement::addDrums(uint64_t seed, bool energetic)
{
    ArrangementLane kick{"KICK",10,true}, snare{"SNARE / CLAP",10,true}, hats{"HATS",10,true}, perc{"PERCUSSION",10,true};
    const auto p = resolvedPrompt.toLowerCase();
    const bool trance = p.contains("trance");
    const bool house = p.contains("house") || p.contains("future rave") || p.contains("edm") || trance;
    const bool tech = p.contains("tech house") || p.contains("minimal house");
    const bool trap = p.contains("trap") || p.contains("hip hop");
    const bool dnb = p.contains("drum and bass") || p.contains("dnb");
    const bool festival = p.contains("festival") || p.contains("mainstage") || p.contains("big room");
    int groove = plan.drumGroove;
    int hatMode = plan.hatMode;
    if(p.contains("four on the floor")||p.contains("four-on-the-floor"))groove=0;
    if(p.contains("half time")||p.contains("halftime"))groove=1;
    if(p.contains("open hats"))hatMode=2;
    if(p.contains("closed hats"))hatMode=0;
    const float swing = plan.rhythmicFeel==0 ? 0.f :
        (plan.rhythmicFeel==2 ? (.055f+random01(seed,13)*.035f)
                              : (.014f+random01(seed,13)*.036f));

    for(int bar=0;bar<bars;++bar)
    {
        const ArrangementSection* section=nullptr;
        for(const auto& s:sections) if(sectionContains(s,bar)){section=&s;break;}
        const float energy=section?section->energy:.4f;
        const auto& goal=sectionGoalFor(section);
        const juce::String sectionName=section?section->name:juce::String();
        const bool intro=sectionName.contains("INTRO");
        const bool build=sectionName.contains("BUILD");
        const bool breakdown=sectionName.contains("BREAKDOWN");
        const bool drop=sectionName.contains("DROP") || sectionName.contains("HOOK");
        const bool chorus=sectionName.contains("CHORUS");
        const bool sectionStart=section && bar==section->startBar;
        const bool sectionEnd=section && bar==section->startBar+section->bars-1;
        const int localBar=section?bar-section->startBar:0;
        const bool fourBarBoundary=!sectionEnd&&((localBar+1)%4==0);
        const bool eightBarBoundary=fourBarBoundary&&((localBar+1)%8==0);
        const bool preDropGap=sectionEnd&&sectionFlowsIntoImpact(section)&&(house||festival);
        const double b=bar*beatsPerBar;
        const uint64_t bs=(uint64_t)bar*97ULL;

        const int producerDrive=(int)std::llround((plan.drumDrive-.90f)*12.f
            +(goal.drumDrive-.55f)*18.f
            +(drop?(plan.dropIntensity-.90f)*14.f:0.f));
        const int energyVelocity=(int)std::llround((energy-.5f)*15.f
            +(goal.density-.5f)*8.f)+producerDrive;
        if(!breakdown && (!intro || bar-section->startBar>=juce::jmax(1,section->bars/2)))
        {
            if(dnb)
            {
                addNote(kick,36,b,.09,drop?118:106);
                addNote(kick,36,b+(groove%2==0?2.5:2.75),.08,drop?108:101);
                if((drop||chorus)&&localBar%2==0)
                    addNote(kick,36,b+(groove%2==0?1.75:1.5),.07,92);
                if(drop&&localBar%4==3)addNote(kick,36,b+3.25,.07,96);
            }
            else if(house)
            {
                for(int q=0;q<4;++q)
                    if((!intro || q==0 || q==2 || bar-section->startBar>=section->bars-2)
                       && !(preDropGap&&q==3))
                        addNote(kick,36,b+q,.11,
                                juce::jlimit(48,127,
                                    (drop?123:(chorus?116:(build?111:105+(q==0?5:0))))+energyVelocity));
                if(!preDropGap&&(drop||build||chorus) && ((bar+groove)%4==3))
                    addNote(kick,36,b+3.5,.08,88+(int)(random01(seed,1020+bs)*20.f));
                if(drop && !tech && groove%3==2 && bar%2==0)
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
            if(dnb)
            {
                const int backbeat=drop?119:(chorus?113:(build?108:102));
                addNote(snare,38,b+1.0,.11,backbeat);
                addNote(snare,38,b+3.0,.11,juce::jmin(127,backbeat+2));
                if((drop||chorus)&&localBar%2==1)
                    addNote(snare,39,b+2.75,.06,70+(int)(random01(seed,1200+bs)*14.f));
            }
            else if(trap)
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

        int hatSteps=4;
        if(dnb)hatSteps=(breakdown?4:16);
        else if(breakdown)hatSteps=4;
        else if(build)hatSteps=localBar>=juce::jmax(1,section->bars/2)?16:8;
        else if(drop)hatSteps=(trance||festival||hatMode>=2)?16:8;
        else if(chorus)hatSteps=8;
        else if(intro)hatSteps=localBar>=juce::jmax(1,section->bars-2)?8:4;
        else if(energy>=.58f||energetic)hatSteps=8;
        // A softer curve may simplify a generic drop, but genre-defining trance
        // and festival 16th motion is a hard style constraint, not optional fill.
        if(drop&&energy<.86f&&!trance&&!festival)hatSteps=juce::jmin(hatSteps,8);
        if(goal.density>.78f&&!intro&&!breakdown)hatSteps=juce::jmax(hatSteps,8);
        if(goal.density<.34f&&!drop&&!chorus)hatSteps=juce::jmin(hatSteps,4);
        // Explicit closed-hat direction is also a hard groove constraint.  A
        // high energy target should make that eight-step house pattern hit
        // harder, not silently turn it into a denser twelve-step rhythm.
        if(energy>=.88f&&!intro&&!breakdown&&!(house&&hatMode==0))
            hatSteps=juce::jmax(hatSteps,12);
        for(int h=0;h<hatSteps;++h)
        {
            const int quarter=juce::jmax(1,hatSteps/4);
            const bool strong=(h%quarter==0);
            float skip=breakdown?.58f:(intro?.28f:(drop?.05f:(chorus?.09f:(build?.12f:.18f))));
            skip=juce::jlimit(.02f,.76f,skip+(.5f-energy)*.30f
                +(.5f-goal.density)*.24f-(goal.drumDrive-.55f)*.18f);
            // Sixteenth-note trance/festival hats are the groove skeleton, not
            // optional ornamentation.  Energy may still shape their velocity
            // and timbre, but stochastic thinning can otherwise erase five or
            // more hits and collapse the style back into an eight-note groove.
            if(drop&&(trance||festival))skip=0.f;
            if(!strong && random01(seed,1300+bs+h)<skip) continue;
            double beat=b+h*(4.0/hatSteps);
            if(h%2==1) beat+=swing*(hatMode==1?1.0:.55);
            if(preDropGap&&beat>=b+3.5)continue;
            bool open=(drop||chorus) && ((h+groove)%8==3 || (hatMode>=2 && h%8==7));
            if(house&&hatSteps==8)open=(h%2==1)&&((h+bar+groove)%4==1);
            const int sectionLift=drop?10:(chorus?6:(build?4:(breakdown?-8:0)));
            const int vel=juce::jlimit(30,118,44+sectionLift+energyVelocity
                +(strong?15:0)+(int)(random01(seed,1400+bs+h)*28.f));
            addNote(hats,open?46:42,beat,.045+(open?.10:0.0),vel);
        }

        if(sectionStart&&(drop||chorus))
        {
            addNote(perc,49,b,.18,drop?112:98); // crash
            // A DROP needs a real rendered impact, not merely a low note played
            // through the airy FX patch. FINAL HOOK keeps the strongest accent.
            if(drop)
                addNote(perc,57,b,.22,
                        (sectionName.contains("FINAL")||sectionName.contains("HOOK"))?121:114);
        }

        if((drop||chorus)&&energy>=.60f)
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

        // Musical phrase fills are tied to 4/8-bar boundaries. They stay short,
        // avoid the protected pre-drop gap, and never appear in the middle of a
        // breakdown where space is the production goal.
        if(fourBarBoundary&&!breakdown&&!preDropGap)
        {
            if(eightBarBoundary)addNote(perc,45,b+3.00,.07,76+(drop?10:0));
            addNote(perc,47,b+3.50,.06,82+(drop?9:0));
            addNote(perc,50,b+3.75,.05,90+(drop?10:0));
        }

        // BUILD -> CHORUS gets a restrained lift. The true festival-style drum
        // tension belongs at CHORUS -> DROP, where the listener expects the payoff.
        if(build && sectionEnd && !preDropGap)
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

        if(breakdown&&sectionEnd)
        {
            // The breakdown exits with a compact rising tom pickup into BUILD 2;
            // previously this boundary was the only major arrival with no fill.
            addNote(perc,45,b+2.75,.075,76);
            addNote(perc,47,b+3.25,.065,88);
            addNote(perc,50,b+3.625,.055,101);
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

    const auto productionPrompt=resolvedPrompt.toLowerCase();
    const bool mainstreamSong=productionPrompt.contains("progressive house")
        ||productionPrompt.contains("melodic house")||productionPrompt.contains("edm")
        ||productionPrompt.contains("pop")||productionPrompt.contains("trance")
        ||productionPrompt.contains("festival")||productionPrompt.contains("mainstage")
        ||productionPrompt.contains("future rave");
    const bool houseBass=productionPrompt.contains("house")||productionPrompt.contains("trance")
        ||productionPrompt.contains("future rave")||productionPrompt.contains("festival")
        ||productionPrompt.contains("mainstage")||productionPrompt.contains("edm");
    const bool techBass=productionPrompt.contains("tech house")||productionPrompt.contains("minimal house");
    const bool dnbBass=productionPrompt.contains("drum and bass")||productionPrompt.contains("dnb");
    const bool sparseLowEnd=productionPrompt.contains("more space")
        ||productionPrompt.contains("less busy")||productionPrompt.contains("sparse bass");

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

    auto foldNear=[&](int note,int low,int high,int anchor)
    {
        while(note<low)note+=12;
        while(note>high)note-=12;
        int best=note;
        if(note+12<=high&&std::abs((note+12)-anchor)<std::abs(best-anchor))best=note+12;
        if(note-12>=low&&std::abs((note-12)-anchor)<std::abs(best-anchor))best=note-12;
        return juce::jlimit(low,high,best);
    };

    auto closeSongChord=[&](const HarmonyEvent& event)
    {
        const int d=juce::jlimit(0,6,event.scaleDegree);
        int root=rootMidi+scaleSemitoneForDegree(d);
        while(root<48)root+=12;
        while(root>60)root-=12;

        int third=root+(scaleSemitoneForDegree(d+2)-scaleSemitoneForDegree(d));
        int fifth=root+(scaleSemitoneForDegree(d+4)-scaleSemitoneForDegree(d));
        int colour=root+(scaleSemitoneForDegree(d+6)-scaleSemitoneForDegree(d));
        if(event.borrowed&&minor&&d==4)third=root+4;
        else if(event.borrowed&&!minor&&d==3)third=root+3;
        if(event.extension==2)colour=root+14;
        else if(event.extension==3)colour=root+2;
        else if(event.extension==4)third=root+5;

        std::array<int,4> out{root,third,fifth,colour};
        for(int i=0;i<juce::jlimit(0,2,event.inversion);++i)out[(size_t)i]+=12;
        std::sort(out.begin(),out.end());
        for(size_t i=1;i<out.size();++i)
            while(out[i]<=out[i-1])out[i]+=12;

        while(out.back()>76)for(auto& n:out)n-=12;
        while(out.front()<45)for(auto& n:out)n+=12;
        return out;
    };

    // Mainstream support harmony is intentionally boring in the best way:
    // one sustained triad per bar, plus slow upper pad harmony. These lanes
    // support the song instead of generating extra melodic information.
    if(mainstreamSong)
    {
        for(int bar=0;bar<bars;++bar)
        {
            const double beat=bar*beatsPerBar;
            const auto* h=harmonyAtBeat(beat);
            const auto [section,sectionIndex]=sectionAtBeat(beat);
            if(h==nullptr||section==nullptr)continue;

            const bool intro=section->name.contains("INTRO");
            const bool verse=section->name.contains("VERSE");
            const bool breakdown=section->name.contains("BREAKDOWN");
            const bool chorus=section->name.contains("CHORUS");
            const bool drop=section->name.contains("DROP")||section->name.contains("FINAL")||section->name.contains("HOOK");
            const bool preDropGap=bar==section->startBar+section->bars-1
                &&sectionFlowsIntoImpact(section);
            const auto& goal=sectionGoalFor(section);
            const auto tones=closeSongChord(*h);

            const double chordLen=preDropGap?2.95:3.82;
            const int baseVel=juce::jlimit(48,108,56+(int)(section->energy*22.f)
                +(int)(goal.harmonicTension*8.f));
            // Chord density changes by attack rate, never by deleting one voice
            // from a triad. The latter creates an isolated/non-chord-looking MIDI
            // event and breaks the lane's harmonic role in the piano roll.
            const bool chordAttack=section->energy>=.38f||bar%2==0;
            if(chordAttack)
            {
                addNote(chords,tones[0],beat,chordLen,baseVel);
                addNote(chords,tones[1],beat,chordLen,juce::jmax(42,baseVel-4));
                addNote(chords,tones[2],beat,chordLen,juce::jmax(40,baseVel-6));

                // Extensions are opt-in in mainstream song mode; default CHORDS
                // is a visible triad, not unrelated notes across octaves.
                const bool wantsColour=productionPrompt.contains("7th chord")
                    ||productionPrompt.contains("extended chord")||productionPrompt.contains("jazz chord");
                if(wantsColour&&h->extension>0)
                    addNote(chords,foldNear(tones[3],55,76,tones[2]+3),beat,chordLen,
                            juce::jmax(36,baseVel-11));
            }

            const bool plannedPad=plan.padPolicy==0
                ?(intro||verse||breakdown||(!drop&&section->energy<.48f))
                :(plan.padPolicy==1?breakdown:
                  (plan.padPolicy==2?(intro||verse||breakdown||(!drop&&section->energy<.62f)):false));
            if(plannedPad&&goal.space>.32f)
            {
                // PAD is two slow upper voices, same harmony, same bar.
                const int padA=foldNear(tones[1],57,74,64);
                const int padB=foldNear(tones[2],60,79,padA+4);
                const double padLen=preDropGap?2.95:3.90;
                addNote(pad,padA,beat,padLen,42+(int)(section->energy*8.f));
                addNote(pad,padB,beat,padLen,39+(int)(section->energy*7.f));
            }
        }
    }

    // Non-mainstream/experimental modes retain the richer Harmony DNA voicing
    // grammar. Mainstream song mode never enters this event-dense path.
    int lastMainstreamChordBar=-1,mainstreamChordAttacksThisBar=0;
    int lastMainstreamPadBar=-1,mainstreamPadAttacksThisBar=0;
    if(!mainstreamSong)
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

        auto tones=mainstreamSong?closeSongChord(event):chordTonesFor(event);
        if(!mainstreamSong)for(auto& n:tones)n+=12;

        const int eventBar=juce::jlimit(0,bars-1,(int)std::floor(event.beat/beatsPerBar));
        if(eventBar!=lastMainstreamChordBar){lastMainstreamChordBar=eventBar;mainstreamChordAttacksThisBar=0;}
        if(eventBar!=lastMainstreamPadBar){lastMainstreamPadBar=eventBar;mainstreamPadAttacksThisBar=0;}
        const bool allowMainstreamChord=!mainstreamSong||mainstreamChordAttacksThisBar<2;
        const bool allowMainstreamPad=!mainstreamSong||mainstreamPadAttacksThisBar<2;

        int repeats=1;
        if(drop||chorus||finalHook)
            repeats=(chordMode==0?1:(chordMode==1?2:(chordMode==2?4:2)));
        else if(build)
            repeats=chordMode>=3?2:1;
        if(energy<.38f)repeats=1;
        else if(energy>.86f&&chordMode>=2)repeats=juce::jmax(repeats,2);
        if(mainstreamSong)
        {
            // One harmonic event = one visible chord block. Rhythm belongs to
            // drums/pluck/bass, not dozens of re-triggered chord notes.
            repeats=1;
        }

        const double unit=event.length/(double)repeats;
        if(allowMainstreamChord)
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

            if(event.extension>0&&(mainstreamSong
               ||random01(domains.voicing,0x5300+rs)<(.52f+harmonyPlan.extensionProbability)))
                addNote(chords,tones[3],startBeat,juce::jmin(len*.92,1.6),juce::jmax(38,baseVel-9));

            if(!mainstreamSong&&finalHook&&r%2==0&&random01(domains.voicing,0x5400+rs)<.58f)
                addNote(chords,tones[2]+12,startBeat,juce::jmin(len*.78,1.1),juce::jmax(36,baseVel-13));
        }
        if(mainstreamSong&&allowMainstreamChord)++mainstreamChordAttacksThisBar;

        // PAD is slow upper harmony only. Mainstream mode never randomizes the
        // octave/register or adds an unrelated root doubling.
        if(intro||verse||breakdown)
        {
            const float padChance=breakdown?.94f:(intro?.82f:.68f);
            if(allowMainstreamPad&&(mainstreamSong||random01(domains.pad,0x6100+hi)<padChance))
            {
                const double padStart=mainstreamSong?event.beat:
                    event.beat+(random01(domains.pad,0x6110+hi)<.22f?.25:0.0);
                const double padLen=juce::jmin(event.length*1.18,8.0);
                const int padA=mainstreamSong?foldNear(tones[1],55,78,64):tones[1];
                const int padB=mainstreamSong?foldNear(tones[2],55,79,padA+4):tones[2];

                addNote(pad,padA,padStart,padLen,42+(int)(random01(domains.pad,0x6130+hi)*10.f));
                addNote(pad,padB,padStart,padLen,40+(int)(random01(domains.pad,0x6140+hi)*9.f));

                if(event.extension>0)
                {
                    const int padC=mainstreamSong?foldNear(tones[3],57,81,padB+3):tones[3];
                    addNote(pad,padC,padStart,padLen*.94,37+(int)(random01(domains.pad,0x6160+hi)*9.f));
                }
                else if(!mainstreamSong&&random01(domains.pad,0x6150+hi)<harmonyPlan.extensionProbability)
                    addNote(pad,tones[3],padStart,padLen*.94,37+(int)(random01(domains.pad,0x6160+hi)*9.f));

                if(!mainstreamSong&&random01(domains.pad,0x6170+hi)<.34f)
                    addNote(pad,tones[0]+12,padStart,padLen*.82,36+(int)(random01(domains.pad,0x6180+hi)*8.f));
                if(mainstreamSong)++mainstreamPadAttacksThisBar;
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
        const auto& goal=sectionGoalFor(section);
        const uint64_t bs=(uint64_t)bar*173ULL;
        const int supportTheme=(chorus||drop||finalHook)?3:sectionIndex;
        const int supportMotifBar=localBar%2;
        const uint64_t bassMotifSeed=mix64(domains.bass
            ^ ((uint64_t)supportTheme+1ULL)*0x9e3779b97f4a7c15ULL
            ^ ((uint64_t)supportMotifBar+1ULL)*0xbf58476d1ce4e5b9ULL);
        const uint64_t pluckMotifSeed=mix64(domains.pluck
            ^ ((uint64_t)supportTheme+1ULL)*0xd1342543de82ef95ULL
            ^ ((uint64_t)supportMotifBar+1ULL)*0xa24baed4963ee407ULL);
        const bool preDropGap=localBar==section->bars-1&&sectionFlowsIntoImpact(section)
            && (sourcePrompt.containsIgnoreCase("house")||sourcePrompt.containsIgnoreCase("edm")
                ||sourcePrompt.containsIgnoreCase("festival")||sourcePrompt.containsIgnoreCase("mainstage"));

        if(!intro||localBar>=juce::jmax(1,section->bars/2))
        {
            static constexpr double sustained[1]={0.16};
            static constexpr double offbeat[4]={0.50,1.50,2.50,3.50};
            static constexpr double octavePickup[4]={0.16,1.50,2.50,3.50};
            static constexpr double rolling[6]={0.16,0.75,1.50,2.16,2.75,3.50};
            static constexpr double syncopated[5]={0.16,0.75,1.75,2.50,3.25};

            const double* positions=sustained;
            int patternSize=1;
            if(bassMode==1){positions=offbeat;patternSize=4;}
            else if(bassMode==2){positions=octavePickup;patternSize=4;}
            else if(bassMode==3){positions=rolling;patternSize=6;}
            else if(bassMode==4){positions=syncopated;patternSize=5;}

            int count=patternSize;
            if(verse)count=juce::jmin(count,bassMode==0?1:(bassMode==3?4:3));
            if(build&&localBar<section->bars/2)count=juce::jmin(count,2);
            if(breakdown)count=(localBar%2==0)?juce::jmin(count,1):0;
            if(intro)count=juce::jmin(count,2);
            if(goal.bassDrive<.38f)count=juce::jmin(count,1);
            else if(goal.bassDrive>.86f&&drop&&bassMode!=0)
                count=juce::jmax(count,juce::jmin(patternSize,4));
            if(energy<.34f)count=juce::jmin(count,1);
            else if(energy<.52f)count=juce::jmin(count,juce::jmax(1,patternSize-2));

            // Explicit sparse-low-end direction remains authoritative after all
            // section-energy/drop boosts. A powerful drop may hit harder, but it
            // must not turn a requested spacious support bass into a busy riff.
            if(sparseLowEnd)count=juce::jmin(count,2);

            for(int i=0;i<count;++i)
            {
                const double pos=positions[i];
                if(preDropGap&&pos>=3.0)continue;
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;

                int degree=h->scaleDegree;
                int note=rootMidi+scaleSemitoneForDegree(degree)-12;

                const float move=random01(bassMotifSeed,0x7200+i);
                if(bassMode==2)
                {
                    // Progressive/festival pickup: lift an octave when the safe
                    // support register permits it, otherwise use a fifth rather
                    // than folding the octave back to the identical root.
                    if(i==count-1&&(drop||chorus||finalHook))note+=note+12<=52?12:7;
                }
                else if(bassMode==4)
                {
                    const float bassPassing=harmonyPlan.passingProbability;
                    if(i>0&&i==count-1&&move<bassPassing)
                    {
                        const int dir=random01(bassMotifSeed,0x7210+i)>.5f?1:-1;
                        note=rootMidi+scaleSemitoneForDegree(degree+dir)-12;
                    }
                    else if(i>0&&i%3==2)note+=7;
                }

                note=foldBass(note);
                double rawLen=bassMode==0?3.20:(bassMode==1?.34:(bassMode==3?.42:.62));
                if(i+1<count)rawLen=juce::jmin(rawLen,juce::jmax(.08,positions[i+1]-pos-.08));
                rawLen=juce::jmin(rawLen,juce::jmax(.08,4.0-pos-.08));
                const double len=preDropGap?juce::jmin(rawLen,juce::jmax(.08,3.0-pos)):rawLen;
                addNote(bass,note,barBeat+pos,len,
                        juce::jlimit(62,122,72+(int)(energy*22.f)+(int)(goal.bassDrive*10.f)
                            +(int)(random01(domains.bass,0x7300+bs+i)*12.f)));
            }
        }

        // SUB is a monophonic fundamental layer. It starts just after strong kick
        // onsets, uses at most two non-overlapping notes per bar, and leaves the
        // final beat clear at the pre-drop transition.
        if(!intro||localBar>=juce::jmax(1,section->bars/2))
        {
            int subCount=1;
            double positions[2]={0.16,2.16};
            if((dnbBass||techBass)&&(drop||chorus||finalHook))subCount=2;
            if(breakdown)subCount=(localBar%2==0&&energy>=.24f)?1:0;
            if(build&&localBar<section->bars/2)subCount=0;
            if(sparseLowEnd||mainstreamSong||houseBass)subCount=juce::jmin(subCount,1);
            if(energy<.20f||goal.bassDrive<.22f)subCount=0;

            for(int i=0;i<subCount;++i)
            {
                const double pos=positions[i];
                if(preDropGap&&pos>=3.0)continue;
                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                int note=rootMidi+scaleSemitoneForDegree(h->scaleDegree)-24;
                while(note<24)note+=12;
                while(note>43)note-=12;
                double rawLen=subCount==1?juce::jmin(3.18,4.0-pos-.12):juce::jmin(1.72,4.0-pos-.12);
                if(i+1<subCount)rawLen=juce::jmin(rawLen,positions[i+1]-pos-.12);
                const double len=preDropGap?juce::jmin(rawLen,juce::jmax(.08,3.0-pos)):rawLen;
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
            if(goal.density<.42f)steps=juce::jmin(steps,2);
            else if(goal.density>.84f&&!mainstreamSong)steps=juce::jmin(7,steps+1);
            if(mainstreamSong)
                steps=2;
            for(int i=0;i<steps;++i)
            {
                const uint64_t ps=(uint64_t)i;
                if(!mainstreamSong&&i>0&&random01(pluckMotifSeed,0x8100+ps)<((drop||chorus)?.16f:.24f))continue;

                double pos=i*(4.0/steps);
                if(!mainstreamSong&&i%2&&random01(pluckMotifSeed,0x8110+ps)<.45f)
                    pos+=((random01(pluckMotifSeed,0x8120+ps)-.5f)*.08f);
                if(preDropGap&&pos>=3.0)continue;

                const auto* h=harmonyAtBeat(barBeat+pos);
                if(h==nullptr)continue;
                const auto tones=mainstreamSong?closeSongChord(*h):chordTonesFor(*h);
                const int patternIndex=arpPatterns[arpMode][(i+supportMotifBar+supportTheme)%8];

                int note=tones[(size_t)(patternIndex%3)];
                if(mainstreamSong)
                {
                    // PLUCK is a chord-tone support pattern in one register, not a
                    // second melody. Keep every onset around the same octave.
                    const int chordIndex=(i+supportMotifBar)%3;
                    note=tones[(size_t)chordIndex];
                    const int anchor=i==0?66:70;
                    note=foldNear(note,62,76,anchor);
                }
                else
                {
                    note+=24;
                    if(random01(pluckMotifSeed,0x8130+ps)<harmonyPlan.passingProbability)
                    {
                        const int dir=random01(pluckMotifSeed,0x8140+ps)>.5f?1:-1;
                        note=rootMidi+scaleSemitoneForDegree(h->scaleDegree+dir)+24;
                    }
                    if(finalHook&&i%4==3&&random01(pluckMotifSeed,0x8150+ps)>.50f)note+=12;
                }

                addNote(pluck,note,barBeat+juce::jlimit(0.0,3.90,pos),
                        mainstreamSong?.32:(.11+random01(pluckMotifSeed,0x8160+ps)*.22),
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

    const auto p=resolvedPrompt.toLowerCase();
    const bool tech=p.contains("tech house")||p.contains("minimal house");
    const bool dnb=p.contains("drum and bass")||p.contains("dnb");
    const bool cinematic=p.contains("cinematic")||p.contains("film");
    const bool pop=p.contains("pop")||p.contains("radio");
    const bool progressive=p.contains("progressive house")||p.contains("melodic house");
    const bool trance=p.contains("trance");
    const bool tropical=p.contains("tropical");
    const bool mainstreamEdm=progressive||pop||trance||p.contains("festival")||p.contains("mainstage")
        ||p.contains("future rave")||(p.contains("edm")&&!tech&&!dnb);
    const bool disableCounter=p.contains("no counter")||p.contains("no counter melody")
        ||p.contains("main melody only")||p.contains("single lead");

    int architecture=plan.melodyArchetype%12;
    if(mainstreamEdm)
    {
        static constexpr int songArchetypes[6]={0,2,3,4,6,7}; // call/response, anthem, sparse, lyric, pedal/answer, short sequence
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
        const auto& goal=sectionGoalFor(section);
        const bool preDropGap=localBar==section->bars-1&&sectionFlowsIntoImpact(section)
            && (p.contains("house")||p.contains("edm")||p.contains("festival")||p.contains("mainstage"));

        // v1.3 section roles: the main melody is deliberately absent in places.
        // Silence is part of the arrangement, so the chorus/drop hook feels like
        // an arrival instead of one continuous intro loop.
        bool active=energy>=.17f&&goal.melodyActivity>=.16f;
        if(intro)active=active&&localBar>=juce::jmax(0,section->bars-2); // 2-bar teaser only
        else if(verse)active=active&&(localBar%4!=2);                    // 3-bar phrase + one breathing bar
        else if(build)active=active&&localBar>=juce::jmax(1,section->bars/2); // second half
        else if(breakdown)active=active&&energy>=.26f
            &&(localBar%4==0||localBar%4==2);                            // sparse replies
        if(!active)continue;

        const int phraseBars=(chorus||drop||finalHook)?4:juce::jmax(2,plan.phraseBars);
        const int barInPhrase=localBar%phraseBars;
        const int phraseIndex=localBar/phraseBars;

        // DROP and FINAL HOOK share a recognizable song identity, but the final hook
        // develops it. Other sections get genuinely separate theme seeds.
        // CHORUS introduces the song's primary hook. DROP and FINAL HOOK develop
        // that same identity instead of inventing another melody.
        int themeGroup=(chorus||drop||finalHook)?3:sectionIndex;
        if(verse)for(size_t si=0;si<sections.size();++si)if(sections[si].name=="VERSE"){themeGroup=(int)si;break;}

        // One section/theme owns a stable motif. Earlier builds re-rolled the
        // starting degree and rhythm family every bar, creating technically
        // in-key but musically unrelated notes.
        const float sectionDevelopment=juce::jlimit(0.f,1.f,
            plan.development*.55f+goal.development*.45f);
        const int phraseGeneration=(chorus||drop)?0:
            (finalHook?(localBar>=juce::jmax(4,section->bars-4)?1:0):
             ((sectionDevelopment>.80f)?phraseIndex/2:(sectionDevelopment>.58f?phraseIndex/3:0)));
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
        const float sectionDensity=juce::jlimit(0.f,1.f,plan.density*.55f+goal.melodyActivity*.45f);
        if(sectionDensity>.80f&&!cinematic)count=juce::jmin(6,count+1);
        if(sectionDensity<.52f)count=juce::jmax(2,count-1);

        // v1.1 note-budget: the hook must be readable at a glance and singable.
        // No section is allowed to become an eight-note-per-bar event cloud.
        if(intro)count=juce::jlimit(1,2,count);
        else if(verse)count=juce::jlimit(2,3,count-1);
        else if(breakdown)count=juce::jlimit(1,2,count);
        else if(build)count=juce::jlimit(2,3,count);
        else if(chorus)count=3;
        else if(drop||finalHook)count=3;
        else count=juce::jmin(count,3);
        if(mainstreamEdm&&p.contains("simple melody"))count=juce::jmin(count,2);

        // v2.2: make the planned motif length control the rendered hook.  The
        // old engine calculated motifLength but then forced every mainstream
        // chorus/drop bar to three notes, so "short hook" and seed-to-seed motif
        // decisions did not affect the audible hook density.  Distribute a
        // four-to-six note motif across its two bars while retaining the normal
        // two-to-three meaningful notes per-bar range.
        if(mainstreamEdm&&(chorus||drop||finalHook))
        {
            const int motifBudget=juce::jlimit(4,6,plan.motifLength);
            const int thisBarBudget=motifBar==0?(motifBudget+1)/2:motifBudget/2;
            count=juce::jlimit(2,3,juce::jmin(count,thisBarBudget));
        }

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

        // Every mainstream hook begins on the downbeat. A clear first note makes
        // CHORUS -> DROP transitions readable instead of sounding like one endless loop.
        if(mainstreamEdm&&(chorus||drop||finalHook)&&count>0)positions[0]=0.0;
        // Reserve a clear late-bar cadence slot at the end of each four-bar
        // statement. Without this, a two-note short hook could "resolve" near
        // beat two and leave the phrase ending musically undefined.
        if(mainstreamEdm&&(chorus||drop||finalHook)
           &&barInPhrase==phraseBars-1&&count>0)
            positions[count-1]=3.25;

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
            const bool phraseEndingNote=barInPhrase==phraseBars-1&&i==count-1;
            const bool motifEndingNote=motifBar==1&&i==count-1;
            const bool minimumHookDensity=(chorus||drop||finalHook)&&count<=2;

            float restChance=plan.restAmount;
            if(chorus)restChance=juce::jmax(.10f,restChance*.70f);
            if(drop||finalHook)restChance=juce::jmax(.08f,restChance*.58f);
            if(chorus||drop||finalHook)
                restChance*=juce::jlimit(.52f,1.18f,1.34f-plan.hookStrength*.62f);
            if(verse)restChance=juce::jmax(.16f,restChance*1.28f);
            if(breakdown)restChance=juce::jmax(restChance,.30f);
            if(tech)restChance=juce::jmax(restChance,.24f);
            if(architecture==3)restChance*=.55f; // already sparse by construction
            // A motif/phrase ending may never disappear because of a random
            // rest. It carries the cadence and is essential to question/answer form.
            if(!strong&&!minimumHookDensity&&!motifEndingNote&&!phraseEndingNote
               &&random01(motifDecisionSeed,0x2100+salt)<restChance)continue;

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
            const bool phraseEnding=phraseEndingNote;
            const bool barAnchor=positions[i]<.08;
            const bool hookSection=chorus||drop||finalHook;
            const float chordToneChance=hookSection
                ?(phraseEnding?.98f:(barAnchor?.78f:(strong?.52f:.24f)))
                :(phraseEnding?.98f:(barAnchor?.88f:(strong?.62f:.30f)));
            if(random01(motifDecisionSeed,0x2300+salt)<chordToneChance)
            {
                const int toneChoice=(int)(random01(motifDecisionSeed,0x2310+salt)*3.f)%3;
                if(toneChoice==0)degree=localHarmony.degreeIndex;
                else if(toneChoice==1)degree=localHarmony.degreeIndex+2;
                else degree=localHarmony.degreeIndex+4;
            }
            else if(!strong&&random01(motifDecisionSeed,0x2320+salt)<(mainstreamEdm?.04f:.14f))
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
               &&architecture!=1&&architecture!=2&&!mainstreamEdm
               &&random01(phraseSeed,0x2350+salt)<plan.development*.55f)
                degree+=random01(phraseSeed,0x2360+salt)>.5f?2:-1;

            // Four-bar mainstream hooks use an explicit A/A' phrase. Bars one
            // and two establish the two-bar motif; bars three and four keep its
            // rhythm but develop one interior tone before the final cadence.
            // This creates recognizable repetition without an ABAB copy loop.
            if(mainstreamEdm&&developmentBar&&!phraseEnding
               &&((barInPhrase==2&&i==juce::jmax(0,count-2))
                  ||(barInPhrase==3&&i==0)))
            {
                const int answerDirection=random01(motifSeed,0x2370)>.5f?1:-1;
                degree+=answerDirection;
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
            if(finalHook&&plan.octaveRange>=2
               &&random01(motifDecisionSeed,0x2400+salt)>(.86f-.18f*goal.development))
                registerNow+=12;

            double pos=positions[i];
            if(!(chorus||drop||finalHook)&&!strong&&plan.syncopation>.45f
               &&random01(motifDecisionSeed,0x2500+salt)<plan.syncopation*.42f)
                pos+=random01(motifDecisionSeed,0x2510+salt)>.5f?.125:-.125;
            pos=juce::jlimit(0.0,3.90,pos);
            if(preDropGap&&pos>=3.0)continue;

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
                57+(int)(energy*31.f)+(int)(goal.melodyActivity*8.f)
                +(strong?7:0)+(int)(phraseAccent*10.f)-5);

            addNote(lead,scaleNote(degree,registerNow),bar*beatsPerBar+pos,length,velocity);
            lastDegree=degree;
        }

        // Build sections climb toward the next section with a musically clear pickup.
        if(build&&localBar>=juce::jmax(0,section->bars-2))
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
        const bool plannedCounter=plan.counterPolicy==0?finalHook:
            (plan.counterPolicy==1?(drop||finalHook):
             (plan.counterPolicy==2?(chorus||drop||finalHook):false));
        if(!disableCounter&&plannedCounter&&!tech&&answerBar&&counterSpace)
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
            const int low=hook?62:(lowEnergy?58:60);
            const int high=hook?82:(lowEnergy?74:79);
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

        // Song hook memory: keep one identity but give sections different jobs.
        // CHORUS states the four-bar idea, DROP turns its first two bars into a
        // simpler driving cell, and FINAL HOOK restores the full phrase.
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
                    return s&&(s==chorusSection||s->name.contains("DROP")||s==finalSection||s->name=="OUTRO");
                };
                for(const auto& n:lead.notes)if(!insideHookSection(n.beat))rebuilt.push_back(n);

                auto writeHook=[&](const ArrangementSection& section,int velocityLift,
                                     bool preservePreDropGap,bool dropVariant,bool finalVariant)
                {
                    const double start=section.startBar*beatsPerBar;
                    const double end=(section.startBar+section.bars)*beatsPerBar;
                    const int blockBars=dropVariant?2:4;
                    const double sourceBeats=dropVariant?8.0:16.0;
                    for(int block=0;block<section.bars;block+=blockBars)
                    {
                        const int statement=block/juce::jmax(1,blockBars);
                        bool rhythmDeveloped=false;
                        for(size_t ti=0;ti<hookTemplate.size();++ti)
                        {
                            const auto& t=hookTemplate[ti];
                            if(t.beat>=sourceBeats)continue;
                            const double baseBeat=start+block*beatsPerBar+t.beat;
                            if(baseBeat>=end)continue;
                            const bool finalChorusBlock=preservePreDropGap&&block+blockBars>=section.bars;
                            if(finalChorusBlock&&t.beat>=sourceBeats-1.0)continue;

                            auto n=t;
                            n.beat=baseBeat;

                            // FINAL HOOK recalls the primary phrase once, then develops
                            // the answer half. Octave development preserves pitch class;
                            // a single 1/8-beat displacement adds forward motion without
                            // destroying the recognizable four-bar rhythm.
                            if(finalVariant&&statement>0)
                            {
                                const int sourceBar=(int)std::floor(t.beat/beatsPerBar);
                                const double beatInBar=t.beat-sourceBar*beatsPerBar;
                                const float evolution=juce::jlimit(0.f,1.f,plan.finalEvolution);
                                if(evolution>.38f&&sourceBar==2&&n.note+12<=86)n.note+=12;
                                if(evolution>.58f&&sourceBar>=3&&!rhythmDeveloped&&beatInBar>0.20&&n.beat+.125<end)
                                {
                                    n.beat+=.125;
                                    rhythmDeveloped=true;
                                }
                                if(evolution>.78f&&sourceBar==3&&ti%3==1)
                                {
                                    const uint64_t evolutionSeed=mix64(domains.melody
                                        ^ ((uint64_t)section.startBar+1ULL)*0x517cc1b727220a95ULL
                                        ^ ((uint64_t)statement+1ULL)*0x94d049bb133111ebULL);
                                    const int direction=random01(evolutionSeed,0x4f00+(uint64_t)ti)>.5f?1:-1;
                                    n.note=nearestScalePitch(n.note+direction*2,62,86);
                                }
                                if(block+blockBars>=section.bars)
                                    n.velocity=juce::jmin(124,n.velocity+(int)std::lround(1.f+5.f*evolution));
                            }

                            if(dropVariant)
                            {
                                if(section.name=="DROP 2"&&statement>0&&ti%4==3&&plan.finalEvolution>.38f)
                                {
                                    if(n.note+12<=86)n.note+=12;
                                    if(n.beat+.125<end)n.beat+=.125;
                                    n.velocity=juce::jmin(124,n.velocity+3);
                                }
                                const float articulation=plan.dropCharacter==2?.76f:(plan.dropCharacter==4?.72f:(plan.dropCharacter==3?.92f:.88f));
                                n.length=juce::jmax(.10,n.length*articulation);
                                if(statement>0&&(plan.dropCharacter==2||plan.dropCharacter==4)&&ti%5==2&&n.beat+.125<end)
                                    n.beat+=.125;
                            }
                            n.length=juce::jmin(n.length,juce::jmax(.08,end-n.beat-.02));
                            n.velocity=juce::jlimit(48,124,n.velocity+velocityLift);
                            rebuilt.push_back(n);
                        }
                    }
                };

                writeHook(*chorusSection,0,sectionFlowsIntoImpact(chorusSection),false,false);
                for(const auto& hookSection:sections)
                {
                    if(hookSection.name.contains("DROP"))writeHook(hookSection,hookSection.name.contains("2")?11:9,sectionFlowsIntoImpact(&hookSection),true,false);
                    else if(hookSection.name=="FINAL HOOK")writeHook(hookSection,11,false,false,true);
                    else if(hookSection.name=="OUTRO")
                    {
                        for(const auto& t:hookTemplate)if(t.beat<8.0)
                        {auto n=t;n.beat=hookSection.startBar*beatsPerBar+t.beat;n.velocity=juce::jmax(42,n.velocity-24);rebuilt.push_back(n);}
                    }
                }
                std::sort(rebuilt.begin(),rebuilt.end(),
                          [](const ArrangementNote& a,const ArrangementNote& b){return a.beat<b.beat;});
                lead.notes=std::move(rebuilt);
            }
        }
    }

    lanes.push_back(std::move(lead));
    lanes.push_back(std::move(counter));
}


void SongArrangement::alignPitchedLanesToLead()
{
    const auto p=resolvedPrompt.toLowerCase();
    const bool songMode=p.contains("progressive house")||p.contains("melodic house")
        ||p.contains("edm")||p.contains("pop")||p.contains("trance")
        ||p.contains("festival")||p.contains("mainstage")||p.contains("future rave");
    if(!songMode)return;

    ArrangementLane *lead=nullptr,*chords=nullptr,*pluck=nullptr,*pad=nullptr,*counter=nullptr;
    for(auto& lane:lanes)
    {
        if(lane.name=="LEAD")lead=&lane;
        else if(lane.name=="CHORDS")chords=&lane;
        else if(lane.name=="PLUCK")pluck=&lane;
        else if(lane.name=="PAD")pad=&lane;
        else if(lane.name=="COUNTER")counter=&lane;
    }
    if(lead==nullptr||lead->notes.empty())return;

    // Establish one common melodic octave family first. Preserve pitch class and
    // hook contour, but remove the unnecessary "lead lives an octave above the
    // whole song" behavior that makes every lane look unrelated in FL Studio.
    std::sort(lead->notes.begin(),lead->notes.end(),
              [](const ArrangementNote& a,const ArrangementNote& b){return a.beat<b.beat;});
    int previousLead=-1;
    const ArrangementSection* previousLeadSection=nullptr;
    for(auto& n:lead->notes)
    {
        const int bar=juce::jlimit(0,bars-1,(int)std::floor(n.beat/beatsPerBar));
        const ArrangementSection* currentSection=nullptr;
        for(const auto& s:sections)if(sectionContains(s,bar)){currentSection=&s;break;}
        if(currentSection!=previousLeadSection)previousLead=-1;

        const bool developedFinal=currentSection!=nullptr
            &&currentSection->name.contains("FINAL")
            &&plan.finalEvolution>.38f
            &&bar-currentSection->startBar>=4;
        const int leadHigh=developedFinal?86:79;

        while(n.note>leadHigh&&n.note-12>=58)n.note-=12;
        while(n.note<58&&n.note+12<=leadHigh)n.note+=12;
        if(previousLead>=0)
        {
            while(n.note-previousLead>12&&n.note-12>=58)n.note-=12;
            while(previousLead-n.note>12&&n.note+12<=leadHigh)n.note+=12;
            // Non-final sections retain the old close melodic register. The later
            // FINAL HOOK intentionally allows one octave-class lift as development.
            if(!developedFinal)
            {
                while(n.note-previousLead>7&&n.note-12>=58)n.note-=12;
                while(previousLead-n.note>7&&n.note+12<=leadHigh)n.note+=12;
            }
        }
        n.note=juce::jlimit(58,leadHigh,n.note);
        previousLead=n.note;
        previousLeadSection=currentSection;
    }

    std::vector<int> barCentre((size_t)bars,67);
    int lastKnownCentre=67;
    for(int bar=0;bar<bars;++bar)
    {
        const double begin=bar*beatsPerBar,end=begin+beatsPerBar;
        int total=0,count=0;
        for(const auto& n:lead->notes)
            if(n.beat>=begin&&n.beat<end){total+=n.note;++count;}
        if(count>0)lastKnownCentre=(int)std::lround(total/(double)count);
        barCentre[(size_t)bar]=lastKnownCentre;
    }

    auto alignGrouped=[&](ArrangementLane* lane,int offset,int low,int high)
    {
        if(lane==nullptr||lane->notes.empty())return;
        std::sort(lane->notes.begin(),lane->notes.end(),
                  [](const ArrangementNote& a,const ArrangementNote& b)
                  {
                      if(std::abs(a.beat-b.beat)>.0001)return a.beat<b.beat;
                      return a.note<b.note;
                  });

        size_t i=0;
        while(i<lane->notes.size())
        {
            size_t j=i+1;
            while(j<lane->notes.size()&&std::abs(lane->notes[j].beat-lane->notes[i].beat)<.0001)++j;

            float average=0.f;int groupMin=127,groupMax=0;
            for(size_t k=i;k<j;++k)
            {
                average+=lane->notes[k].note;
                groupMin=juce::jmin(groupMin,lane->notes[k].note);
                groupMax=juce::jmax(groupMax,lane->notes[k].note);
            }
            average/=(float)(j-i);

            const int groupBar=juce::jlimit(0,bars-1,
                (int)std::floor(lane->notes[i].beat/beatsPerBar));
            const int target=barCentre[(size_t)groupBar]+offset;
            int bestShift=0;float bestScore=1.0e9f;
            for(int oct=-4;oct<=4;++oct)
            {
                const int shift=oct*12;
                const int lo=groupMin+shift,hi=groupMax+shift;
                float score=std::abs((average+shift)-(float)target);
                if(lo<low)score+=(low-lo)*5.f;
                if(hi>high)score+=(hi-high)*5.f;
                if(score<bestScore){bestScore=score;bestShift=shift;}
            }

            for(size_t k=i;k<j;++k)
            {
                int aligned=lane->notes[k].note+bestShift;
                // Close inversion around the shared lead center. Each tone keeps
                // its pitch class, but one top/bottom voice cannot drag the whole
                // chord an octave away from the song.
                while(aligned-target>7&&aligned-12>=low)aligned-=12;
                while(target-aligned>7&&aligned+12<=high)aligned+=12;
                lane->notes[k].note=juce::jlimit(low,high,aligned);
            }
            i=j;
        }
    };

    // Same song, same register family. Only BASS/SUB intentionally live below it.
    alignGrouped(chords,-4,52,76);
    alignGrouped(pluck,-1,60,79);
    alignGrouped(pad,-5,55,78);
    alignGrouped(counter,3,60,84);
}

void SongArrangement::addFx(uint64_t seed)
{
    ArrangementLane fx{"FX / TRANSITIONS",8,false};
    const auto prompt=resolvedPrompt.toLowerCase();
    const bool festival=prompt.contains("festival")||prompt.contains("mainstage")
        ||prompt.contains("big room")||prompt.contains("powerful drop")
        ||prompt.contains("massive drop");
    const bool cinematic=prompt.contains("cinematic")||prompt.contains("film");
    const bool minimal=prompt.contains("minimal")||prompt.contains("subtle transition")
        ||prompt.contains("subtle fx");
    const int style=static_cast<int>(random01(seed,801)*4.f)%4;

    // FX pitches are a semantic grammar as well as MIDI: 36-47 are arrival
    // accents, 52-63 descend after releases, 66-73 mark atmosphere changes,
    // 74-81 create filter-tension steps, 82-89 are sustained noise rises, and
    // 92-97 are short reverse/suck cues. Every onset is tied to a boundary.
    for(size_t i=1;i<sections.size();++i)
    {
        const auto& s=sections[i];
        const auto& previous=sections[i-1];
        const double boundary=s.startBar*beatsPerBar;
        const float previousEnergy=previous.energy;
        const float energyDelta=s.energy-previousEnergy;
        const auto& goal=sectionGoals.size()>i?sectionGoals[i]:sectionGoalFor(&s);
        const float transitionStrength=juce::jlimit(.55f,1.35f,
            plan.transitionIntensity*(.68f+.30f*juce::jmax(s.energy,std::abs(energyDelta))
                +.10f*goal.development));
        const bool dropArrival=s.name.contains("DROP")||s.name.contains("FINAL")
            ||s.name.contains("HOOK");
        const bool chorusArrival=s.name.contains("CHORUS");
        const bool buildArrival=s.name.contains("BUILD");
        const bool breakdownArrival=s.name.contains("BREAKDOWN");
        const bool majorArrival=dropArrival||chorusArrival;
        // Named musical roles decide whether a rise is useful. The previous
        // energy-only rule put a full riser on INTRO -> VERSE simply because the
        // numeric curve increased, even though that is normally a subtle change.
        const bool lift=majorArrival||buildArrival;
        const bool release=breakdownArrival||energyDelta<=-.12f;

        if(lift&&!release)
        {
            double leadIn=buildArrival?2.0:(chorusArrival?4.0:6.0);
            if(dropArrival)
                leadIn=festival?8.0:(style==0?4.0:style==1?6.0:style==2?3.0:5.0);
            if(minimal)leadIn=juce::jmin(leadIn,dropArrival?4.0:2.0);
            if(cinematic)leadIn=juce::jmin(8.0,leadIn+2.0);
            leadIn=juce::jmin(8.0,leadIn*transitionStrength);
            leadIn=juce::jmin(leadIn,(double)previous.bars*beatsPerBar);

            const int riserNote=82+(int)(random01(seed,810+i)*8.f);
            const int riserVelocity=juce::jlimit(52,122,
                52+(int)(s.energy*39.f)+(int)(transitionStrength*8.f)
                +(dropArrival?12:(chorusArrival?6:0))
                +(int)(random01(seed,820+i)*5.f));
            addNote(fx,riserNote,juce::jmax(0.0,boundary-leadIn),
                    juce::jmax(.35,leadIn-.10),riserVelocity);

            // Three ascending, increasingly strong pulses make filter tension
            // audible even though the whole transition lane shares one patch.
            if(majorArrival)
            {
                const int tensionBase=74+(style%2);
                const int transitionLift=(int)std::llround((transitionStrength-1.f)*18.f);
                addNote(fx,tensionBase,  juce::jmax(0.0,boundary-2.00),.24,juce::jlimit(48,122,66+(dropArrival?8:0)+transitionLift));
                addNote(fx,tensionBase+3,juce::jmax(0.0,boundary-1.25),.20,juce::jlimit(48,124,76+(dropArrival?9:0)+transitionLift));
                addNote(fx,tensionBase+6,juce::jmax(0.0,boundary-.75), .16,juce::jlimit(48,126,88+(dropArrival?10:0)+transitionLift));

                // Reverse/suck ends before the boundary so the final instant is
                // real air rather than an FX tail smeared over the downbeat.
                addNote(fx,92+(int)(random01(seed,825+i)*6.f),
                        juce::jmax(0.0,boundary-.50),.36,
                        juce::jlimit(62,116,72+(int)(s.energy*28.f)+(dropArrival?8:0)));
            }

            // Short airy arrival accent. The physical crash/impact is rendered by
            // PERCUSSION, keeping the swept lane free of low-frequency buildup.
            if(majorArrival)
                addNote(fx,dropArrival?43:47,boundary,.22+(dropArrival?.12:0.0),
                        juce::jlimit(76,122,88+(int)(s.energy*26.f)));
        }
        else if(release)
        {
            // A short descending sequence makes the downlifter direction explicit,
            // followed by a longer atmospheric bed for the new low-energy space.
            const int top=61+(style%2);
            addNote(fx,top,  boundary,.34,82+(int)(previousEnergy*24.f));
            addNote(fx,top-4,boundary+.32,.42,74+(int)(previousEnergy*20.f));
            addNote(fx,top-8,boundary+.72,.62,66+(int)(previousEnergy*17.f));
            const double bedLength=(cinematic?7.5:3.5)*(.72+.45*goal.space);
            addNote(fx,66+(style%4),boundary+.05,bedLength,
                    juce::jlimit(48,92,54+(int)(s.energy*34.f)+(int)(goal.space*8.f)));
        }
        else
        {
            // Neutral INTRO -> VERSE style changes receive one restrained marker;
            // no random FX are scattered through the body of a section.
            addNote(fx,68+(style%5),boundary+.08,.42,
                    juce::jlimit(48,84,54+(int)(s.energy*30.f)));
        }
    }

    std::sort(fx.notes.begin(),fx.notes.end(),[](const ArrangementNote& a,const ArrangementNote& b)
    {
        if(std::abs(a.beat-b.beat)>.0001)return a.beat<b.beat;
        return a.note<b.note;
    });
    lanes.push_back(std::move(fx));
}

bool SongArrangement::validate(juce::String* reason) const
{
    auto fail=[&](const juce::String& why){if(reason)*reason=why;return false;};
    if(!std::isfinite(tempo)||tempo<60.0||tempo>200.0||bars<1||bars>512)
        return fail("Invalid tempo or duration");
    if(lanes.size()!=12||sections.empty()||sectionGoals.size()!=sections.size())
        return fail("Incomplete producer plan");
    int boundary=0;
    for(const auto& s:sections)
    {
        if(s.startBar!=boundary||s.bars<=0||!std::isfinite(s.energy))return fail("Invalid section timing");
        boundary+=s.bars;
    }
    if(boundary!=bars)return fail("Sections do not cover the song");
    size_t count=0;
    for(const auto& lane:lanes)
    {
        double previous=-1.0;
        for(const auto& n:lane.notes)
        {
            if(!std::isfinite(n.beat)||!std::isfinite(n.length)||n.beat<0.0||n.beat<previous
               ||n.beat>=getTotalBeats()||n.length<=0.0||n.beat+n.length>getTotalBeats()+.001
               ||n.note<0||n.note>127||n.velocity<1||n.velocity>127)return fail("Invalid MIDI event");
            previous=n.beat;
            if(++count>200000)return fail("Arrangement exceeds event budget");
        }
    }
    if(count==0)return fail("The prompt excludes every instrument");
    return true;
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
    auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(getTotalBeats()*tpq);conductor.addEvent(end);
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
        auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(getTotalBeats()*tpq);seq.addEvent(end);
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
    root.setProperty("schema",5,nullptr);root.setProperty("prompt",sourcePrompt,nullptr);root.setProperty("resolvedPrompt",resolvedPrompt,nullptr);root.setProperty("exclusions",(int)promptIntent.exclusionMask,nullptr);root.setProperty("bpm",tempo,nullptr);root.setProperty("bars",bars,nullptr);root.setProperty("rootMidi",rootMidi,nullptr);root.setProperty("minor",minor,nullptr);
    root.setProperty("songId",juce::String::toHexString((juce::int64)masterSeed),nullptr);
    root.setProperty("harmonyId",juce::String::toHexString((juce::int64)harmonyId),nullptr);
    root.setProperty("melodyId",juce::String::toHexString((juce::int64)melodyId),nullptr);

    juce::ValueTree songPlan("COMPOSITION_DNA");
    songPlan.setProperty("structureStyle",plan.structureStyle,nullptr);songPlan.setProperty("structureVariant",plan.structureVariant,nullptr);songPlan.setProperty("targetBars",plan.targetBars,nullptr);
    songPlan.setProperty("drumGroove",plan.drumGroove,nullptr);songPlan.setProperty("hatMode",plan.hatMode,nullptr);
    songPlan.setProperty("bassMode",plan.bassMode,nullptr);songPlan.setProperty("chordMode",plan.chordMode,nullptr);songPlan.setProperty("arpMode",plan.arpMode,nullptr);
    songPlan.setProperty("melodyArchetype",plan.melodyArchetype,nullptr);songPlan.setProperty("rhythmFamily",plan.rhythmFamily,nullptr);songPlan.setProperty("startingDegree",plan.startingDegree,nullptr);
    songPlan.setProperty("cadenceStyle",plan.cadenceStyle,nullptr);songPlan.setProperty("motifLength",plan.motifLength,nullptr);songPlan.setProperty("phraseBars",plan.phraseBars,nullptr);songPlan.setProperty("octaveRange",plan.octaveRange,nullptr);
    songPlan.setProperty("genreFamily",plan.genreFamily,nullptr);songPlan.setProperty("emotionProfile",plan.emotionProfile,nullptr);songPlan.setProperty("hookShape",plan.hookShape,nullptr);songPlan.setProperty("chordTexture",plan.chordTexture,nullptr);
    songPlan.setProperty("padPolicy",plan.padPolicy,nullptr);songPlan.setProperty("counterPolicy",plan.counterPolicy,nullptr);songPlan.setProperty("hookStrength",plan.hookStrength,nullptr);songPlan.setProperty("dropIntensity",plan.dropIntensity,nullptr);songPlan.setProperty("drumDrive",plan.drumDrive,nullptr);
    songPlan.setProperty("energyContrast",plan.energyContrast,nullptr);songPlan.setProperty("energyBias",plan.energyBias,nullptr);songPlan.setProperty("transitionIntensity",plan.transitionIntensity,nullptr);
    songPlan.setProperty("density",plan.density,nullptr);songPlan.setProperty("syncopation",plan.syncopation,nullptr);songPlan.setProperty("restAmount",plan.restAmount,nullptr);songPlan.setProperty("development",plan.development,nullptr);
    songPlan.setProperty("rhythmicFeel",plan.rhythmicFeel,nullptr);songPlan.setProperty("dropCharacter",plan.dropCharacter,nullptr);
    songPlan.setProperty("brightness",plan.brightness,nullptr);songPlan.setProperty("space",plan.space,nullptr);songPlan.setProperty("aggression",plan.aggression,nullptr);
    songPlan.setProperty("harmonicMotion",plan.harmonicMotion,nullptr);songPlan.setProperty("finalEvolution",plan.finalEvolution,nullptr);songPlan.setProperty("callResponse",plan.callResponse,nullptr);
    songPlan.setProperty("paletteCharacter",palettePlan.character,nullptr);songPlan.setProperty("paletteBrightness",palettePlan.brightness,nullptr);songPlan.setProperty("paletteMovement",palettePlan.movement,nullptr);songPlan.setProperty("paletteSpace",palettePlan.space,nullptr);songPlan.setProperty("paletteImpact",palettePlan.impact,nullptr);
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
    juce::ValueTree goalTree("SECTION_GOALS");
    for(size_t i=0;i<sectionGoals.size();++i)
    {
        const auto& g=sectionGoals[i];juce::ValueTree v("GOAL");
        v.setProperty("index",(int)i,nullptr);
        v.setProperty("name",i<sections.size()?sections[i].name:juce::String{},nullptr);
        v.setProperty("energy",g.energy,nullptr);v.setProperty("density",g.density,nullptr);
        v.setProperty("melody",g.melodyActivity,nullptr);v.setProperty("tension",g.harmonicTension,nullptr);
        v.setProperty("bass",g.bassDrive,nullptr);v.setProperty("drums",g.drumDrive,nullptr);
        v.setProperty("space",g.space,nullptr);v.setProperty("development",g.development,nullptr);
        goalTree.addChild(v,-1,nullptr);
    }
    root.addChild(goalTree,-1,nullptr);
    juce::ValueTree laneTree("LANES");
    for(const auto& lane:lanes){juce::ValueTree l("LANE");l.setProperty("name",lane.name,nullptr);l.setProperty("channel",lane.midiChannel,nullptr);l.setProperty("drums",lane.drums,nullptr);l.addChild(lane.sound.toValueTree(),-1,nullptr);juce::ValueTree notes("NOTES");for(const auto& n:lane.notes){juce::ValueTree v("NOTE");v.setProperty("note",n.note,nullptr);v.setProperty("velocity",n.velocity,nullptr);v.setProperty("beat",n.beat,nullptr);v.setProperty("length",n.length,nullptr);notes.addChild(v,-1,nullptr);}l.addChild(notes,-1,nullptr);laneTree.addChild(l,-1,nullptr);}root.addChild(laneTree,-1,nullptr);
    return root;
}

SongArrangement SongArrangement::fromValueTree(const juce::ValueTree& root)
{
    SongArrangement a;
    if(!root.isValid()||root.getType().toString()!="SONARA_ARRANGEMENT")return a;
    if(root.getChildWithName("LANES").getNumChildren()>12||root.getChildWithName("SECTIONS").getNumChildren()>64
       ||!std::isfinite((double)root.getProperty("bpm",128.0)))return a;
    a.sourcePrompt=root.getProperty("prompt","").toString();a.tempo=juce::jlimit(60.0,200.0,(double)root.getProperty("bpm",128.0));a.bars=juce::jlimit(1,512,(int)root.getProperty("bars",defaultBars));a.rootMidi=juce::jlimit(0,127,(int)root.getProperty("rootMidi",53));a.minor=(bool)root.getProperty("minor",true);a.promptIntent=parsePromptIntent(a.sourcePrompt,a.tempo);a.promptIntent.tempo=a.tempo;a.promptIntent.rootMidi=a.rootMidi;a.promptIntent.minor=a.minor;a.sections.clear();a.lanes.clear();
    a.resolvedPrompt=root.getProperty("resolvedPrompt",ProducerPrompt::parse(a.sourcePrompt).positive).toString();
    a.promptIntent.exclusionMask=(unsigned)(int)root.getProperty("exclusions",(int)a.promptIntent.exclusionMask);
    a.masterSeed=(uint64_t)root.getProperty("songId","0").toString().getHexValue64();
    a.buildSeedDomains(a.masterSeed);
    a.harmonyId=(uint64_t)root.getProperty("harmonyId","0").toString().getHexValue64();
    a.melodyId=(uint64_t)root.getProperty("melodyId","0").toString().getHexValue64();
    auto composition=root.getChildWithName("COMPOSITION_DNA");
    if(composition.isValid())
    {
        a.plan.structureStyle=(int)composition.getProperty("structureStyle",0);a.plan.structureVariant=(int)composition.getProperty("structureVariant",0);a.plan.targetBars=(int)composition.getProperty("targetBars",a.bars);
        a.plan.drumGroove=(int)composition.getProperty("drumGroove",0);a.plan.hatMode=(int)composition.getProperty("hatMode",0);
        a.plan.bassMode=(int)composition.getProperty("bassMode",0);a.plan.chordMode=(int)composition.getProperty("chordMode",0);a.plan.arpMode=(int)composition.getProperty("arpMode",0);
        a.plan.melodyArchetype=(int)composition.getProperty("melodyArchetype",0);a.plan.rhythmFamily=(int)composition.getProperty("rhythmFamily",0);a.plan.startingDegree=(int)composition.getProperty("startingDegree",0);
        a.plan.cadenceStyle=(int)composition.getProperty("cadenceStyle",0);a.plan.motifLength=(int)composition.getProperty("motifLength",8);a.plan.phraseBars=(int)composition.getProperty("phraseBars",4);a.plan.octaveRange=(int)composition.getProperty("octaveRange",2);
        a.plan.genreFamily=juce::jlimit(0,7,(int)composition.getProperty("genreFamily",0));a.plan.emotionProfile=juce::jlimit(0,5,(int)composition.getProperty("emotionProfile",0));a.plan.hookShape=juce::jlimit(0,3,(int)composition.getProperty("hookShape",0));a.plan.chordTexture=juce::jlimit(0,3,(int)composition.getProperty("chordTexture",0));
        a.plan.padPolicy=juce::jlimit(0,3,(int)composition.getProperty("padPolicy",0));a.plan.counterPolicy=juce::jlimit(0,3,(int)composition.getProperty("counterPolicy",1));a.plan.hookStrength=juce::jlimit(.35f,1.f,(float)composition.getProperty("hookStrength",.72f));a.plan.dropIntensity=juce::jlimit(.55f,1.25f,(float)composition.getProperty("dropIntensity",.90f));a.plan.drumDrive=juce::jlimit(.55f,1.25f,(float)composition.getProperty("drumDrive",.90f));
        a.plan.energyContrast=juce::jlimit(.5f,1.5f,(float)composition.getProperty("energyContrast",1.f));a.plan.energyBias=juce::jlimit(-.25f,.25f,(float)composition.getProperty("energyBias",0.f));a.plan.transitionIntensity=juce::jlimit(.4f,1.6f,(float)composition.getProperty("transitionIntensity",1.f));
        a.plan.density=(float)composition.getProperty("density",.65f);a.plan.syncopation=(float)composition.getProperty("syncopation",.35f);a.plan.restAmount=(float)composition.getProperty("restAmount",.18f);a.plan.development=(float)composition.getProperty("development",.55f);
        a.plan.rhythmicFeel=juce::jlimit(0,2,(int)composition.getProperty("rhythmicFeel",0));
        a.plan.dropCharacter=juce::jlimit(1,4,(int)composition.getProperty("dropCharacter",1));
        a.plan.brightness=juce::jlimit(0.f,1.f,(float)composition.getProperty("brightness",.5f));
        a.plan.space=juce::jlimit(0.f,1.f,(float)composition.getProperty("space",.5f));
        a.plan.aggression=juce::jlimit(0.f,1.f,(float)composition.getProperty("aggression",.5f));
        a.plan.harmonicMotion=juce::jlimit(0.f,1.f,(float)composition.getProperty("harmonicMotion",.5f));
        a.plan.finalEvolution=juce::jlimit(0.f,1.f,(float)composition.getProperty("finalEvolution",.65f));
        a.plan.callResponse=juce::jlimit(0.f,1.f,(float)composition.getProperty("callResponse",.5f));
        a.palettePlan.character=juce::jlimit(0,3,(int)composition.getProperty("paletteCharacter",0));
        a.palettePlan.brightness=juce::jlimit(0.f,1.f,(float)composition.getProperty("paletteBrightness",.5f));a.palettePlan.movement=juce::jlimit(0.f,1.f,(float)composition.getProperty("paletteMovement",.5f));a.palettePlan.space=juce::jlimit(0.f,1.f,(float)composition.getProperty("paletteSpace",.5f));a.palettePlan.impact=juce::jlimit(0.f,1.f,(float)composition.getProperty("paletteImpact",.5f));
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
    auto goals=root.getChildWithName("SECTION_GOALS");
    for(int i=0;i<goals.getNumChildren();++i)
    {
        const auto v=goals.getChild(i);if(v.getType().toString()!="GOAL")continue;
        SectionGoal g;
        g.energy=juce::jlimit(.05f,1.f,(float)v.getProperty("energy",.5f));
        g.density=juce::jlimit(.05f,1.f,(float)v.getProperty("density",.5f));
        g.melodyActivity=juce::jlimit(.05f,1.f,(float)v.getProperty("melody",.5f));
        g.harmonicTension=juce::jlimit(.05f,1.f,(float)v.getProperty("tension",.4f));
        g.bassDrive=juce::jlimit(.05f,1.f,(float)v.getProperty("bass",.5f));
        g.drumDrive=juce::jlimit(.05f,1.f,(float)v.getProperty("drums",.5f));
        g.space=juce::jlimit(.05f,1.f,(float)v.getProperty("space",.5f));
        g.development=juce::jlimit(.05f,1.f,(float)v.getProperty("development",.5f));
        a.sectionGoals.push_back(g);
    }
    if(a.sectionGoals.size()!=a.sections.size())
    {
        a.sectionGoals.clear();
        a.buildSectionGoals(a.masterSeed!=0?a.masterSeed:0x360036ULL);
    }
    auto lt=root.getChildWithName("LANES");for(int i=0;i<lt.getNumChildren();++i){auto l=lt.getChild(i);ArrangementLane lane;lane.name=l.getProperty("name","Lane").toString();lane.midiChannel=juce::jlimit(1,16,(int)l.getProperty("channel",1));lane.drums=(bool)l.getProperty("drums",false);auto dna=l.getChildWithName("SoundDNA");if(dna.isValid())lane.sound=SoundDNA::fromValueTree(dna);auto notes=l.getChildWithName("NOTES");for(int j=0;j<notes.getNumChildren();++j){auto n=notes.getChild(j);lane.notes.push_back({juce::jlimit(0,127,(int)n.getProperty("note",60)),juce::jlimit(1,127,(int)n.getProperty("velocity",100)),juce::jmax(0.0,(double)n.getProperty("beat",0.0)),juce::jmax(.03,(double)n.getProperty("length",.5))});}a.lanes.push_back(std::move(lane));}
    for(auto& lane:a.lanes)canonicaliseLane(lane,a.getTotalBeats());
    return a;
}

} // namespace sonara
