#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace sonara {

// Resolve language once, before choosing a song plan. In particular, a denied
// instrument/trait must never become a positive rule in a downstream writer.
struct ProducerPrompt {
    enum Exclusion : unsigned {
        counter=1u, pad=2u, pluck=4u, fx=8u, hats=16u, bass=32u,
        sub=64u, chords=128u, lead=256u, kick=512u, snare=1024u, percussion=2048u
    };
    juce::String positive;
    unsigned exclusions=0;
    double durationSeconds=0.0;

    static ProducerPrompt parse(const juce::String& raw)
    {
        ProducerPrompt out;
        auto text=raw.toLowerCase()
            .replace(juce::String::fromUTF8("\xe2\x99\xaf"),"#")
            .replace(juce::String::fromUTF8("\xe2\x99\xad"),"b")
            .replace("drum & bass","drum and bass").replace("drum'n'bass","drum and bass")
            .replace("drum n bass","drum and bass").replace("hi-hats","hats")
            .replace("hi hats","hats").replace("hi-hat","hat")
            .replace("counter-melody","counter melody").replace("half-time","half time")
            .replace("bpm=","bpm ").replace("tempo=","tempo ")
            .replace("key=","key ").replace("key:","key ");
        juce::StringArray words;
        // Keep decimal duration/tempo values and pitch accidentals intact.
        words.addTokens(text," ,;:/\t\r\n()[]{}","\"'");
        words.trim();words.removeEmptyStrings();
        auto instrument=[](const juce::String& w)->unsigned {
            if(w=="counter"||w=="countermelody")return counter;
            if(w=="pad"||w=="pads"||w=="strings")return pad;
            if(w=="arp"||w=="arps"||w=="arpeggio"||w=="arpeggios"||w=="pluck"||w=="plucks")return pluck;
            if(w=="fx"||w=="effects"||w=="risers"||w=="transitions")return fx;
            if(w=="hat"||w=="hats"||w=="hihat"||w=="hihats"||w=="cymbals")return hats;
            if(w=="bass"||w=="bassline"||w=="basses")return bass;
            if(w=="sub"||w=="subs"||w=="subbass"||w=="sub-bass")return sub;
            if(w=="chord"||w=="chords"||w=="harmony")return chords;
            if(w=="lead"||w=="leads")return lead;
            if(w=="kick"||w=="kicks")return kick;
            if(w=="snare"||w=="snares"||w=="clap"||w=="claps")return snare;
            if(w=="percussion"||w=="perc")return percussion;
            if(w=="drums"||w=="drum")return kick|snare|hats|percussion;
            return 0u;
        };
        juce::StringArray affirmative;
        for(int i=0;i<words.size();++i)
        {
            const auto w=words[i];
            const bool denied=w=="no"||w=="not"||w=="without"||w=="exclude"
                ||w=="excluding"||w=="avoid"||w=="remove";
            if(denied&&i+1<words.size())
            {
                int end=i+1;
                unsigned mask=0;
                for(;end<words.size();++end)
                {
                    const auto next=words[end].trimCharactersAtEnd(".!?");
                    const auto bit=instrument(next);
                    if(bit!=0){mask|=bit;continue;}
                    if(next=="and"||next=="or"||next=="&"||next=="any"||next=="the"||next=="all"
                       ||(mask!=0&&(next=="melody"||next=="melodies")))continue;
                    if(next=="transition"&&end+1<words.size()&&words[end+1]=="fx")continue;
                    break;
                }
                if(mask!=0){out.exclusions|=mask;i=end-1;continue;}
                // These useful negative directions have explicit musical meaning.
                if(words[i+1]=="swing"){affirmative.add("straight groove");++i;continue;}
                if(words[i+1]=="random"){affirmative.add("simple melody");i+=juce::jmin(2,words.size()-i-1);continue;}
                if(words[i+1]=="too"&&i+2<words.size()&&words[i+2]=="busy")
                {affirmative.add("sparse");i+=2;continue;}
                // Denied adjectives/styles are removed from every writer's input.
                ++i;
                if(i+1<words.size()&&(words[i+1]=="house"||words[i+1]=="energy"))++i;
                continue;
            }
            affirmative.add(w);
            if(i+1<words.size())
            {
                const double value=w.getDoubleValue();
                const auto unit=words[i+1].trimCharactersAtEnd(".!?");
                if(value>0.0&&std::isfinite(value))
                {
                    if(unit=="second"||unit=="seconds"||unit=="sec"||unit=="secs")out.durationSeconds=value;
                    if(unit=="minute"||unit=="minutes"||unit=="min"||unit=="mins")out.durationSeconds=value*60.0;
                }
            }
        }
        out.positive=affirmative.joinIntoString(" ");
        if(text.contains("main melody only")||text.contains("single lead"))out.exclusions|=counter;
        return out;
    }
};
} // namespace sonara
