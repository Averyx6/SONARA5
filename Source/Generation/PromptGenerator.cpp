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
    if(has("airy")){d.name="Generated Airy";d.noiseLevel=juce::jmax(d.noiseLevel,.035f+.035f*hash01(seed,177));d.width=juce::jmax(d.width,.82f);d.attack=juce::jmax(d.attack,.018f);d.filterMode=FilterMode::highpass;d.cutoff=juce::jlimit(900.f,7800.f,1500.f+3400.f*hash01(seed,178));d.reverb=juce::jmax(d.reverb,.22f);}
    if(has("vibrato")){d.lfoPitch=.32f+.25f*hash01(seed,41);d.lfoRate=4.5f+1.8f*hash01(seed,42);d.lfoShape=LfoShape::sine;}
    if(has("highpass")||has("high-pass")||has("low cut")||has("thin")||has("telephone")){d.filterMode=FilterMode::highpass;d.cutoff=juce::jlimit(120.f,9000.f,450.f+hash01(seed,77)*3600.f);d.resonance=juce::jmax(d.resonance,.12f);}
    if(has("bandpass")||has("band-pass")||has("band pass")||has("nasal")||has("vocal")||has("formant")){d.filterMode=FilterMode::bandpass;d.cutoff=juce::jlimit(180.f,12000.f,700.f+hash01(seed,78)*6200.f);d.resonance=.18f+hash01(seed,79)*.28f;}
    if(has("lowpass")||has("low-pass")||has("low pass")){d.filterMode=FilterMode::lowpass;}
    if(cb) cb(.58f,"Shaping tone, filter and movement");
    d.chorus=.03f+hash01(seed,64)*.15f; d.chorusRate=.16f+hash01(seed,65)*.55f; d.chorusDepth=.20f+hash01(seed,66)*.42f; d.reverb=juce::jmax(d.reverb,.06f+hash01(seed,8)*.32f); d.delay=.03f+hash01(seed,9)*.28f;
    if(has("pad")||has("ambient")){d.chorus=.24f+.18f*hash01(seed,67);d.chorusRate=.10f+.22f*hash01(seed,68);d.chorusDepth=.48f+.25f*hash01(seed,69);}
    if(has("supersaw")||has("wide")||has("stereo")){d.chorus=juce::jmax(d.chorus,.20f+.16f*hash01(seed,70));d.chorusDepth=juce::jmax(d.chorusDepth,.50f);}
    if(has("reese")){d.chorus=.10f+.12f*hash01(seed,71);d.chorusRate=.12f+.18f*hash01(seed,72);d.chorusDepth=.34f+.20f*hash01(seed,73);}
    if(has("chorus")||has("lush")){d.chorus=.38f+.22f*hash01(seed,74);d.chorusRate=.12f+.42f*hash01(seed,75);d.chorusDepth=.58f+.28f*hash01(seed,76);}
    if(has("dry")||has("tight")){d.chorus=0.f;d.reverb=.015f;d.delay=.01f;} if(has("mono")||has("centered"))d.chorus=juce::jmin(d.chorus,.05f); if(has("wet")||has("space")||has("cinematic")){d.chorus=juce::jmax(d.chorus,.26f);d.reverb=.52f;d.delay=.30f;} if(has("noisy")||has("texture"))d.noiseLevel=.10f+.10f*hash01(seed,45);
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