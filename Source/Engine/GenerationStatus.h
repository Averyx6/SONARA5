#pragma once
#include <JuceHeader.h>
namespace sonara {
// Status is read by the GUI while planning/rendering works on a control worker.
class GenerationStatus {
public:
    GenerationStatus()=default;
    GenerationStatus& operator=(const juce::String& value){const juce::ScopedLock guard(lock);text=value;return *this;}
    operator juce::String() const {const juce::ScopedLock guard(lock);return text;}
    bool containsIgnoreCase(const juce::String& value) const{return juce::String(*this).containsIgnoreCase(value);}
private:
    mutable juce::CriticalSection lock;juce::String text{"Ready"};
};
}
