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

void SongArrangement::clear()
{
    lanes.clear();
    sections.clear();
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
    plan.structureStyle=(int)((seed ^ (seed>>16)) & 3ULL);
    plan.drumGroove=(int)(random01(seed,0x1002)*6.f)%6;
    plan.hatMode=(int)(random01(seed,0x1003)*4.f)%4;
    plan.progressionIndex=(int)(random01(seed,0x1004)*8.f)%8;
    plan.alternateProgressionIndex=(int)(random01(seed,0x1005)*8.f)%8;
    if(plan.alternateProgressionIndex==plan.progressionIndex)
        plan.alternateProgressionIndex=(plan.alternateProgressionIndex+3)%8;
    plan.bassMode=(int)(random01(seed,0x1006)*5.f)%5;
    plan.chordMode=(int)(random01(seed,0x1007)*5.f)%5;
    plan.arpMode=(int)(random01(seed,0x1008)*6.f)%6;
    plan.melodyArchetype=(int)(random01(seed,0x1009)*12.f)%12;
    plan.rhythmFamily=(int)(random01(seed,0x100b)*10.f)%10;
    plan.startingDegree=(int)(random01(seed,0x100c)*7.f)%7;
    plan.cadenceStyle=(int)(random01(seed,0x100d)*4.f)%4;
    plan.motifLength=3+(int)(random01(seed,0x100a)*6.f);
    plan.phraseBars=random01(seed,0x1010)>.62f?8:4;
    plan.octaveRange=1+(int)(random01(seed,0x1011)*3.f);
    plan.density=.46f+.42f*random01(seed,0x1012);
    plan.syncopation=.12f+.58f*random01(seed,0x1013);
    plan.restAmount=.08f+.30f*random01(seed,0x1014);
    plan.development=.30f+.62f*random01(seed,0x1015);

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
    buildSongPlan(seed ^ 0x6f4d2b19ULL);
    buildSections(seed ^ 0x7a1f2d4bULL);

    const auto lower = prompt.toLowerCase();
    const bool energetic = lower.contains("energetic") || lower.contains("powerful") || lower.contains("festival") || lower.contains("hard") || lower.contains("edm");
    addDrums(seed ^ 0x11223344ULL, energetic);
    addHarmony(seed ^ 0x22334455ULL);
    addMelody(seed ^ 0x33445566ULL, energetic);
    addFx(seed ^ 0x44556677ULL);

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
        const float flavour = random01(seed, 0x9000ULL + static_cast<uint64_t>(i) * 17ULL);
        soundPrompt += flavour < .25f ? "warm soft spacious long release" :
                       flavour < .50f ? "bright crisp wide fast attack" :
                       flavour < .75f ? "dark thick body subtle modulation" :
                                        "airy animated wide heavy modulation";
        lane.sound = designer.generate(soundPrompt, mix64(seed + static_cast<uint64_t>(i) * 0x517cc1b727220a95ULL));

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

    static constexpr int minorProgressions[8][4] = {
        {0,8,3,10}, {0,10,8,10}, {0,3,10,8}, {0,5,8,7},
        {0,8,5,10}, {0,7,8,5}, {0,10,5,8}, {0,3,5,10}
    };
    static constexpr int majorProgressions[8][4] = {
        {0,7,9,5}, {0,9,5,7}, {0,5,9,7}, {9,5,0,7},
        {0,4,5,7}, {0,9,7,5}, {0,5,7,9}, {0,7,5,4}
    };
    const int progressionIndex=plan.progressionIndex;
    const int alternateIndex=plan.alternateProgressionIndex;
    const int third=minor?3:4;
    const int bassMode=plan.bassMode;
    const int chordMode=plan.chordMode;
    const int arpMode=plan.arpMode;
    static constexpr int arpPatterns[6][8] = {
        {0,1,2,1,0,1,2,1},
        {0,2,1,2,0,2,1,2},
        {0,1,2,0,2,1,0,2},
        {2,1,0,1,2,1,0,1},
        {0,2,0,1,2,0,2,1},
        {1,0,2,1,0,2,1,2}
    };

    for(int bar=0;bar<bars;++bar)
    {
        const ArrangementSection* section=nullptr;
        int sectionIndex=0;
        for(size_t si=0;si<sections.size();++si)
            if(sectionContains(sections[si],bar)){section=&sections[si];sectionIndex=(int)si;break;}

        const float energy=section?section->energy:.4f;
        const juce::String sectionName=section?section->name:juce::String();
        const bool drop=sectionName.contains("DROP")||sectionName.contains("HOOK");
        const bool chorus=sectionName.contains("CHORUS");
        const bool breakdown=sectionName.contains("BREAKDOWN");
        const bool verse=sectionName.contains("VERSE");
        const bool intro=sectionName.contains("INTRO");
        const bool finalHook=sectionName.contains("FINAL");
        const int localBar=section?bar-section->startBar:bar;
        const bool useAlt=chorus||finalHook||(sectionIndex%3==2&&random01(seed,210+sectionIndex)>.55f);
        const int* progression=minor
            ? minorProgressions[useAlt?alternateIndex:progressionIndex]
            : majorProgressions[useAlt?alternateIndex:progressionIndex];

        const int degree=progression[(localBar + (sectionIndex%2))%4];
        const int root=rootMidi+degree;
        const double b=bar*beatsPerBar;
        const uint64_t bs=(uint64_t)bar*131ULL;

        if(!intro || localBar>=juce::jmax(1,section?section->bars/2:2))
        {
            const float bassChance=breakdown?.46f:1.f;
            if(random01(seed,2900+bs)<bassChance)
            {
                if(bassMode==0)
                {
                    for(int q=0;q<4;++q)
                        if(drop||chorus||q%2==0)
                        {
                            int n=root-12;
                            if((drop||finalHook) && q==3 && random01(seed,3000+bs+q)>.45f) n+=(finalHook?12:7);
                            addNote(bass,n,b+q,(drop||chorus)?.62:1.35,
                                    juce::jlimit(66,120,84+(int)(energy*22.f)+(int)(random01(seed,3010+bs+q)*10.f)));
                        }
                }
                else if(bassMode==1)
                {
                    static constexpr double pos[6]={0.0,.75,1.5,2.0,2.75,3.5};
                    for(int i=0;i<((drop||chorus)?6:4);++i)
                    {
                        int n=root-12;
                        if(i==2||i==5)n+=7;
                        addNote(bass,n,b+pos[i],.38,78+(int)(energy*24.f)+(i%2)*4);
                    }
                }
                else if(bassMode==2)
                {
                    static constexpr double pos[5]={0.0,1.0,1.75,2.5,3.25};
                    for(int i=0;i<((drop||finalHook)?5:3);++i)
                        addNote(bass,root-12+(i==4?12:0),b+pos[i],.44,
                                82+(int)(energy*22.f)+(int)(random01(seed,3020+bs+i)*8.f));
                }
                else if(bassMode==3)
                {
                    static constexpr double pos[7]={0.0,.5,1.25,2.0,2.5,3.0,3.75};
                    const int count=(drop||chorus)?7:4;
                    for(int i=0;i<count;++i)
                    {
                        const int movement=(i==2?7:(i==5?12:0));
                        addNote(bass,root-12+movement,b+pos[i],.30,
                                76+(int)(energy*27.f)+(int)(random01(seed,3030+bs+i)*7.f));
                    }
                }
                else
                {
                    addNote(bass,root-12,b,.76,98);
                    if(!breakdown)addNote(bass,root-12,b+1.5,.36,86);
                    addNote(bass,root-5,b+2.0,.64,(drop||chorus)?108:91);
                    if(drop||finalHook)addNote(bass,root,b+3.25,.34,98);
                }
            }
        }

        // Dedicated SUB is intentionally simpler than BASS: clean mono fundamentals,
        // no upper movement, and section-aware note lengths to avoid low-end overlap.
        if(!intro || localBar>=juce::jmax(1,section?section->bars/2:2))
        {
            if(breakdown)
            {
                if(localBar%2==0)
                    addNote(sub,juce::jlimit(24,48,root-24),b,3.35,58+(int)(energy*18.f));
            }
            else if(drop||chorus||finalHook)
            {
                const int subSteps=(bassMode==1||bassMode==3)?4:2;
                const double subStep=4.0/subSteps;
                for(int s=0;s<subSteps;++s)
                {
                    if(s>0&&random01(seed,3040+bs+s)<.08f)continue;
                    addNote(sub,juce::jlimit(24,48,root-24),b+s*subStep,
                            juce::jmin(1.55,subStep*.78),68+(int)(energy*20.f));
                }
            }
            else
            {
                addNote(sub,juce::jlimit(24,48,root-24),b,1.72,62+(int)(energy*16.f));
                if(verse&&random01(seed,3048+bs)>.46f)
                    addNote(sub,juce::jlimit(24,48,root-24),b+2.0,1.55,58+(int)(energy*14.f));
            }
        }

        const int repeats=breakdown?1:((drop||chorus||finalHook)?(chordMode==2?2:4):(verse&&chordMode==4?2:1));
        const double unit=4.0/repeats;
        const double chordLen=breakdown?3.55:((drop||chorus)?(chordMode==0?.70:juce::jmin(.72,unit*.68)):juce::jmin(3.45,unit*.86));
        for(int r=0;r<repeats;++r)
        {
            if(verse&&r>0&&random01(seed,3050+bs+r)<.28f)continue;
            const double cb=b+r*unit+(((drop||chorus)&&chordMode==3&&r%2)?0.11:0.0);
            int notes[3]={root+12,root+12+third,root+19};
            const int inversion=(localBar+r+progressionIndex+chordMode+sectionIndex)%3;
            if(inversion>=1)notes[0]+=12;
            if(inversion>=2)notes[1]+=12;
            std::sort(notes,notes+3);
            const int baseVel=62+(int)(energy*25.f)+(int)(random01(seed,3100+bs+r)*9.f);
            addNote(chords,notes[0],cb,chordLen,juce::jlimit(48,112,baseVel));
            addNote(chords,notes[1],cb,chordLen,juce::jlimit(46,110,baseVel-3));
            addNote(chords,notes[2],cb,chordLen,juce::jlimit(44,108,baseVel-5));
            if(finalHook && (r%2==0||chordMode==1))
                addNote(chords,notes[2]+12,cb,chordLen*.78,juce::jlimit(42,102,baseVel-12));
        }

        if(intro||breakdown||verse||chorus)
        {
            if(random01(seed,3190+bar)>.18f)
            {
                const int padOct=(static_cast<int>(random01(seed,3200+bar/4)*2.f))*12;
                const double start=b+(random01(seed,3210+bar)>.78f?.25:0.0);
                const double len=3.20+random01(seed,3220+bar)*.58;
                addNote(pad,root+12+padOct,start,len,47+(int)(random01(seed,3230+bar)*10.f));
                addNote(pad,root+12+third+padOct,start,len,44+(int)(random01(seed,3240+bar)*9.f));
                addNote(pad,root+19+padOct,start,len,42+(int)(random01(seed,3250+bar)*9.f));
            }
        }

        if((energy>.5f||chorus)&&!breakdown)
        {
            const int chordTones[3]={0,third,7};
            const int steps=(drop||finalHook)?8:(chorus?6:4);
            for(int e=0;e<steps;++e)
            {
                if(e>0&&random01(seed,3300+bs+e)<((drop||finalHook)?.07f:.20f))continue;
                const int toneIndex=arpPatterns[arpMode][(e+localBar+sectionIndex)%8];
                int note=root+24+chordTones[toneIndex];
                if(finalHook&&e%4==3&&random01(seed,3310+bs+e)>.42f)note+=12;
                const double pos=b+e*(4.0/steps)+((e%2)?(random01(seed,3320+bs+e)-.5)*.035:0.0);
                addNote(pluck,note,pos,.13+random01(seed,3330+bs+e)*.19,
                        56+(int)(energy*22.f)+(int)(random01(seed,3340+bs+e)*13.f));
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
    const int third=minor?3:4;

    static constexpr int minorProgressions[8][4]={
        {0,8,3,10},{0,10,8,10},{0,3,10,8},{0,5,8,7},
        {0,8,5,10},{0,7,8,5},{0,10,5,8},{0,3,5,10}
    };
    static constexpr int majorProgressions[8][4]={
        {0,7,9,5},{0,9,5,7},{0,5,9,7},{9,5,0,7},
        {0,4,5,7},{0,9,7,5},{0,5,7,9},{0,7,5,4}
    };

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
        return juce::jlimit(48,100,rootMidi+registerSemitones+scale[degree]+12*oct);
    };

    auto sectionForBar=[&](int bar)->std::pair<const ArrangementSection*,int>
    {
        for(size_t i=0;i<sections.size();++i)
            if(sectionContains(sections[i],bar))return {&sections[i],(int)i};
        return {nullptr,0};
    };

    auto chordInfo=[&](const ArrangementSection* section,int sectionIndex,int bar)
    {
        struct ChordInfo { int rootOffset=0; int degreeIndex=0; };
        if(section==nullptr)return ChordInfo{};
        const int localBar=bar-section->startBar;
        const bool chorus=section->name.contains("CHORUS");
        const bool finalHook=section->name.contains("FINAL");
        const bool useAlt=chorus||finalHook||(sectionIndex%3==2&&random01(seed,210+sectionIndex)>.55f);
        const int progressionIndex=useAlt?plan.alternateProgressionIndex:plan.progressionIndex;
        const int* progression=minor?minorProgressions[progressionIndex]:majorProgressions[progressionIndex];
        const int offset=progression[(localBar+(sectionIndex%2))%4];

        int nearest=0;
        int best=999;
        for(int d=0;d<7;++d)
        {
            const int diff=std::abs(scale[d]-offset);
            if(diff<best){best=diff;nearest=d;}
        }
        return ChordInfo{offset,nearest};
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

        const auto chord=chordInfo(section,sectionIndex,bar);
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
                    degree=chord.degreeIndex+chordDegrees[(i+barInPhrase)%6];
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

            // Strong beats gravitate toward the current harmony so radical variation
            // remains musical rather than random.
            if(strong&&random01(phraseSeed,0x2300+salt)<.58f)
            {
                const int toneChoice=(int)(random01(phraseSeed,0x2310+salt)*3.f)%3;
                if(toneChoice==0)degree=chord.degreeIndex;
                else if(toneChoice==1)degree=chord.degreeIndex+2;
                else degree=chord.degreeIndex+4;
            }

            // Phrase cadence is intentionally explicit.
            const bool phraseEnding=(barInPhrase==phraseBars-1&&i==count-1);
            if(phraseEnding)
            {
                if(plan.cadenceStyle==0)degree=chord.degreeIndex;
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
            if(architecture==4)length=.68+random01(phraseSeed,0x2600+salt)*.68;
            else if(architecture==3)length=.30+random01(phraseSeed,0x2610+salt)*.42;
            else if(architecture==2)length=.24+random01(phraseSeed,0x2620+salt)*.30;
            else if(tech||dnb||architecture==5||architecture==10)length=.12+random01(phraseSeed,0x2630+salt)*.16;
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
            const uint64_t cs=mix64(phraseSeed^0x434f554e544552ULL);
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