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

void SongArrangement::buildSections(uint64_t seed)
{
    struct Layout { int lengths[8]; };
    static constexpr Layout layouts[] = {
        {{8,8,8,12,8,8,4,16}},
        {{4,12,8,16,8,8,4,12}},
        {{8,8,4,16,12,8,4,12}},
        {{4,8,8,12,8,12,8,12}}
    };

    const int style = static_cast<int>(random01(seed, 0x51ec7100ULL) * 4.f) % 4;
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
        else if (lane.name == "BASS") soundPrompt += "deep controlled bass mono ";
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
        lane.sound.name = lane.name + " • Generated";
    }
}

void SongArrangement::addDrums(uint64_t seed, bool energetic)
{
    ArrangementLane kick{"KICK",10,true}, snare{"SNARE / CLAP",10,true}, hats{"HATS",10,true}, perc{"PERCUSSION",10,true};
    const auto p = sourcePrompt.toLowerCase();
    const bool house = p.contains("house") || p.contains("future rave") || p.contains("edm");
    const bool trap = p.contains("trap") || p.contains("hip hop");
    const int groove = static_cast<int>(random01(seed, 11) * 4.f) % 4;
    const int hatMode = static_cast<int>(random01(seed, 12) * 3.f) % 3;
    const float swing = .015f + random01(seed,13) * .055f;

    for(int bar=0;bar<bars;++bar)
    {
        float energy=.4f;
        for(const auto& s:sections) if(sectionContains(s,bar)){energy=s.energy;break;}
        const double b=bar*beatsPerBar;
        const bool drop=energy>.8f, build=energy>.55f&&energy<=.8f, intro=bar<8;
        const uint64_t bs=(uint64_t)bar*97ULL;

        if(!intro || bar>=4)
        {
            if(house)
            {
                for(int q=0;q<4;++q)
                    if(!intro || q==0 || q==2 || bar>=6)
                        addNote(kick,36,b+q,.10,drop?118:102+(q==0?4:0));
                if((drop||build) && ((bar+groove)%4==3))
                    addNote(kick,36,b+3.5,.08,88+(int)(random01(seed,1000+bs)*20.f));
                if(drop && groove==2 && bar%2==0)
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
                addNote(kick,36,b+2.0+(groove%2)*.5,.10,drop?116:100);
                if(drop && random01(seed,1130+bs)>.38f) addNote(kick,36,b+3.25,.08,94);
            }
        }

        if(!intro)
        {
            if(trap)
            {
                addNote(snare,38,b+2.0,.12,drop?114:104);
                if(drop && random01(seed,1200+bs)>.55f) addNote(snare,39,b+1.75,.07,72);
            }
            else
            {
                addNote(snare,38,b+1.0,.12,98+(drop?12:0));
                addNote(snare,38,b+3.0,.12,102+(drop?10:0));
                if((bar+groove)%4==2) addNote(snare,39,b+2.75,.07,64+(int)(random01(seed,1210+bs)*18.f));
            }
        }

        const int hatSteps = drop ? (hatMode==2?16:8) : ((build||energetic)?8:4);
        for(int h=0;h<hatSteps;++h)
        {
            const bool strong=(h%(hatSteps/4)==0);
            if(!strong && random01(seed,1300+bs+h)<(drop?.08f:.16f)) continue;
            double beat=b+h*(4.0/hatSteps);
            if(h%2==1) beat+=swing*(hatMode==1?1.0:.55);
            const bool open = drop && ((h+groove)%8==3 || (hatMode==2 && h%8==7));
            const int vel=juce::jlimit(36,112,52+(strong?15:0)+(int)(random01(seed,1400+bs+h)*34.f));
            addNote(hats,open?46:42,beat,.055+(open?.10:0.0),vel);
        }

        if(drop)
        {
            const int percCount=1+((bar+groove)%3);
            for(int k=0;k<percCount;++k)
            {
                const double pos=.5*((k*3+groove+bar)%8);
                if(pos<.1) continue;
                addNote(perc,(k%2)?37:39,b+pos,.07,62+(int)(random01(seed,1500+bs+k)*26.f));
            }
        }

        if(build && (bar==23 || bar==55))
        {
            const int divisions = hatMode==2 ? 24 : 16;
            for(int s=0;s<divisions;++s)
            {
                if(s<4 && groove==3 && s%2==1) continue;
                addNote(snare,38,b+s*(4.0/divisions),.05,
                        juce::jlimit(48,127,54+s*(64/divisions)+(int)(random01(seed,1600+s)*8.f)));
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
    ArrangementLane bass{"BASS",2,false}, chords{"CHORDS",3,false}, pluck{"PLUCK",4,false}, pad{"PAD",5,false};

    static constexpr int minorProgressions[5][4] = {
        {0,8,3,10}, {0,10,8,10}, {0,3,10,8}, {0,5,8,7}, {0,8,5,10}
    };
    static constexpr int majorProgressions[5][4] = {
        {0,7,9,5}, {0,9,5,7}, {0,5,9,7}, {9,5,0,7}, {0,4,5,7}
    };
    const int progressionIndex=static_cast<int>(random01(seed,201)*5.f)%5;
    const int* progression=minor?minorProgressions[progressionIndex]:majorProgressions[progressionIndex];
    const int third=minor?3:4;
    const int bassMode=static_cast<int>(random01(seed,202)*4.f)%4;
    const int chordMode=static_cast<int>(random01(seed,203)*4.f)%4;
    const int arpMode=static_cast<int>(random01(seed,204)*4.f)%4;
    static constexpr int arpPatterns[4][8] = {
        {0,1,2,1,0,1,2,1},
        {0,2,1,2,0,2,1,2},
        {0,1,2,0,2,1,0,2},
        {2,1,0,1,2,1,0,1}
    };

    for(int bar=0;bar<bars;++bar)
    {
        float energy=.4f;
        for(const auto& s:sections) if(sectionContains(s,bar)){energy=s.energy;break;}
        const double b=bar*beatsPerBar;
        const int root=rootMidi+progression[bar%4];
        const bool drop=energy>.8f;
        const bool breakdown=bar>=40&&bar<48;
        const bool finalDrop=bar>=56;
        const uint64_t bs=(uint64_t)bar*131ULL;

        if(energy>.3f)
        {
            if(bassMode==0 || (!drop && bassMode==1))
            {
                for(int q=0;q<4;++q)
                    if(drop || q%2==0)
                    {
                        int n=root-12;
                        if(drop && q==3 && random01(seed,3000+bs+q)>.45f) n+=(finalDrop?12:7);
                        addNote(bass,n,b+q,drop?.68:1.42,juce::jlimit(70,122,90+(drop?18:0)+(int)(random01(seed,3010+bs+q)*10.f)));
                    }
            }
            else if(bassMode==1)
            {
                static constexpr double pos[6]={0.0,.75,1.5,2.0,2.75,3.5};
                for(int i=0;i<(drop?6:4);++i)
                {
                    int n=root-12;
                    if(i==2||i==5) n+=7;
                    addNote(bass,n,b+pos[i],.42,82+(drop?20:0)+(i%2)*5);
                }
            }
            else if(bassMode==2)
            {
                static constexpr double pos[5]={0.0,1.0,1.75,2.5,3.25};
                for(int i=0;i<(drop?5:3);++i)
                    addNote(bass,root-12+(i==4?12:0),b+pos[i],.48,88+(drop?18:0)+(int)(random01(seed,3020+bs+i)*8.f));
            }
            else
            {
                addNote(bass,root-12,b,.82,104);
                addNote(bass,root-12,b+1.5,.42,90);
                addNote(bass,root-5,b+2.0,.72,drop?110:94);
                if(drop) addNote(bass,root,b+3.25,.38,102);
            }
        }

        const double chordLen=breakdown?3.72:(drop?(chordMode==0?.78:.46):3.55);
        const int repeats=drop?(chordMode==2?2:4):1;
        for(int r=0;r<repeats;++r)
        {
            const double cb=b+r*(4.0/repeats)+(drop&&chordMode==3&&r%2?0.12:0.0);
            int notes[3]={root+12,root+12+third,root+19};
            const int inversion=(bar+r+progressionIndex+chordMode)%3;
            if(inversion>=1) notes[0]+=12;
            if(inversion>=2) notes[1]+=12;
            std::sort(notes,notes+3);
            const int baseVel=68+(int)(energy*24.f)+(int)(random01(seed,3100+bs+r)*8.f);
            addNote(chords,notes[0],cb,chordLen,juce::jlimit(50,118,baseVel));
            addNote(chords,notes[1],cb,chordLen,juce::jlimit(50,118,baseVel-3));
            addNote(chords,notes[2],cb,chordLen,juce::jlimit(50,118,baseVel-5));
            if(finalDrop && (r%2==0 || chordMode==1))
                addNote(chords,notes[2]+12,cb,chordLen*.82,juce::jlimit(45,108,baseVel-12));
        }

        if(bar<24 || breakdown)
        {
            const int padOct=(static_cast<int>(random01(seed,3200+bar/4)*2.f))*12;
            const double start=b+(random01(seed,3210+bar)>.72f?.25:0.0);
            const double len=3.45+random01(seed,3220+bar)*.38;
            addNote(pad,root+12+padOct,start,len,54+(int)(random01(seed,3230+bar)*10.f));
            addNote(pad,root+12+third+padOct,start,len,50+(int)(random01(seed,3240+bar)*9.f));
            addNote(pad,root+19+padOct,start,len,48+(int)(random01(seed,3250+bar)*9.f));
        }

        if(energy>.5f&&!breakdown)
        {
            const int chordTones[3]={0,third,7};
            for(int e=0;e<8;++e)
            {
                if(e>0 && random01(seed,3300+bs+e)<(drop?.08f:.18f)) continue;
                const int toneIndex=arpPatterns[arpMode][(e+bar+progressionIndex)%8];
                int note=root+24+chordTones[toneIndex];
                if(finalDrop && e%4==3 && random01(seed,3310+bs+e)>.45f) note+=12;
                const double pos=b+e*.5+((e%2)?(random01(seed,3320+bs+e)-.5)*.035:0.0);
                addNote(pluck,note,pos,.15+random01(seed,3330+bs+e)*.18,
                        62+(int)(energy*20.f)+(int)(random01(seed,3340+bs+e)*12.f));
            }
        }
    }

    lanes.push_back(std::move(bass));
    lanes.push_back(std::move(chords));
    lanes.push_back(std::move(pluck));
    lanes.push_back(std::move(pad));
}

void SongArrangement::addMelody(uint64_t seed, bool energetic)
{
    ArrangementLane lead{"LEAD",1,false}, counter{"COUNTER",6,false};
    const int scaleMinor[] = {0,2,3,5,7,8,10,12};
    const int scaleMajor[] = {0,2,4,5,7,9,11,12};
    const int* scale = minor ? scaleMinor : scaleMajor;

    // Build two genuinely different seed-driven phrases instead of rotating one fixed motif.
    // The prompt still controls key/style/energy, while the generation seed controls melodic identity.
    int phraseA[8]{}, phraseB[8]{}, counterPhrase[8]{};
    int degreeA = static_cast<int>(random01(seed, 1001) * 7.f);
    int degreeB = static_cast<int>(random01(seed, 1002) * 7.f);
    static constexpr int moves[] = {-3,-2,-1,-1,0,1,1,2,3};

    for (int i = 0; i < 8; ++i)
    {
        const int moveA = moves[juce::jlimit(0, 8, static_cast<int>(random01(seed, 1100 + i) * 9.f))];
        const int moveB = moves[juce::jlimit(0, 8, static_cast<int>(random01(seed, 1200 + i) * 9.f))];
        degreeA = juce::jlimit(0, 7, degreeA + moveA);
        degreeB = juce::jlimit(0, 7, degreeB + moveB);
        phraseA[i] = degreeA;
        phraseB[i] = degreeB;

        // Counter melody has its own contour and is not just a transposed lead.
        const int counterBase = static_cast<int>(random01(seed, 1300 + i) * 7.f);
        counterPhrase[i] = juce::jlimit(0, 7, counterBase + ((i % 3) - 1));
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        float energy=.4f;
        for (const auto& s : sections) if(sectionContains(s,bar)){energy=s.energy;break;}

        const bool leadActive = (bar >= 12 && bar < 40) || bar >= 48;
        if (!leadActive) continue;

        const double b = bar * beatsPerBar;
        const bool finalDrop = bar >= 56;
        const bool drop = energy > .8f;
        const int steps = (drop || energetic) ? 8 : 4;
        const double step = 4.0 / steps;
        const int phraseGroup = bar / 4;
        const int rotation = static_cast<int>(random01(seed, 2000 + phraseGroup) * 8.f) % 8;
        const bool useB = ((phraseGroup + static_cast<int>(random01(seed, 2050 + phraseGroup) * 3.f)) % 2) != 0;
        const int* phrase = useB ? phraseB : phraseA;

        for (int i = 0; i < steps; ++i)
        {
            const uint64_t salt = static_cast<uint64_t>(bar * 64 + i);
            const bool strongBeat = i == 0 || i == steps / 2;
            const float restChance = drop ? .10f : .22f;
            if (!strongBeat && random01(seed, 3000 + salt) < restChance) continue;

            int degree = phrase[(i + rotation) % 8];
            if (random01(seed, 4000 + salt) > .72f)
            {
                const int nudge = random01(seed, 4100 + salt) > .5f ? 1 : -1;
                degree = juce::jlimit(0, 7, degree + nudge);
            }

            int octave = drop ? 24 : 12;
            if (finalDrop && random01(seed, 4200 + salt) > .78f) octave += 12;
            if (!drop && random01(seed, 4250 + salt) < .13f) octave -= 12;

            const int note = rootMidi + octave + scale[degree];
            const double length = juce::jlimit(.16, .92,
                step * (.46 + .72 * random01(seed, 5000 + salt)));
            const int velocity = juce::jlimit(58, 127,
                78 + static_cast<int>(energy * 30.f)
                + static_cast<int>(random01(seed, 5100 + salt) * 16.f) - 8);

            const double timing = (i % 2 == 1)
                ? (random01(seed, 5200 + salt) - .5) * .035
                : 0.0;
            addNote(lead, note, juce::jmax(b, b + i * step + timing), length, velocity);
        }

        // Phrase ending/fill varies by 4/8-bar position so drops evolve instead of looping literally.
        if ((bar % 4) == 3)
        {
            const int fillDegree = finalDrop
                ? phraseB[static_cast<int>(random01(seed, 6000 + bar) * 8.f) % 8]
                : phraseA[static_cast<int>(random01(seed, 6100 + bar) * 8.f) % 8];
            const int fillOctave = drop ? 24 : 12;
            addNote(lead, rootMidi + fillOctave + scale[fillDegree],
                    b + 3.5, .34, finalDrop ? 119 : 103);
        }

        // Independent counter line appears selectively in high-energy sections.
        if (drop && (bar % 2 == 1) && random01(seed, 7000 + bar) > .18f)
        {
            const int counterRotation = static_cast<int>(random01(seed, 7100 + bar) * 8.f) % 8;
            for (int i = 0; i < 4; ++i)
            {
                if (i > 0 && random01(seed, 7200 + bar * 8 + i) < .20f) continue;
                const int degree = counterPhrase[(i + counterRotation) % 8];
                const int octave = finalDrop ? 24 : 12;
                const int velocity = juce::jlimit(52, 104,
                    62 + static_cast<int>(random01(seed, 7300 + bar * 8 + i) * 24.f));
                addNote(counter, rootMidi + octave + scale[degree],
                        b + 2.0 + i * .5,
                        .18 + .18 * random01(seed, 7400 + bar * 8 + i),
                        velocity);
            }
        }
    }

    lanes.push_back(std::move(lead));
    lanes.push_back(std::move(counter));
}

void SongArrangement::addFx(uint64_t seed)
{
    ArrangementLane fx{"FX / TRANSITIONS",7,false};
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