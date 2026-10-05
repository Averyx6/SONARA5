#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace sonara::mixpolicy {

constexpr int musicalLaneCount = 8;

// Lane order after drums: BASS, SUB, CHORDS, PLUCK, PAD, LEAD, COUNTER, FX.
inline float baseGain(int lane) noexcept
{
    static constexpr std::array<float,musicalLaneCount> values{
        .62f,.36f,.37f,.27f,.21f,.84f,.18f,.15f
    };
    return values[(size_t)juce::jlimit(0,musicalLaneCount-1,lane)];
}

inline float highPassHz(int lane) noexcept
{
    static constexpr std::array<float,musicalLaneCount> values{
        32.f,22.f,120.f,125.f,160.f,120.f,150.f,110.f
    };
    return values[(size_t)juce::jlimit(0,musicalLaneCount-1,lane)];
}

inline float baseFxSend(int lane) noexcept
{
    static constexpr std::array<float,musicalLaneCount> values{
        0.f,0.f,.14f,.10f,.18f,.12f,.08f,.15f
    };
    return values[(size_t)juce::jlimit(0,musicalLaneCount-1,lane)];
}

inline float duckDepth(int lane) noexcept
{
    switch(lane)
    {
        case 1: return .58f; // SUB
        case 0: return .50f; // BASS
        case 2: return .28f; // CHORDS
        case 4: return .20f; // PAD
        default:return 0.f;
    }
}

inline float subLowPassHz() noexcept { return 112.f; }
inline float bassStereoFraction() noexcept { return .08f; }

inline float toneCutoffHz(float energy) noexcept
{
    return 4800.f + 14800.f*juce::jlimit(0.f,1.f,energy);
}

inline float energyGain(int lane,float energy) noexcept
{
    energy=juce::jlimit(0.f,1.f,energy);
    // Low end rises less with section intensity than the upper arrangement.
    // This prevents bass+sub buildup in drops while keeping the musical layers lively.
    return lane<=1 ? (.82f+.15f*energy) : (.84f+.20f*energy);
}

inline float energySpace(float energy) noexcept
{
    return .88f+.24f*(1.f-juce::jlimit(0.f,1.f,energy));
}

inline float stereoWidth(int lane,float requested,float energy) noexcept
{
    requested=juce::jlimit(0.f,1.5f,requested);
    if(lane==1)return 0.f;
    if(lane==0)return juce::jmin(.18f,requested);
    return requested*(.70f+.34f*juce::jlimit(0.f,1.f,energy));
}

inline float sectionGain(int lane,const juce::String& section) noexcept
{
    const bool intro=section=="INTRO"||section=="OUTRO";
    const bool verse=section.startsWith("VERSE");
    const bool build=section.contains("BUILD");
    const bool chorus=section=="CHORUS";
    const bool drop=section.contains("DROP");
    const bool breakdown=section=="BREAKDOWN";
    const bool finalHook=section=="FINAL HOOK";

    switch(lane)
    {
        case 5: return intro?.62f:(verse?.76f:(build?.84f:(chorus?1.02f:(drop?1.14f:(breakdown?.66f:(finalHook?1.18f:1.f))))));
        case 0: return intro?.50f:(verse?.80f:(build?.88f:(chorus?.96f:(drop?1.08f:(breakdown?.48f:(finalHook?1.10f:1.f))))));
        case 1: return intro?.25f:(verse?.70f:(build?.78f:(chorus?.88f:(drop?.98f:(breakdown?.28f:(finalHook?1.00f:1.f))))));
        case 2: return intro?.72f:(verse?.78f:(build?.88f:(chorus?1.02f:(drop?1.04f:(breakdown?.74f:(finalHook?1.08f:1.f))))));
        case 3: return intro?.45f:(verse?.62f:(build?.72f:(chorus?.70f:(drop?.88f:(breakdown?.40f:(finalHook?.92f:1.f))))));
        case 4: return intro?1.02f:(verse?.88f:(build?.72f:(chorus?.52f:(drop?.38f:(breakdown?1.08f:(finalHook?.42f:1.f))))));
        case 6: return (drop||finalHook)?.72f:.42f;
        default:return 1.f;
    }
}

inline float drumGain(const juce::String& section,float energy) noexcept
{
    const bool intro=section=="INTRO"||section=="OUTRO";
    const bool verse=section.startsWith("VERSE");
    const bool build=section.contains("BUILD");
    const bool chorus=section=="CHORUS";
    const bool drop=section.contains("DROP");
    const bool breakdown=section=="BREAKDOWN";
    const bool finalHook=section=="FINAL HOOK";
    const float sectionGainValue=intro?.46f:(verse?.60f:(build?.64f:
        (chorus?.68f:(drop?.84f:(breakdown?.48f:(finalHook?.86f:.70f))))));
    return sectionGainValue*(.84f+.20f*juce::jlimit(0.f,1.f,energy));
}

inline float masterHighPassHz() noexcept { return 24.f; }
inline float masterDrive() noexcept { return 1.18f; }
inline float masterCeiling() noexcept { return .955f; }

inline float processMasterSample(float input,float& x1,float& y1,double sampleRate) noexcept
{
    const float safe=std::isfinite(input)?input:0.f;
    const float rc=1.f/(juce::MathConstants<float>::twoPi*masterHighPassHz());
    const float dt=1.f/(float)juce::jmax(8000.0,sampleRate);
    const float a=rc/(rc+dt);
    const float hp=a*(y1+safe-x1);
    x1=safe;y1=hp;
    return juce::jlimit(-masterCeiling(),masterCeiling(),std::tanh(hp*masterDrive()));
}

} // namespace sonara::mixpolicy
