#include "PromptGenerator.h"
#include <algorithm>

namespace sonara {
float PromptGenerator::hash01(uint64_t x, uint64_t salt) {
    x += salt + 0x9e3779b97f4a7c15ULL; x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL; x ^= x >> 31;
    return static_cast<float>(x & 0xffffff) / static_cast<float>(0xffffff);
}
SoundDNA PromptGenerator::generate(const juce::String& prompt, uint64_t seed, Progress cb) const {
    SoundDNA d; d.seed=seed; d.sourcePrompt=prompt; d.name="Generated";
    const auto p=prompt.toLowerCase(); const auto has=[&p](const char* word){return p.contains(word);};
    if(cb) cb(.10f,"Understanding prompt");
    d.oscA=hash01(seed,20)>.72f?WaveShape::softSaw:WaveShape::saw; d.oscB=hash01(seed,21)>.68f?WaveShape::square:WaveShape::softSaw;
    d.oscMix=.18f+hash01(seed,2)*.64f; d.oscBTranspose=hash01(seed,22)>.78f?12.f:0.f; d.oscAMorph=.04f+hash01(seed,46)*.28f; d.oscBMorph=.04f+hash01(seed,47)*.32f;
    d.unison=3+(int)(hash01(seed,23)*4.f); d.detune=.04f+hash01(seed,1)*.22f; d.unisonBlend=.58f+hash01(seed,24)*.3f; d.phaseRandom=.35f+hash01(seed,25)*.6f;
    if(cb) cb(.28f,"Designing oscillators and morphs");
    d.attack=.002f+hash01(seed,3)*.07f; d.decay=.16f+hash01(seed,26)*.42f; d.sustain=.58f+hash01(seed,27)*.34f; d.release=.14f+hash01(seed,4);
    d.filterMode=FilterMode::lowpass; d.cutoff=3000.f+hash01(seed,5)*14000.f; d.resonance=.05f+hash01(seed,6)*.28f; d.filterEnv=.05f+hash01(seed,28)*.34f;
    d.lfoRate=.12f+hash01(seed,29)*2.8f; d.lfoCutoff=(hash01(seed,30)-.5f)*.22f; d.lfoPitch=0.f; d.lfoMorphA=(hash01(seed,55)-.5f)*.16f; d.lfoMorphB=(hash01(seed,56)-.5f)*.18f;
    // Sine is the neutral/default modulation shape. Musical intent below can select more characterful shapes.
    d.lfoShape=LfoShape::sine;
    d.subLevel=.02f+hash01(seed,31)*.12f; d.subOctave=-1.f; d.noiseLevel=hash01(seed,32)*.035f;
    d.pitchEnv=0.f; d.pitchEnvDecay=.12f; d.transientLevel=.01f+hash01(seed,80)*.04f; d.transientDecay=.012f+.025f*hash01(seed,81);
    d.fmAmount=hash01(seed,82)*.08f; d.fmRatio=1.f+std::floor(hash01(seed,83)*4.f); d.ringMod=hash01(seed,84)*.04f; d.bitCrush=0.f; d.downsample=0.f;

    // Seeded base character gives repeated prompts a genuinely different sonic identity
    // before explicit prompt words refine it. The variants stay musical and bounded.
    const int character=(int)(hash01(seed,96)*6.f)%6;
    if(character==0){d.oscA=WaveShape::softSaw;d.oscB=WaveShape::triangle;d.oscMix=.28f+.18f*hash01(seed,97);d.drive=.05f+.08f*hash01(seed,98);d.detune=.055f+.07f*hash01(seed,99);}
    else if(character==1){d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.34f;d.fmAmount=.08f+.16f*hash01(seed,100);d.fmRatio=2.f+std::floor(hash01(seed,101)*3.f);d.transientLevel=.05f+.08f*hash01(seed,102);}
    else if(character==2){d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.22f+.24f*hash01(seed,103);d.drive=.11f+.12f*hash01(seed,104);d.filterEnv=.14f+.24f*hash01(seed,105);}
    else if(character==3){d.oscA=WaveShape::triangle;d.oscB=WaveShape::softSaw;d.oscMix=.44f;d.noiseLevel=.008f+.026f*hash01(seed,106);d.transientLevel=.04f+.10f*hash01(seed,107);d.width=.50f+.34f*hash01(seed,108);}
    else if(character==4){d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.48f;d.fmAmount=.15f+.24f*hash01(seed,109);d.fmRatio=1.5f+std::floor(hash01(seed,110)*5.f);d.ringMod=.025f+.08f*hash01(seed,111);}
    else {d.oscA=WaveShape::softSaw;d.oscB=WaveShape::softSaw;d.oscMix=.50f;d.oscAMorph=.18f+.32f*hash01(seed,112);d.oscBMorph=.12f+.38f*hash01(seed,113);d.lfoMorphA=.04f+.12f*hash01(seed,114);d.lfoMorphB=-(.03f+.11f*hash01(seed,115));}

    if(has("analog")||has("vintage"))
    {
        d.oscA=WaveShape::softSaw;d.oscB=WaveShape::triangle;d.oscMix=.24f+.18f*hash01(seed,116);
        d.detune=.045f+.07f*hash01(seed,117);d.phaseRandom=.72f;d.drive=.07f+.10f*hash01(seed,118);
        d.cutoff=juce::jmin(d.cutoff,10500.f);d.noiseLevel=juce::jmax(d.noiseLevel,.008f);
    }
    if(has("glassy")||has("crystal")||has("icy"))
    {
        d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.42f;
        d.fmAmount=.22f+.28f*hash01(seed,119);d.fmRatio=2.f+std::floor(hash01(seed,120)*4.f);
        d.transientLevel=.10f+.12f*hash01(seed,121);d.cutoff=juce::jmax(d.cutoff,13500.f);
        d.width=juce::jmax(d.width,.72f);d.drive=juce::jmin(d.drive,.07f);
    }
    if(has("organic")||has("woody")||has("natural"))
    {
        d.oscA=WaveShape::triangle;d.oscB=WaveShape::softSaw;d.oscMix=.30f;
        d.transientLevel=.16f+.18f*hash01(seed,122);d.transientDecay=.012f+.035f*hash01(seed,123);
        d.noiseLevel=.018f+.035f*hash01(seed,124);d.cutoff=4200.f+5200.f*hash01(seed,125);
        d.drive=.035f+.055f*hash01(seed,126);
    }
    if(has("hollow")||has("vocal-like")||has("vocal synth"))
    {
        d.filterMode=FilterMode::bandpass;d.cutoff=900.f+3200.f*hash01(seed,127);
        d.resonance=.22f+.22f*hash01(seed,128);d.oscA=WaveShape::square;d.oscB=WaveShape::sine;
        d.oscMix=.25f+.20f*hash01(seed,129);
    }
    if(has("gritty")||has("dirty")||has("raw"))
    {
        d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.drive=.22f+.26f*hash01(seed,130);
        d.transientLevel=juce::jmax(d.transientLevel,.12f);d.cutoff=juce::jmin(d.cutoff,12500.f);
    }
    if(has("kick")||has("808")){d.name=has("808")?"Generated 808":"Generated Kick";d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.08f;d.unison=1;d.width=.02f;d.phaseRandom=0.f;d.attack=.001f;d.decay=.18f+(has("808")?.38f:.08f);d.sustain=has("808")?.22f:0.f;d.release=has("808")?.42f:.08f;d.cutoff=juce::jmin(d.cutoff,has("808")?4200.f:6500.f);d.pitchEnv=has("808")?18.f:34.f;d.pitchEnvDecay=has("808")?.095f:.055f;d.transientLevel=has("soft")?.18f:.55f;d.transientDecay=.009f;d.subLevel=has("808")?.58f:.34f;d.drive=has("clean")?.08f:.24f;d.reverb=0.f;d.delay=0.f;d.chorus=0.f;}
    if(has("snare")||has("clap")){d.name=has("clap")?"Generated Clap":"Generated Snare";d.oscA=WaveShape::triangle;d.oscB=WaveShape::sine;d.oscMix=.35f;d.unison=1;d.width=has("clap")?.68f:.25f;d.attack=.001f;d.decay=.14f;d.sustain=0.f;d.release=.08f;d.noiseLevel=has("clap")?.58f:.42f;d.transientLevel=.52f;d.transientDecay=.012f;d.pitchEnv=8.f;d.pitchEnvDecay=.035f;d.cutoff=has("clap")?9000.f:7200.f;d.resonance=.08f;d.reverb=.06f;d.delay=0.f;}
    if(has("hat")||has("cymbal")||has("ride")||has("crash")){d.name="Generated Metal Percussion";d.oscA=WaveShape::square;d.oscB=WaveShape::triangle;d.oscMix=.55f;d.unison=3;d.detune=.31f;d.attack=.001f;d.decay=has("crash")||has("ride")?.85f:.07f;d.sustain=0.f;d.release=has("crash")?.9f:.05f;d.noiseLevel=.34f;d.transientLevel=.38f;d.fmAmount=.52f;d.fmRatio=6.f;d.ringMod=.42f;d.filterMode=FilterMode::highpass;d.cutoff=has("crash")?3800.f:6500.f;d.width=.8f;d.reverb=has("crash")?.24f:.04f;}
    if(has("bell")){d.name="Generated Bell";d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.34f+.12f*hash01(seed,142);d.unison=1;d.attack=.001f;d.decay=.50f+.38f*hash01(seed,143);d.sustain=.03f+.06f*hash01(seed,144);d.release=.85f+.80f*hash01(seed,145);d.fmAmount=.42f+.30f*hash01(seed,85);d.fmRatio=2.f+std::floor(hash01(seed,86)*5.f);d.ringMod=.04f+.08f*hash01(seed,146);d.transientLevel=.16f+.12f*hash01(seed,147);d.cutoff=13500.f+3500.f*hash01(seed,148);d.reverb=.22f+.16f*hash01(seed,149);}
    if(has("metallic")&&!has("bell")){d.name="Generated Metallic";d.oscA=WaveShape::square;d.oscB=WaveShape::sine;d.oscMix=.45f+.18f*hash01(seed,150);d.unison=1+(int)(hash01(seed,151)*2.f);d.attack=.001f;d.decay=.22f+.42f*hash01(seed,152);d.sustain=.10f+.18f*hash01(seed,153);d.release=.30f+.62f*hash01(seed,154);d.fmAmount=.34f+.38f*hash01(seed,155);d.fmRatio=3.f+std::floor(hash01(seed,156)*6.f);d.ringMod=.20f+.34f*hash01(seed,157);d.transientLevel=.12f+.20f*hash01(seed,158);d.drive=.06f+.13f*hash01(seed,159);d.filterMode=FilterMode::bandpass;d.cutoff=2800.f+7200.f*hash01(seed,160);d.resonance=.14f+.22f*hash01(seed,161);}
    if(has("fm")||has("digital")){d.fmAmount=juce::jmax(d.fmAmount,.28f+.42f*hash01(seed,87));d.fmRatio=1.f+std::floor(hash01(seed,88)*7.f);}
    if(has("digital")){d.name="Generated Digital";d.oscA=WaveShape::square;d.oscB=WaveShape::sine;d.oscMix=.30f+.30f*hash01(seed,162);d.fmAmount=juce::jmax(d.fmAmount,.34f+.32f*hash01(seed,163));d.fmRatio=2.f+std::floor(hash01(seed,164)*6.f);d.phaseRandom=.12f+.28f*hash01(seed,165);d.transientLevel=juce::jmax(d.transientLevel,.08f+.12f*hash01(seed,166));d.drive=.025f+.09f*hash01(seed,167);}
    if(has("pure sub")||has("clean sub")||has("sine sub")){d.name="Generated Sub";d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.05f;d.oscBTranspose=0.f;d.unison=1;d.detune=0.f;d.phaseRandom=0.f;d.width=0.f;d.attack=.004f;d.decay=.08f;d.sustain=.94f;d.release=.14f;d.filterMode=FilterMode::lowpass;d.cutoff=115.f;d.resonance=.06f;d.filterEnv=0.f;d.subLevel=0.f;d.noiseLevel=0.f;d.fmAmount=0.f;d.ringMod=0.f;d.drive=.018f;d.chorus=0.f;d.reverb=0.f;d.delay=0.f;}
    if(has("ring mod")||has("inharmonic")){d.ringMod=.32f+.46f*hash01(seed,89);}
    if(has("bitcrush")||has("8-bit")||has("lofi")||has("lo-fi")){d.bitCrush=.28f+.48f*hash01(seed,90);d.downsample=.12f+.38f*hash01(seed,91);}
    if(has("punch")||has("transient")||has("impact")){d.transientLevel=juce::jmax(d.transientLevel,.25f+.35f*hash01(seed,92));d.transientDecay=.006f+.025f*hash01(seed,93);}
    if(has("pitch drop")||has("descending")||has("drop pitch")){d.pitchEnv=20.f+20.f*hash01(seed,94);d.pitchEnvDecay=.04f+.11f*hash01(seed,95);}
    if(has("pluck")){d.attack=.001f;d.decay=.075f+hash01(seed,33)*.13f;d.sustain=.06f+.08f*hash01(seed,131);d.release=.09f+.18f*hash01(seed,34);d.filterEnv=.48f+.24f*hash01(seed,132);d.noiseLevel=.008f+.024f*hash01(seed,133);d.transientLevel=juce::jmax(d.transientLevel,.12f+.15f*hash01(seed,134));d.oscAMorph=.06f+.28f*hash01(seed,48);d.lfoMorphA*=.30f;d.lfoMorphB*=.30f;}
    if(has("pad")||has("ambient")){d.attack=.28f+.35f*hash01(seed,35);d.release=1.4f+1.2f*hash01(seed,36);d.sustain=.82f;d.width=.96f;d.lfoCutoff=.16f;d.lfoRate=.16f+.28f*hash01(seed,37);d.lfoShape=LfoShape::triangle;d.oscAMorph=.30f+.38f*hash01(seed,49);d.oscBMorph=.25f+.42f*hash01(seed,50);d.lfoMorphA=.18f+.22f*hash01(seed,57);d.lfoMorphB=-(.14f+.24f*hash01(seed,58));}
    if(has("bass")||has("sub")){d.cutoff=1800.f+2200.f*hash01(seed,38);d.release=.12f+.16f*hash01(seed,39);d.width=.10f;d.unison=has("reese")?5:1;d.subLevel=has("sub")?.48f:.28f;d.oscBTranspose=-12.f;d.reverb=.04f;d.oscAMorph=has("reese")?.48f:.08f;d.lfoMorphA=has("reese")?.16f:0.f;d.lfoMorphB=has("reese")?-.12f:0.f;}
    if(has("reese")){d.name="Generated Reese";d.oscA=WaveShape::saw;d.oscB=WaveShape::softSaw;d.oscMix=.42f+.16f*hash01(seed,168);d.unison=4+(int)(hash01(seed,169)*3.f);d.detune=.075f+.10f*hash01(seed,170);d.width=.22f+.22f*hash01(seed,171);d.drive=.12f+.18f*hash01(seed,172);d.cutoff=900.f+2600.f*hash01(seed,173);d.lfoCutoff=.18f+.28f*hash01(seed,174);d.lfoRate=.18f+1.20f*hash01(seed,175);d.subLevel=.18f+.18f*hash01(seed,176);}
    if(has("lead")){d.attack=.002f+.006f*hash01(seed,135);d.sustain=.70f+.18f*hash01(seed,136);d.release=.16f+.24f*hash01(seed,137);d.unison=4+(int)(hash01(seed,138)*3.f);d.filterEnv=.12f+.20f*hash01(seed,139);d.oscAMorph=.06f+.38f*hash01(seed,51);d.oscBMorph=.04f+.34f*hash01(seed,140);d.lfoMorphA*=.58f;d.lfoMorphB*=.58f;d.transientLevel=juce::jmax(d.transientLevel,.045f+.08f*hash01(seed,141));}
    if(has("supersaw")){d.oscA=WaveShape::saw;d.oscB=WaveShape::softSaw;d.oscAMorph=d.oscBMorph=d.lfoMorphA=d.lfoMorphB=0.f;d.unison=9;d.detune=.14f+.1f*hash01(seed,40);d.unisonBlend=.78f;d.width=.98f;}
    // Semantic instrument families. These are synthesized approximations, but they
    // give the prompt a much larger vocabulary than the old lead/pluck/bass-only
    // keyword set. Compound descriptions still combine deterministically.
    const bool festival=has("festival")||has("mainstage")||has("big room")||has("anthemic")||has("arena");
    const bool leadRole=has("lead")||has("hook")||has("solo");
    const bool pluckRole=has("pluck")||has("pizzicato");
    const bool chordRole=has("chord")||has("stab");
    const bool brassRole=has("brass")||has("horn")||has("trumpet");
    const bool stringRole=has("strings")||has("string ensemble")||has("violin")||has("cello");
    const bool keysRole=has("piano")||has("keys")||has("keyboard");
    const bool organRole=has("organ");
    const bool fluteRole=has("flute")||has("whistle")||has("woodwind");
    const bool choirRole=has("choir")||has("choir-like")||has("vocal pad");
    const bool guitarRole=has("guitar")||has("picked string");
    const bool malletRole=has("marimba")||has("xylophone")||has("mallet");
    const bool growlRole=has("growl")||has("neuro")||has("monster bass");
    const bool reedRole=has("sax")||has("saxophone")||has("reed")||has("clarinet");
    const bool harpRole=has("harp");
    const bool kalimbaRole=has("kalimba")||has("thumb piano");
    const bool acidRole=has("acid bass")||has("acid lead")||has("303");
    const bool psyRole=has("psy bass")||has("psytrance bass");
    const bool screechRole=has("screech")||has("hardstyle lead")||has("hardstyle screech");
    const bool vocalRole=has("vocal synth")||has("formant lead")||has("voice-like")||has("vocal-like");
    const bool chipRole=has("chiptune")||has("chip lead")||has("retro game");
    const bool raveRole=has("future rave")||has("rave stab")||has("rave lead");
    const bool donkRole=has("donk")||has("bounce bass")||has("bouncy bass");
    const bool glassRole=has("glassy")||has("glass pluck")||has("crystal pluck");
    const bool laserRole=has("laser")||has("zap")||has("zappy");
    const bool wubRole=has("wub")||has("wobble bass")||has("dubstep wobble");

    if(brassRole)
    {
        d.name="Generated Synth Brass";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.30f;
        d.unison=3+(int)(hash01(seed,180)*3.f);d.detune=.045f+.055f*hash01(seed,181);
        d.attack=.012f+.025f*hash01(seed,182);d.decay=.18f;d.sustain=.76f;d.release=.20f+.18f*hash01(seed,183);
        d.filterMode=FilterMode::lowpass;d.cutoff=3600.f+5200.f*hash01(seed,184);d.filterEnv=.34f+.30f*hash01(seed,185);
        d.drive=.10f+.12f*hash01(seed,186);d.transientLevel=juce::jmax(d.transientLevel,.08f);
    }
    if(stringRole)
    {
        d.name="Generated String Ensemble";d.oscA=WaveShape::softSaw;d.oscB=WaveShape::triangle;d.oscMix=.38f;
        d.unison=5+(int)(hash01(seed,187)*3.f);d.detune=.035f+.055f*hash01(seed,188);d.width=.88f;
        d.attack=.12f+.28f*hash01(seed,189);d.decay=.35f;d.sustain=.86f;d.release=.75f+1.0f*hash01(seed,190);
        d.cutoff=5200.f+5200.f*hash01(seed,191);d.lfoPitch=.035f+.035f*hash01(seed,192);d.lfoRate=4.2f+1.2f*hash01(seed,193);
    }
    if(keysRole)
    {
        d.name="Generated Synth Keys";d.oscA=WaveShape::triangle;d.oscB=WaveShape::sine;d.oscMix=.32f;
        d.unison=1+(int)(hash01(seed,194)*2.f);d.attack=.001f;d.decay=.42f+.45f*hash01(seed,195);
        d.sustain=.30f+.22f*hash01(seed,196);d.release=.35f+.45f*hash01(seed,197);
        d.transientLevel=.18f+.18f*hash01(seed,198);d.transientDecay=.012f+.018f*hash01(seed,199);
        d.fmAmount=.08f+.12f*hash01(seed,200);d.cutoff=5200.f+6500.f*hash01(seed,201);
    }
    if(organRole)
    {
        d.name="Generated Organ";d.oscA=WaveShape::sine;d.oscB=WaveShape::square;d.oscMix=.28f;
        d.unison=2;d.detune=.018f;d.attack=.006f;d.decay=.08f;d.sustain=.96f;d.release=.18f;
        d.filterEnv=0.f;d.cutoff=7600.f;d.fmAmount=.06f;d.width=.62f;
    }
    if(fluteRole)
    {
        d.name="Generated Air Flute";d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.18f;
        d.unison=1;d.attack=.035f+.035f*hash01(seed,202);d.decay=.14f;d.sustain=.82f;d.release=.30f;
        d.noiseLevel=.025f+.025f*hash01(seed,203);d.filterMode=FilterMode::lowpass;d.cutoff=5200.f+2200.f*hash01(seed,204);
        d.lfoPitch=.055f;d.lfoRate=4.8f+.9f*hash01(seed,205);d.width=.36f;
    }
    if(choirRole)
    {
        d.name="Generated Choir Pad";d.oscA=WaveShape::triangle;d.oscB=WaveShape::softSaw;d.oscMix=.42f;
        d.unison=5;d.detune=.055f;d.attack=.24f;d.decay=.48f;d.sustain=.90f;d.release=1.35f;
        d.filterMode=FilterMode::bandpass;d.cutoff=1350.f+1300.f*hash01(seed,206);d.resonance=.24f;
        d.width=.96f;d.lfoMorphA=.09f;d.lfoMorphB=-.07f;
    }
    if(guitarRole)
    {
        d.name="Generated Picked String";d.oscA=WaveShape::triangle;d.oscB=WaveShape::softSaw;d.oscMix=.24f;
        d.unison=2;d.attack=.001f;d.decay=.20f+.24f*hash01(seed,207);d.sustain=.18f;d.release=.28f;
        d.transientLevel=.24f+.16f*hash01(seed,208);d.transientDecay=.010f;d.noiseLevel=.012f;
        d.cutoff=4700.f+4600.f*hash01(seed,209);d.filterEnv=.28f;
    }
    if(malletRole)
    {
        d.name="Generated Mallet";d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.22f;
        d.unison=1;d.attack=.001f;d.decay=.24f+.34f*hash01(seed,210);d.sustain=.03f;d.release=.22f+.28f*hash01(seed,211);
        d.transientLevel=.28f+.18f*hash01(seed,212);d.transientDecay=.008f;d.fmAmount=.12f+.18f*hash01(seed,213);
        d.fmRatio=2.f+std::floor(hash01(seed,214)*3.f);d.cutoff=6500.f+5200.f*hash01(seed,215);
    }
    if(growlRole)
    {
        d.name="Generated Growl Bass";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.46f;
        d.unison=2+(int)(hash01(seed,216)*3.f);d.detune=.05f+.08f*hash01(seed,217);d.oscBTranspose=-12.f;
        d.fmAmount=.34f+.28f*hash01(seed,218);d.fmRatio=1.f+std::floor(hash01(seed,219)*4.f);
        d.ringMod=.12f+.18f*hash01(seed,220);d.drive=.28f+.22f*hash01(seed,221);
        d.filterMode=FilterMode::bandpass;d.cutoff=700.f+1800.f*hash01(seed,222);d.resonance=.25f+.18f*hash01(seed,223);
        d.lfoCutoff=.45f+.30f*hash01(seed,224);d.lfoRate=.45f+2.3f*hash01(seed,225);d.width=.18f;
    }

    if(reedRole)
    {
        d.name="Generated Reed";d.oscA=WaveShape::saw;d.oscB=WaveShape::triangle;d.oscMix=.26f;
        d.unison=1;d.width=.28f;d.attack=.018f+.022f*hash01(seed,249);d.decay=.16f;d.sustain=.82f;d.release=.24f;
        d.filterMode=FilterMode::bandpass;d.cutoff=1150.f+1700.f*hash01(seed,250);d.resonance=.30f+.14f*hash01(seed,251);
        d.noiseLevel=.015f+.018f*hash01(seed,252);d.lfoPitch=.045f+.025f*hash01(seed,253);d.lfoRate=4.3f+1.4f*hash01(seed,254);
    }
    if(harpRole)
    {
        d.name="Generated Harp";d.oscA=WaveShape::triangle;d.oscB=WaveShape::sine;d.oscMix=.24f;
        d.unison=2;d.detune=.012f;d.attack=.001f;d.decay=.42f+.44f*hash01(seed,255);d.sustain=.06f;d.release=.42f+.38f*hash01(seed,256);
        d.transientLevel=.20f+.18f*hash01(seed,257);d.transientDecay=.008f;d.cutoff=7200.f+6500.f*hash01(seed,258);d.width=.72f;
    }
    if(kalimbaRole)
    {
        d.name="Generated Kalimba";d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.18f;
        d.unison=1;d.attack=.001f;d.decay=.22f+.20f*hash01(seed,259);d.sustain=.025f;d.release=.20f+.22f*hash01(seed,260);
        d.transientLevel=.34f+.16f*hash01(seed,261);d.transientDecay=.006f;d.fmAmount=.18f+.18f*hash01(seed,262);
        d.fmRatio=3.f+std::floor(hash01(seed,263)*3.f);d.cutoff=6900.f+4800.f*hash01(seed,264);
    }
    if(acidRole)
    {
        d.name="Generated Acid";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.22f;
        d.unison=1;d.width=.06f;d.attack=.001f;d.decay=.11f+.12f*hash01(seed,265);d.sustain=.42f;d.release=.10f;
        d.filterMode=FilterMode::lowpass;d.cutoff=550.f+1550.f*hash01(seed,266);d.resonance=.48f+.22f*hash01(seed,267);
        d.filterEnv=.68f+.22f*hash01(seed,268);d.drive=.16f+.18f*hash01(seed,269);d.lfoCutoff=.10f+.18f*hash01(seed,270);
    }
    if(psyRole)
    {
        d.name="Generated Psy Bass";d.oscA=WaveShape::saw;d.oscB=WaveShape::triangle;d.oscMix=.18f;
        d.unison=1;d.width=.03f;d.oscBTranspose=-12.f;d.attack=.001f;d.decay=.075f+.050f*hash01(seed,271);
        d.sustain=.18f;d.release=.055f;d.filterMode=FilterMode::lowpass;d.cutoff=1250.f+1900.f*hash01(seed,272);
        d.filterEnv=.50f+.24f*hash01(seed,273);d.drive=.12f+.12f*hash01(seed,274);d.subLevel=.26f+.12f*hash01(seed,275);
    }
    if(screechRole)
    {
        d.name="Generated Screech";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.48f;
        d.unison=4+(int)(hash01(seed,276)*3.f);d.detune=.10f+.11f*hash01(seed,277);d.width=.92f;
        d.fmAmount=.25f+.30f*hash01(seed,278);d.ringMod=.12f+.22f*hash01(seed,279);d.drive=.34f+.24f*hash01(seed,280);
        d.filterMode=FilterMode::bandpass;d.cutoff=1700.f+4200.f*hash01(seed,281);d.resonance=.34f+.18f*hash01(seed,282);
        d.attack=.003f;d.release=.18f;d.lfoCutoff=.22f+.24f*hash01(seed,283);
    }
    if(vocalRole)
    {
        d.name="Generated Formant";d.oscA=WaveShape::triangle;d.oscB=WaveShape::saw;d.oscMix=.36f;
        d.unison=2+(int)(hash01(seed,284)*2.f);d.detune=.025f;d.width=.66f;d.attack=.018f;d.release=.28f;
        d.filterMode=FilterMode::bandpass;d.cutoff=850.f+2300.f*hash01(seed,285);d.resonance=.38f+.16f*hash01(seed,286);
        d.lfoCutoff=.16f+.20f*hash01(seed,287);d.lfoMorphA=.12f+.18f*hash01(seed,288);d.lfoMorphB=-(.10f+.18f*hash01(seed,289));
    }
    if(chipRole)
    {
        d.name="Generated Chip";d.oscA=WaveShape::square;d.oscB=WaveShape::square;d.oscMix=.35f;
        d.unison=1;d.phaseRandom=0.f;d.width=.08f;d.attack=.001f;d.decay=.12f;d.sustain=.70f;d.release=.08f;
        d.bitCrush=.34f+.26f*hash01(seed,290);d.downsample=.12f+.20f*hash01(seed,291);d.reverb=.025f;d.delay=.035f;
    }

    if(raveRole)
    {
        d.name="Generated Future Rave";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.34f;
        d.unison=5+(int)(hash01(seed,294)*3.f);d.detune=.07f+.07f*hash01(seed,295);d.width=.88f;
        d.attack=.001f;d.decay=.16f+.12f*hash01(seed,296);d.sustain=.50f;d.release=.14f+.10f*hash01(seed,297);
        d.filterMode=FilterMode::bandpass;d.cutoff=1600.f+3200.f*hash01(seed,298);d.resonance=.28f+.14f*hash01(seed,299);
        d.drive=.20f+.16f*hash01(seed,300);d.transientLevel=.16f+.12f*hash01(seed,301);
    }
    if(donkRole)
    {
        d.name="Generated Donk Bass";d.oscA=WaveShape::sine;d.oscB=WaveShape::triangle;d.oscMix=.26f;
        d.unison=1;d.width=.03f;d.attack=.001f;d.decay=.11f+.08f*hash01(seed,302);d.sustain=.08f;d.release=.08f;
        d.pitchEnv=18.f+12.f*hash01(seed,303);d.pitchEnvDecay=.035f+.035f*hash01(seed,304);
        d.transientLevel=.36f+.20f*hash01(seed,305);d.transientDecay=.006f;d.drive=.10f+.10f*hash01(seed,306);
        d.subLevel=.18f+.10f*hash01(seed,307);d.cutoff=1600.f+1800.f*hash01(seed,308);
    }
    if(glassRole)
    {
        d.name="Generated Glass Pluck";d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.26f;
        d.unison=2;d.detune=.018f;d.width=.78f;d.attack=.001f;d.decay=.28f+.24f*hash01(seed,309);
        d.sustain=.035f;d.release=.36f+.32f*hash01(seed,310);d.fmAmount=.34f+.26f*hash01(seed,311);
        d.fmRatio=4.f+std::floor(hash01(seed,312)*4.f);d.transientLevel=.22f+.14f*hash01(seed,313);
        d.cutoff=10500.f+6000.f*hash01(seed,314);d.reverb=.16f+.12f*hash01(seed,315);
    }
    if(laserRole)
    {
        d.name="Generated Laser";d.oscA=WaveShape::sine;d.oscB=WaveShape::square;d.oscMix=.20f;
        d.unison=1;d.width=.35f;d.attack=.001f;d.decay=.08f+.06f*hash01(seed,316);d.sustain=.02f;d.release=.08f;
        d.pitchEnv=30.f+16.f*hash01(seed,317);d.pitchEnvDecay=.025f+.045f*hash01(seed,318);
        d.ringMod=.10f+.16f*hash01(seed,319);d.transientLevel=.28f+.16f*hash01(seed,320);
        d.filterMode=FilterMode::highpass;d.cutoff=2200.f+4200.f*hash01(seed,321);
    }
    if(wubRole)
    {
        d.name="Generated Wub Bass";d.oscA=WaveShape::saw;d.oscB=WaveShape::square;d.oscMix=.42f;
        d.unison=2+(int)(hash01(seed,322)*2.f);d.detune=.035f+.045f*hash01(seed,323);d.width=.14f;
        d.oscBTranspose=-12.f;d.drive=.24f+.18f*hash01(seed,324);d.filterMode=FilterMode::lowpass;
        d.cutoff=500.f+1100.f*hash01(seed,325);d.resonance=.22f+.16f*hash01(seed,326);
        d.lfoCutoff=.62f+.24f*hash01(seed,327);d.lfoRate=.35f+2.4f*hash01(seed,328);
        d.subLevel=.22f+.12f*hash01(seed,329);d.fmAmount=.08f+.14f*hash01(seed,330);
    }

    if(festival&&leadRole)
    {
        d.name="Generated Festival Lead";d.oscA=WaveShape::saw;d.oscB=WaveShape::softSaw;d.oscMix=.46f;
        d.unison=8+(int)(hash01(seed,226)*3.f);d.detune=.105f+.075f*hash01(seed,227);d.unisonBlend=.84f;d.width=.98f;
        d.attack=.002f;d.decay=.18f;d.sustain=.83f;d.release=.20f+.18f*hash01(seed,228);
        d.cutoff=10500.f+6500.f*hash01(seed,229);d.filterEnv=.16f+.16f*hash01(seed,230);
        d.transientLevel=juce::jmax(d.transientLevel,.12f);d.drive=.13f+.10f*hash01(seed,231);d.subLevel=juce::jmin(d.subLevel,.04f);
    }
    if(festival&&pluckRole)
    {
        d.name="Generated Festival Pluck";d.oscA=WaveShape::saw;d.oscB=WaveShape::triangle;d.oscMix=.30f;
        d.unison=3+(int)(hash01(seed,232)*3.f);d.detune=.055f+.055f*hash01(seed,233);d.width=.84f;
        d.attack=.001f;d.decay=.095f+.075f*hash01(seed,234);d.sustain=.05f;d.release=.12f+.12f*hash01(seed,235);
        d.filterEnv=.58f+.18f*hash01(seed,236);d.cutoff=5200.f+6200.f*hash01(seed,237);
        d.transientLevel=juce::jmax(d.transientLevel,.18f);d.drive=.08f+.08f*hash01(seed,238);
    }
    if(festival&&chordRole)
    {
        d.name="Generated Festival Chords";d.oscA=WaveShape::saw;d.oscB=WaveShape::softSaw;
        d.unison=6+(int)(hash01(seed,239)*3.f);d.detune=.075f+.055f*hash01(seed,240);d.width=.96f;
        d.attack=.006f;d.decay=.22f;d.sustain=.76f;d.release=.25f;d.cutoff=8200.f+6200.f*hash01(seed,241);
        d.drive=.08f+.08f*hash01(seed,242);
    }
    if(has("soft")||has("mellow")){d.oscA=WaveShape::sine;d.oscB=WaveShape::softSaw;d.oscAMorph=.18f+.22f*hash01(seed,52);d.drive=.03f;d.cutoff*=.62f;}
    if(has("square")||has("8-bit")){d.oscA=d.oscB=WaveShape::square;d.oscAMorph=d.oscBMorph=d.lfoMorphA=d.lfoMorphB=0.f;d.unison=1;d.phaseRandom=0.f;}
    if(has("morph")||has("wavetable")||has("evolving")){d.oscAMorph=.55f+.40f*hash01(seed,53);d.oscBMorph=.45f+.48f*hash01(seed,54);d.lfoCutoff=juce::jmax(d.lfoCutoff,.18f);d.lfoMorphA=.30f+.34f*hash01(seed,59);d.lfoMorphB=-(.24f+.38f*hash01(seed,60));d.lfoRate=.12f+1.1f*hash01(seed,61);d.lfoShape=LfoShape::triangle;}
    if(has("wobble")||has("movement")||has("animated")){d.lfoCutoff=.45f+.25f*hash01(seed,43);d.lfoRate=.35f+2.4f*hash01(seed,44);d.lfoMorphA=.18f+.28f*hash01(seed,62);d.lfoMorphB=-(.14f+.30f*hash01(seed,63));}
    // Explicit modulation-language overrides broad timbre categories and remains deterministic for identical prompt+seed.
    if(has("square lfo")||has("stepped")||has("gate")||has("gated")) d.lfoShape=LfoShape::square;
    else if(has("ramp down")||has("falling lfo")||has("down saw")) d.lfoShape=LfoShape::sawDown;
    else if(has("ramp up")||has("rising lfo")||has("up saw")) d.lfoShape=LfoShape::sawUp;
    else if(has("triangle lfo")||has("smooth movement")) d.lfoShape=LfoShape::triangle;
    else if(has("sine lfo")||has("smooth lfo")) d.lfoShape=LfoShape::sine;
    if(has("pure")||has("clean oscillator")){d.oscAMorph=d.oscBMorph=d.lfoMorphA=d.lfoMorphB=0.f;}
    if(has("wide")||has("stereo"))d.width=.98f; if(has("mono")||has("centered"))d.width=.05f;
    if(has("aggressive")||has("powerful")||has("hard")){d.drive=.24f+hash01(seed,7)*.30f;d.resonance=juce::jmin(.48f,d.resonance+.08f);d.oscBMorph=juce::jmax(d.oscBMorph,.35f);}
    if(has("warm")||has("dark"))d.cutoff*=.58f; if(has("bright")||has("airy")){d.cutoff=juce::jmax(d.cutoff,12500.f);d.noiseLevel=juce::jmax(d.noiseLevel,.018f);}
    if(has("airy"))
    {
        d.noiseLevel=juce::jmax(d.noiseLevel,.035f+.035f*hash01(seed,177));
        d.width=juce::jmax(d.width,.82f);d.attack=juce::jmax(d.attack,.018f);d.reverb=juce::jmax(d.reverb,.22f);
        if(fluteRole||reedRole)
        {
            d.filterMode=FilterMode::lowpass;
            d.cutoff=juce::jmax(d.cutoff,6200.f);
        }
        else if(choirRole)
        {
            d.filterMode=FilterMode::bandpass;
            d.cutoff=juce::jlimit(900.f,4200.f,d.cutoff);
        }
        else
        {
            d.name="Generated Airy";
            d.filterMode=FilterMode::highpass;
            d.cutoff=juce::jlimit(900.f,7800.f,1500.f+3400.f*hash01(seed,178));
        }
    }
    if(has("vibrato")){d.lfoPitch=.32f+.25f*hash01(seed,41);d.lfoRate=4.5f+1.8f*hash01(seed,42);d.lfoShape=LfoShape::sine;}
    if(has("staccato")||has("short decay")){d.attack=juce::jmin(d.attack,.004f);d.decay=juce::jmin(d.decay,.18f);d.sustain=juce::jmin(d.sustain,.24f);d.release=juce::jmin(d.release,.16f);}
    if(has("legato")||has("sustained")){d.attack=juce::jmax(d.attack,.018f);d.sustain=juce::jmax(d.sustain,.78f);d.release=juce::jmax(d.release,.42f);}
    if(has("slow attack"))d.attack=juce::jmax(d.attack,.18f);
    if(has("fast attack"))d.attack=juce::jmin(d.attack,.004f);
    if(has("tremolo")||has("pulsing")){d.lfoShape=LfoShape::sine;d.lfoRate=2.0f+4.5f*hash01(seed,292);d.lfoCutoff=juce::jmax(d.lfoCutoff,.12f);}
    if(has("detuned")){d.detune=juce::jmax(d.detune,.10f+.10f*hash01(seed,293));d.unison=juce::jmax(d.unison,3);}

    if(has("highpass")||has("high-pass")||has("low cut")||has("thin")||has("telephone")){d.filterMode=FilterMode::highpass;d.cutoff=juce::jlimit(120.f,9000.f,450.f+hash01(seed,77)*3600.f);d.resonance=juce::jmax(d.resonance,.12f);}
    if(has("bandpass")||has("band-pass")||has("band pass")||has("nasal")||has("vocal")||has("formant"))
    {
        d.filterMode=FilterMode::bandpass;
        d.cutoff=juce::jlimit(180.f,12000.f,700.f+hash01(seed,78)*6200.f);
        d.resonance=juce::jmax(d.resonance,.18f+hash01(seed,79)*.28f);
    }
    if(has("lowpass")||has("low-pass")||has("low pass")){d.filterMode=FilterMode::lowpass;}
    if(cb) cb(.58f,"Shaping tone, filter and movement");
    d.chorus=.03f+hash01(seed,64)*.15f; d.chorusRate=.16f+hash01(seed,65)*.55f; d.chorusDepth=.20f+hash01(seed,66)*.42f; d.reverb=juce::jmax(d.reverb,.06f+hash01(seed,8)*.32f); d.delay=.03f+hash01(seed,9)*.28f;
    if(has("pad")||has("ambient")){d.chorus=.24f+.18f*hash01(seed,67);d.chorusRate=.10f+.22f*hash01(seed,68);d.chorusDepth=.48f+.25f*hash01(seed,69);}
    if(has("supersaw")||has("wide")||has("stereo")){d.chorus=juce::jmax(d.chorus,.20f+.16f*hash01(seed,70));d.chorusDepth=juce::jmax(d.chorusDepth,.50f);}
    if(has("reese")){d.chorus=.10f+.12f*hash01(seed,71);d.chorusRate=.12f+.18f*hash01(seed,72);d.chorusDepth=.34f+.20f*hash01(seed,73);}
    if(has("chorus")||has("lush")){d.chorus=.38f+.22f*hash01(seed,74);d.chorusRate=.12f+.42f*hash01(seed,75);d.chorusDepth=.58f+.28f*hash01(seed,76);}
    if(has("dry")||has("tight")){d.chorus=0.f;d.reverb=.015f;d.delay=.01f;} if(has("mono")||has("centered"))d.chorus=juce::jmin(d.chorus,.05f); if(has("wet")||has("space")||has("cinematic")){d.chorus=juce::jmax(d.chorus,.26f);d.reverb=.52f;d.delay=.30f;} if(has("noisy")||has("texture"))d.noiseLevel=.10f+.10f*hash01(seed,45);
    // Final role-aware production polish runs after the generic FX defaults so
    // festival requests keep the intended scale and impact.
    if(festival&&leadRole)
    {
        d.chorus=juce::jmax(d.chorus,.22f);d.chorusDepth=juce::jmax(d.chorusDepth,.52f);
        d.reverb=juce::jlimit(.10f,.28f,juce::jmax(d.reverb,.16f));
        d.delay=juce::jlimit(.08f,.22f,.11f+.08f*hash01(seed,243));
    }
    if(festival&&pluckRole)
    {
        d.chorus=juce::jmax(d.chorus,.10f);d.reverb=juce::jlimit(.08f,.22f,juce::jmax(d.reverb,.12f));
        d.delay=.08f+.08f*hash01(seed,244);
    }
    if(festival&&chordRole)
    {
        d.chorus=juce::jmax(d.chorus,.18f);d.chorusDepth=juce::jmax(d.chorusDepth,.48f);
        d.reverb=juce::jlimit(.10f,.26f,juce::jmax(d.reverb,.15f));d.delay=.05f+.06f*hash01(seed,245);
    }
    if(festival&&has("kick"))
    {
        d.transientLevel=juce::jmax(d.transientLevel,.72f);d.transientDecay=.0075f;
        d.pitchEnv=juce::jmax(d.pitchEnv,40.f);d.pitchEnvDecay=.050f;
        d.drive=juce::jmax(d.drive,.27f);d.decay=juce::jmax(d.decay,.20f);d.subLevel=juce::jmax(d.subLevel,.38f);
        d.reverb=0.f;d.delay=0.f;d.chorus=0.f;
    }
    if(festival&&(has("snare")||has("clap")))
    {
        d.transientLevel=juce::jmax(d.transientLevel,.62f);d.noiseLevel=juce::jmax(d.noiseLevel,.52f);
        d.width=juce::jmax(d.width,.68f);d.drive=juce::jmax(d.drive,.12f);d.reverb=juce::jlimit(.04f,.13f,d.reverb);
    }
    if(has("riser")||has("uplifter"))
    {
        d.name="Generated Riser";d.noiseLevel=juce::jmax(d.noiseLevel,.16f);d.filterMode=FilterMode::highpass;
        d.cutoff=900.f+2200.f*hash01(seed,246);d.lfoCutoff=.55f+.25f*hash01(seed,247);d.lfoRate=.18f+.8f*hash01(seed,248);
        d.attack=.18f;d.release=.65f;d.width=.98f;d.reverb=juce::jmax(d.reverb,.28f);d.delay=juce::jmax(d.delay,.14f);
    }
    if(has("impact")||has("downlifter"))
    {
        d.name="Generated Impact";d.transientLevel=juce::jmax(d.transientLevel,.78f);d.transientDecay=.014f;
        d.pitchEnv=has("downlifter")?30.f:18.f;d.pitchEnvDecay=.16f;d.subLevel=juce::jmax(d.subLevel,.36f);
        d.noiseLevel=juce::jmax(d.noiseLevel,.12f);d.drive=juce::jmax(d.drive,.22f);d.release=.55f;
    }
    if(has("pure sub")||has("clean sub")||has("sine sub"))
    {
        d.name="Generated Sub";
        d.oscA=WaveShape::sine;d.oscB=WaveShape::sine;d.oscMix=.05f;d.oscBTranspose=0.f;
        d.unison=1;d.detune=0.f;d.phaseRandom=0.f;d.width=0.f;
        d.attack=.004f;d.decay=.08f;d.sustain=.94f;d.release=.14f;
        d.filterMode=FilterMode::lowpass;d.cutoff=115.f;d.resonance=.06f;d.filterEnv=0.f;
        d.subLevel=0.f;d.noiseLevel=0.f;d.fmAmount=0.f;d.ringMod=0.f;
        d.drive=.018f;d.chorus=0.f;d.reverb=0.f;d.delay=0.f;
        d.lfoCutoff=0.f;d.lfoPitch=0.f;d.lfoMorphA=0.f;d.lfoMorphB=0.f;
    }
    d.oscAMorph=juce::jlimit(0.f,1.f,d.oscAMorph);d.oscBMorph=juce::jlimit(0.f,1.f,d.oscBMorph);d.lfoMorphA=juce::jlimit(-1.f,1.f,d.lfoMorphA);d.lfoMorphB=juce::jlimit(-1.f,1.f,d.lfoMorphB);d.cutoff=juce::jlimit(80.f,19000.f,d.cutoff);d.subLevel=juce::jlimit(0.f,.65f,d.subLevel);d.noiseLevel=juce::jlimit(0.f,.35f,d.noiseLevel);d.pitchEnv=juce::jlimit(-48.f,48.f,d.pitchEnv);d.pitchEnvDecay=juce::jlimit(.005f,4.f,d.pitchEnvDecay);d.transientLevel=juce::jlimit(0.f,1.f,d.transientLevel);d.transientDecay=juce::jlimit(.001f,.5f,d.transientDecay);d.fmAmount=juce::jlimit(0.f,1.f,d.fmAmount);d.fmRatio=juce::jlimit(.125f,16.f,d.fmRatio);d.ringMod=juce::jlimit(0.f,1.f,d.ringMod);d.bitCrush=juce::jlimit(0.f,1.f,d.bitCrush);d.downsample=juce::jlimit(0.f,1.f,d.downsample);d.chorus=juce::jlimit(0.f,1.f,d.chorus);d.chorusRate=juce::jlimit(.02f,8.f,d.chorusRate);d.chorusDepth=juce::jlimit(0.f,1.f,d.chorusDepth);
    if(cb) cb(.82f,"Building chorus, space and FX"); if(cb) cb(1.f,"Ready"); return d;
}
SoundDNA PromptGenerator::mutate(const SoundDNA& s,uint64_t seed,float amount,Progress cb) const {
    auto n=generate(s.sourcePrompt,seed,cb); amount=juce::jlimit(0.f,1.f,amount); auto mix=[amount](float a,float b){return a+(b-a)*amount;};
    n.oscMix=mix(s.oscMix,n.oscMix);n.oscBTranspose=mix(s.oscBTranspose,n.oscBTranspose);n.oscAMorph=mix(s.oscAMorph,n.oscAMorph);n.oscBMorph=mix(s.oscBMorph,n.oscBMorph);n.detune=mix(s.detune,n.detune);n.unisonBlend=mix(s.unisonBlend,n.unisonBlend);n.phaseRandom=mix(s.phaseRandom,n.phaseRandom);
    n.attack=mix(s.attack,n.attack);n.decay=mix(s.decay,n.decay);n.sustain=mix(s.sustain,n.sustain);n.release=mix(s.release,n.release);n.cutoff=mix(s.cutoff,n.cutoff);n.resonance=mix(s.resonance,n.resonance);n.filterEnv=mix(s.filterEnv,n.filterEnv);
    n.lfoRate=mix(s.lfoRate,n.lfoRate);n.lfoCutoff=mix(s.lfoCutoff,n.lfoCutoff);n.lfoPitch=mix(s.lfoPitch,n.lfoPitch);n.lfoMorphA=mix(s.lfoMorphA,n.lfoMorphA);n.lfoMorphB=mix(s.lfoMorphB,n.lfoMorphB);n.subLevel=mix(s.subLevel,n.subLevel);n.subOctave=mix(s.subOctave,n.subOctave);n.noiseLevel=mix(s.noiseLevel,n.noiseLevel);n.pitchEnv=mix(s.pitchEnv,n.pitchEnv);n.pitchEnvDecay=mix(s.pitchEnvDecay,n.pitchEnvDecay);n.transientLevel=mix(s.transientLevel,n.transientLevel);n.transientDecay=mix(s.transientDecay,n.transientDecay);n.fmAmount=mix(s.fmAmount,n.fmAmount);n.fmRatio=mix(s.fmRatio,n.fmRatio);n.ringMod=mix(s.ringMod,n.ringMod);n.bitCrush=mix(s.bitCrush,n.bitCrush);n.downsample=mix(s.downsample,n.downsample);n.drive=mix(s.drive,n.drive);n.width=mix(s.width,n.width);n.chorus=mix(s.chorus,n.chorus);n.chorusRate=mix(s.chorusRate,n.chorusRate);n.chorusDepth=mix(s.chorusDepth,n.chorusDepth);n.reverb=mix(s.reverb,n.reverb);n.delay=mix(s.delay,n.delay);
    // Similar generation preserves discrete topology and modulation character. Strong mutations may adopt newly designed choices.
    if(amount<.5f){n.oscA=s.oscA;n.oscB=s.oscB;n.unison=s.unison;n.filterMode=s.filterMode;n.lfoShape=s.lfoShape;} n.name="Mutation"; return n;
}
}