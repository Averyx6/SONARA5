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

void SongArrangement::buildSections()
{
    sections = {
        {"INTRO",        0,  8, .22f},
        {"VERSE / BREAK",8,  8, .38f},
        {"BUILD 1",     16,  8, .62f},
        {"DROP 1",      24, 16, .95f},
        {"BREAKDOWN",   40,  8, .32f},
        {"BUILD 2",     48,  8, .72f},
        {"FINAL DROP",  56, 16, 1.00f}
    };
}

void SongArrangement::generate(const juce::String& prompt, double bpm, uint64_t seed)
{
    clear();
    sourcePrompt = prompt;
    tempo = juce::jlimit(60.0, 200.0, bpm);
    rootMidi = parseRootMidi(prompt, minor);
    bars = defaultBars;
    buildSections();

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
        if (lane.drums) soundPrompt += "tight punchy drum transient dry";
        else if (lane.name == "BASS") soundPrompt += "deep controlled bass mono tight";
        else if (lane.name == "CHORDS" || lane.name == "PAD") soundPrompt += "warm wide lush pad";
        else if (lane.name == "LEAD") soundPrompt += "memorable emotional lead wide powerful";
        else if (lane.name == "PLUCK") soundPrompt += "bright pluck short punchy";
        else soundPrompt += "clean atmospheric texture";
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
    const int minorProg[4] = {0, 8, 3, 10}; // i - VI - III - VII
    const int majorProg[4] = {0, 7, 9, 5};  // I - V - vi - IV
    const int* progression = minor ? minorProg : majorProg;
    const int third = minor ? 3 : 4;

    for (int bar = 0; bar < bars; ++bar)
    {
        float energy = .4f;
        for (const auto& s : sections) if (sectionContains(s, bar)) { energy=s.energy; break; }
        const double b = bar * beatsPerBar;
        const int root = rootMidi + progression[bar % 4];
        const bool drop = energy > .8f;
        const bool breakdown = bar >= 40 && bar < 48;

        if (energy > .3f)
        {
            for (int q = 0; q < 4; ++q)
            {
                if (drop || q % 2 == 0)
                    addNote(bass, root - 12 + (drop && q == 3 && random01(seed, bar) > .55f ? 12 : 0), b + q, drop ? .76 : 1.55, drop ? 110 : 94);
            }
        }
        const double chordLen = breakdown ? 3.75 : (drop ? .82 : 3.7);
        const int repeats = drop ? 4 : 1;
        for (int r = 0; r < repeats; ++r)
        {
            const double cb = b + r * (4.0 / repeats);
            addNote(chords, root + 12, cb, chordLen, 76 + static_cast<int>(energy * 20));
            addNote(chords, root + 12 + third, cb, chordLen, 72 + static_cast<int>(energy * 18));
            addNote(chords, root + 19, cb, chordLen, 70 + static_cast<int>(energy * 18));
        }
        if (bar < 24 || breakdown)
        {
            addNote(pad, root + 12, b, 3.8, 58);
            addNote(pad, root + 12 + third, b, 3.8, 54);
            addNote(pad, root + 19, b, 3.8, 54);
        }
        if (energy > .5f && !breakdown)
            for (int e = 0; e < 8; ++e)
                addNote(pluck, root + 24 + ((e % 3 == 1) ? third : (e % 3 == 2 ? 7 : 0)), b + e * .5, .22, 70 + (e % 4) * 5);
    }
    lanes.push_back(std::move(bass)); lanes.push_back(std::move(chords)); lanes.push_back(std::move(pluck)); lanes.push_back(std::move(pad));
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
    for (const auto& s : sections)
    {
        if (s.startBar > 0) addNote(fx, 84 + static_cast<int>(random01(seed,s.startBar)*5.f), s.startBar*beatsPerBar - .5, .45, 72);
        if (s.energy > .75f) addNote(fx, 48, s.startBar*beatsPerBar, .25, 95);
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
    auto st=root.getChildWithName("SECTIONS");for(int i=0;i<st.getNumChildren();++i){auto v=st.getChild(i);a.sections.push_back({v.getProperty("name","").toString(),(int)v.getProperty("startBar",0),(int)v.getProperty("bars",8),(float)v.getProperty("energy",.5)});}if(a.sections.empty())a.buildSections();
    auto lt=root.getChildWithName("LANES");for(int i=0;i<lt.getNumChildren();++i){auto l=lt.getChild(i);ArrangementLane lane;lane.name=l.getProperty("name","Lane").toString();lane.midiChannel=juce::jlimit(1,16,(int)l.getProperty("channel",1));lane.drums=(bool)l.getProperty("drums",false);auto dna=l.getChildWithName("SoundDNA");if(dna.isValid())lane.sound=SoundDNA::fromValueTree(dna);auto notes=l.getChildWithName("NOTES");for(int j=0;j<notes.getNumChildren();++j){auto n=notes.getChild(j);lane.notes.push_back({juce::jlimit(0,127,(int)n.getProperty("note",60)),juce::jlimit(1,127,(int)n.getProperty("velocity",100)),juce::jmax(0.0,(double)n.getProperty("beat",0.0)),juce::jmax(.03,(double)n.getProperty("length",.5))});}std::sort(lane.notes.begin(),lane.notes.end(),[](const ArrangementNote&x,const ArrangementNote&y){return x.beat<y.beat;});a.lanes.push_back(std::move(lane));}
    return a;
}

} // namespace sonara