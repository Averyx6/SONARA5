#include "SonaraEngine.h"
#include <cmath>
namespace sonara {
float SonaraVoice::polyBlep(float t,float dt) noexcept { if(dt<=0.f)return 0.f;if(t<dt){t/=dt;return t+t-t*t-1.f;}if(t>1.f-dt){t=(t-1.f)/dt;return t*t+t+t+1.f;}return 0.f; }
float SonaraVoice::wave(WaveShape s,double p,double inc) noexcept
{
    const auto twoPi=juce::MathConstants<double>::twoPi;
    float t=(float)(p/twoPi);t-=std::floor(t);
    const float dt=juce::jlimit(1.0e-6f,.49f,(float)std::abs(inc/twoPi));
    switch(s)
    {
        case WaveShape::sine:
            return (float)std::sin(p);
        case WaveShape::triangle:
            return 1.f-4.f*std::abs(std::round(t)-t);
        case WaveShape::square:
        {
            float y=t<.5f?1.f:-1.f;y+=polyBlep(t,dt);
            float t2=t+.5f;if(t2>=1.f)t2-=1.f;y-=polyBlep(t2,dt);
            return y;
        }
        case WaveShape::softSaw:
        {
            float y=2.f*t-1.f;y-=polyBlep(t,dt);
            return .72f*y+.28f*(float)std::sin(p);
        }
        default:
        {
            float y=2.f*t-1.f;
            return y-polyBlep(t,dt);
        }
    }
}
float SonaraVoice::morphedWave(WaveShape shape,float morph,double phase,double inc) noexcept { morph=juce::jlimit(0.f,1.f,morph);if(morph<=0.0001f)return wave(shape,phase,inc);const int base=static_cast<int>(shape);const auto next=static_cast<WaveShape>((base+1)%5);const float a=wave(shape,phase,inc),b=wave(next,phase,inc);const float x=morph*morph*(3.f-2.f*morph);return a+(b-a)*x; }
static float evaluateLfo(LfoShape shape,float phase) noexcept {const float twoPi=juce::MathConstants<float>::twoPi;float p=phase/twoPi;p-=std::floor(p);switch(shape){case LfoShape::triangle:return 1.f-4.f*std::abs(p-.5f);case LfoShape::sawUp:return 2.f*p-1.f;case LfoShape::sawDown:return 1.f-2.f*p;case LfoShape::square:return p<.5f?1.f:-1.f;default:return std::sin(phase);}}
float SonaraVoice::nextNoise() noexcept { noiseState ^= noiseState << 13; noiseState ^= noiseState >> 7; noiseState ^= noiseState << 17; return (float)((noiseState >> 40) & 0xFFFFFF) * (2.0f / 16777215.0f) - 1.0f; }
void SonaraVoice::prepare(double sampleRate,int maximumBlockSize,int numChannels) noexcept {const juce::dsp::ProcessSpec spec{juce::jmax(8000.0,sampleRate),(juce::uint32)juce::jmax(1,maximumBlockSize),(juce::uint32)juce::jmax(1,numChannels)};filterL.prepare(spec);filterR.prepare(spec);filterL.reset();filterR.reset();adsr.setSampleRate(spec.sampleRate);}
void SonaraVoice::startNote(int midi,float velocity,juce::SynthesiserSound*,int initialPitchWheel){pitchBendRatio=1.f;noteHz=juce::MidiMessage::getMidiNoteInHertz(midi);auto sr=getSampleRate();
const float voiceComplexity=dna.fmAmount*1.5f+dna.ringMod*1.2f+std::abs(dna.lfoMorphA)+std::abs(dna.lfoMorphB)+dna.lfoCutoff*.8f+dna.noiseLevel*.4f;
int requestedUnison=juce::jmin(dna.unison,runtimeUnisonCap);
if(voiceComplexity>2.0f)requestedUnison=juce::jmin(requestedUnison,3);
else if(voiceComplexity>1.25f)requestedUnison=juce::jmin(requestedUnison,4);
voices=juce::jlimit(1,maxUnison,requestedUnison);level=velocity;for(int i=0;i<voices;++i){const float pos=voices==1?0.f:(2.f*i/(voices-1.f)-1.f);const double cents=pos*dna.detune*100.0;const double ratio=std::pow(2.0,cents/1200.0);incA[i]=juce::MathConstants<double>::twoPi*noteHz*ratio/sr;incB[i]=juce::MathConstants<double>::twoPi*noteHz*ratio*std::pow(2.0,dna.oscBTranspose/12.0)/sr;const uint64_t h=dna.seed+(uint64_t)i*0x9e3779b97f4a7c15ULL;phaseA[i]=dna.phaseRandom*juce::MathConstants<double>::twoPi*((h>>8)&65535)/65535.0;phaseB[i]=dna.phaseRandom*juce::MathConstants<double>::twoPi*((h>>24)&65535)/65535.0;const float pan=pos*dna.width;panL[i]=std::sqrt(.5f*(1.f-pan));panR[i]=std::sqrt(.5f*(1.f+pan));}subPhase=0.0;subInc=juce::MathConstants<double>::twoPi*noteHz*std::pow(2.0,(double)dna.subOctave)/sr;noiseState=dna.seed^((uint64_t)(midi+1)*0x9e3779b97f4a7c15ULL);if(noiseState==0)noiseState=1;
pitchWheelMoved(initialPitchWheel);
ageSamples=0;holdCounter=0;heldL=heldR=0.f;env={dna.attack,dna.decay,dna.sustain,dna.release};adsr.setSampleRate(sr);adsr.setParameters(env);adsr.noteOn();lfoPhase=0.f;
pitchEnvState=dna.pitchEnv;pitchEnvMul=std::exp(-1.f/(float)(sr*juce::jmax(.005f,dna.pitchEnvDecay)));
transientState=dna.transientLevel;transientMul=std::exp(-1.f/(float)(sr*juce::jmax(.001f,dna.transientDecay)));
unisonNorm=.28f/std::sqrt((float)juce::jmax(1,voices));
lfoActive=std::abs(dna.lfoPitch)>.0001f||std::abs(dna.lfoCutoff)>.0001f||std::abs(dna.lfoMorphA)>.0001f||std::abs(dna.lfoMorphB)>.0001f;
filterDynamic=std::abs(dna.lfoCutoff)>.0001f||std::abs(dna.filterEnv)>.0001f;
filterUpdateCounter=0;holdPeriodSamples=1+(int)std::round(dna.downsample*dna.downsample*23.f);
const float bits=16.f-12.f*dna.bitCrush;crushLevels=std::pow(2.f,bits);juce::dsp::StateVariableTPTFilterType type=juce::dsp::StateVariableTPTFilterType::lowpass;if(dna.filterMode==FilterMode::highpass)type=juce::dsp::StateVariableTPTFilterType::highpass;else if(dna.filterMode==FilterMode::bandpass)type=juce::dsp::StateVariableTPTFilterType::bandpass;for(auto* f:{&filterL,&filterR}){f->reset();f->setType(type);f->setResonance(juce::jlimit(.01f,.95f,dna.resonance));f->setCutoffFrequency(juce::jlimit(30.f,(float)sr*.45f,dna.cutoff));}}
void SonaraVoice::stopNote(float,bool tail){if(tail)adsr.noteOff();else{adsr.reset();clearCurrentNote();}}
void SonaraVoice::renderNextBlock(juce::AudioBuffer<float>& out,int start,int count){
    const auto sr=(float)getSampleRate();
    while(count-->0){
        const float e=adsr.getNextSample();
        const float lfo=lfoActive?evaluateLfo(dna.lfoShape,lfoPhase):0.f;
        if(lfoActive){lfoPhase+=juce::MathConstants<float>::twoPi*dna.lfoRate/sr;if(lfoPhase>=juce::MathConstants<float>::twoPi)lfoPhase-=juce::MathConstants<float>::twoPi;}
        const float pitchEnvValue=pitchEnvState;pitchEnvState*=pitchEnvMul;
        const double pitchEnvelopeRatio=std::abs(pitchEnvValue)>.0001f?std::pow(2.0,(double)pitchEnvValue/12.0):1.0;
        const float morphA=juce::jlimit(0.f,1.f,dna.oscAMorph+dna.lfoMorphA*lfo*.5f),morphB=juce::jlimit(0.f,1.f,dna.oscBMorph+dna.lfoMorphB*lfo*.5f);
        if(filterDynamic)
        {
            if(filterUpdateCounter<=0)
            {
                const float cutoff=juce::jlimit(30.f,sr*.45f,dna.cutoff*(1.f+dna.lfoCutoff*lfo*.75f+dna.filterEnv*e));
                filterL.setCutoffFrequency(cutoff);filterR.setCutoffFrequency(cutoff);
                filterUpdateCounter=7;
            }
            else --filterUpdateCounter;
        }
        float l=0.f,r=0.f;
        for(int i=0;i<voices;++i){
            const double pitchMod=juce::jlimit(.35,2.4,(1.0+dna.lfoPitch*lfo*.01)*pitchEnvelopeRatio);
            const double stepA=incA[i]*pitchMod,stepB=incB[i]*pitchMod*(dna.fmAmount>.0001f?dna.fmRatio:1.f);
            const float b=morphedWave(dna.oscB,morphB,phaseB[i],stepB);
            const float a=morphedWave(dna.oscA,morphA,phaseA[i]+(double)b*dna.fmAmount*4.0,stepA);
            float x=(1.f-dna.oscMix)*a+dna.oscMix*b;
            x=x*(1.f-dna.ringMod)+(a*b)*dna.ringMod;
            const float gain=i==voices/2?1.f:dna.unisonBlend;
            l+=x*gain*panL[i];r+=x*gain*panR[i];
            phaseA[i]+=stepA;phaseB[i]+=stepB;
            if(phaseA[i]>=juce::MathConstants<double>::twoPi)phaseA[i]=std::fmod(phaseA[i],juce::MathConstants<double>::twoPi);
            if(phaseB[i]>=juce::MathConstants<double>::twoPi)phaseB[i]=std::fmod(phaseB[i],juce::MathConstants<double>::twoPi);
        }
        l*=unisonNorm;r*=unisonNorm;
        const float sub=(float)std::sin(subPhase)*dna.subLevel*.32f;subPhase+=subInc*pitchEnvelopeRatio;if(subPhase>=juce::MathConstants<double>::twoPi)subPhase=std::fmod(subPhase,juce::MathConstants<double>::twoPi);
        const float noise=nextNoise();
        const float transientEnv=transientState;transientState*=transientMul;
        const float texture=noise*dna.noiseLevel*.14f + noise*transientEnv*.32f;
        l+=sub+texture;r+=sub+texture;
        l=std::tanh(l*(1.f+dna.drive*5.f))*e*level;r=std::tanh(r*(1.f+dna.drive*5.f))*e*level;
        l=filterL.processSample(0,l);r=filterR.processSample(0,r);
        if(holdCounter<=0){heldL=l;heldR=r;holdCounter=holdPeriodSamples;}
        --holdCounter;l=heldL;r=heldR;
        if(dna.bitCrush>.001f){l=std::round(l*crushLevels)/crushLevels;r=std::round(r*crushLevels)/crushLevels;}
        if(out.getNumChannels()>0)out.addSample(0,start,l);if(out.getNumChannels()>1)out.addSample(1,start,r);
        ++start;++ageSamples;
    }
    if(!adsr.isActive())clearCurrentNote();
}
SonaraEngine::SonaraEngine(){synth.setMinimumRenderingSubdivisionSize(1,true);for(int i=0;i<8;++i)synth.addVoice(new SonaraVoice());synth.addSound(new SonaraSound());audioDNA.copyDSPFrom(dna);pendingDNA=dna;patchPending=true;}
void SonaraEngine::rebuildVoices(){synth.allNotesOff(0,false);synth.clearVoices();const int count=lowCpuMode?juce::jmin(requestedVoices,4):requestedVoices;for(int i=0;i<juce::jlimit(1,8,count);++i)synth.addVoice(new SonaraVoice());}
void SonaraEngine::setLowCpuMode(bool enabled){if(lowCpuMode==enabled)return;lowCpuMode=enabled;rebuildVoices();}
void SonaraEngine::setRuntimeEcoMode(bool enabled) noexcept
{
    if(runtimeEcoMode==enabled)return;
    runtimeEcoMode=enabled;
    const int cap=enabled?2:9;
    for(int i=0;i<synth.getNumVoices();++i)
        if(auto* v=dynamic_cast<SonaraVoice*>(synth.getVoice(i)))v->setRuntimeUnisonCap(cap);
}
void SonaraEngine::setVoiceLimit(int voices){const int next=juce::jlimit(1,8,voices);if(requestedVoices==next)return;requestedVoices=next;rebuildVoices();}
void SonaraEngine::prepare(double sampleRate,int maximumBlockSize,int numChannels){sr=juce::jmax(8000.0,sampleRate);synth.setCurrentPlaybackSampleRate(sr);for(int i=0;i<synth.getNumVoices();++i)if(auto*v=dynamic_cast<SonaraVoice*>(synth.getVoice(i))){v->prepare(sr,maximumBlockSize,numChannels);v->setRuntimeUnisonCap(runtimeEcoMode?2:9);}reverb.setSampleRate(sr);reverb.reset();chorusBuffer.setSize(2,juce::jmax(8,(int)std::ceil(sr*.06)+4),false,false,true);chorusBuffer.clear();chorusWrite=0;chorusPhase=0.f;delayBuffer.setSize(2,juce::jmax(4,(int)std::ceil(sr*.45)+2),false,false,true);delayBuffer.clear();delayWrite=0;}
void SonaraEngine::reset() noexcept {synth.allNotesOff(0,false);reverb.reset();chorusBuffer.clear();delayBuffer.clear();chorusWrite=delayWrite=0;chorusPhase=0.f;}
bool SonaraEngine::hasActiveVoices() noexcept {for(int i=0;i<synth.getNumVoices();++i)if(auto* v=synth.getVoice(i);v!=nullptr&&v->isVoiceActive())return true;return false;}
void SonaraEngine::setPatch(const SoundDNA& d){const juce::SpinLock::ScopedLockType lock(pendingLock);dna=d;auto next=d;pendingDNA=next;patchPending=true;}
void SonaraEngine::applyPendingPatch() noexcept {juce::SpinLock::ScopedTryLockType lock(pendingLock);if(!lock.isLocked()||!patchPending)return;audioDNA.copyDSPFrom(pendingDNA);patchPending=false;for(int i=0;i<synth.getNumVoices();++i)if(auto*v=dynamic_cast<SonaraVoice*>(synth.getVoice(i)))v->setDNA(audioDNA);reverbParams.roomSize=.2f+audioDNA.reverb*.65f;reverbParams.wetLevel=audioDNA.reverb*.35f;reverbParams.dryLevel=1.f-reverbParams.wetLevel*.25f;reverbParams.width=audioDNA.width;reverb.setParameters(reverbParams);reverb.setSampleRate(sr);}
float SonaraEngine::readFractional(const juce::AudioBuffer<float>& b,int channel,int writeIndex,float delaySamples) noexcept {const int size=b.getNumSamples();if(size<2)return 0.f;float pos=(float)writeIndex-juce::jlimit(1.f,(float)(size-2),delaySamples);while(pos<0.f)pos+=(float)size;const int i0=(int)pos%size,i1=(i0+1)%size;const float frac=pos-std::floor(pos);const float* d=b.getReadPointer(juce::jlimit(0,b.getNumChannels()-1,channel));return d[i0]+(d[i1]-d[i0])*frac;}
void SonaraEngine::processChorus(juce::AudioBuffer<float>& a,int numSamples) noexcept {if(audioDNA.chorus<=0.0001f||chorusBuffer.getNumSamples()<2||a.getNumChannels()==0)return;const int samples=juce::jlimit(0,a.getNumSamples(),numSamples);const float wet=juce::jlimit(0.f,.42f,audioDNA.chorus*.42f),rate=juce::jlimit(.02f,8.f,audioDNA.chorusRate),depth=juce::jlimit(0.f,1.f,audioDNA.chorusDepth);const float base=(float)sr*.012f,sweep=(float)sr*(.0015f+.0085f*depth),phaseInc=juce::MathConstants<float>::twoPi*rate/(float)sr;const int size=chorusBuffer.getNumSamples();for(int i=0;i<samples;++i){const float inL=a.getSample(0,i),inR=a.getNumChannels()>1?a.getSample(1,i):inL;chorusBuffer.setSample(0,chorusWrite,inL);chorusBuffer.setSample(1,chorusWrite,inR);const float modL=.5f+.5f*std::sin(chorusPhase),modR=.5f+.5f*std::sin(chorusPhase+juce::MathConstants<float>::halfPi);const float outL=readFractional(chorusBuffer,0,chorusWrite,base+sweep*modL),outR=readFractional(chorusBuffer,1,chorusWrite,base+sweep*modR);a.setSample(0,i,inL*(1.f-wet)+outL*wet);if(a.getNumChannels()>1)a.setSample(1,i,inR*(1.f-wet)+outR*wet);if(++chorusWrite>=size)chorusWrite=0;chorusPhase+=phaseInc;if(chorusPhase>=juce::MathConstants<float>::twoPi)chorusPhase-=juce::MathConstants<float>::twoPi;}}
float SonaraEngine::readDelay(int channel,float delaySamples) const noexcept {return readFractional(delayBuffer,channel,delayWrite,delaySamples);}
void SonaraEngine::processDelay(juce::AudioBuffer<float>& a,int numSamples) noexcept {if(audioDNA.delay<=0.0001f||delayBuffer.getNumSamples()<2||a.getNumChannels()==0)return;const int samples=juce::jlimit(0,a.getNumSamples(),numSamples);const float mix=juce::jlimit(0.f,.45f,audioDNA.delay*.45f);const float feedback=juce::jlimit(0.f,.72f,.18f+audioDNA.delay*.48f);const float leftTime=(float)(sr*.285),rightTime=(float)(sr*.365);const int size=delayBuffer.getNumSamples();for(int i=0;i<samples;++i){const float inL=a.getSample(0,i);const float inR=a.getNumChannels()>1?a.getSample(1,i):inL;const float wetL=readDelay(0,leftTime),wetR=readDelay(1,rightTime);delayBuffer.setSample(0,delayWrite,inL+wetR*feedback);delayBuffer.setSample(1,delayWrite,inR+wetL*feedback);a.setSample(0,i,inL*(1.f-mix)+wetL*mix);if(a.getNumChannels()>1)a.setSample(1,i,inR*(1.f-mix)+wetR*mix);if(++delayWrite>=size)delayWrite=0;}}
void SonaraEngine::render(juce::AudioBuffer<float>& a,juce::MidiBuffer& m){render(a,m,a.getNumSamples());}
void SonaraEngine::render(juce::AudioBuffer<float>& a,juce::MidiBuffer& m,int numSamples)
{
    const int samples=juce::jlimit(0,a.getNumSamples(),numSamples);
    if(samples<=0)return;
    applyPendingPatch();
    synth.renderNextBlock(a,m,0,samples);

    if(!runtimeEcoMode)
    {
        processChorus(a,samples);
        if(audioDNA.reverb>.0001f)
        {
            if(a.getNumChannels()>=2)reverb.processStereo(a.getWritePointer(0),a.getWritePointer(1),samples);
            else if(a.getNumChannels()==1)reverb.processMono(a.getWritePointer(0),samples);
        }
        processDelay(a,samples);
    }

    for(int ch=0;ch<a.getNumChannels();++ch)
    {
        auto* p=a.getWritePointer(ch);
        for(int i=0;i<samples;++i)
        {
            const float x=std::isfinite(p[i])?p[i]:0.f;
            p[i]=runtimeEcoMode?juce::jlimit(-.98f,.98f,x):std::tanh(x*.92f);
        }
    }
}
}