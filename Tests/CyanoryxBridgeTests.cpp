#include <JuceHeader.h>
#include "../Source/Integration/CyanoryxBridge.h"
#include <iostream>
#include <cmath>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}

int main() {
    sonara::CyanoryxBridge bridge;

    sonara::SoundDNA source;
    source.seed = 987654321ULL;
    source.name = "Bridge Test";
    source.cutoff = 4321.0f;
    sonara::SoundDNA decoded;
    expect(bridge.deserializePatch(bridge.serializePatch(source), decoded), "current patch round-trip parses");
    expect(decoded.seed == source.seed && std::abs(decoded.cutoff - source.cutoff) < 0.01f, "current patch preserves DSP identity");

    const auto legacy = source.toValueTree().createXml()->toString();
    expect(bridge.deserializePatch(legacy, decoded), "legacy direct SoundDNA remains supported");
    juce::ValueTree unrelated("NOT_SOUND_DNA");
    expect(!bridge.deserializePatch(unrelated.createXml()->toString(), decoded), "unrelated legacy tree is rejected");

    sonara::CyanoryxSoundRequest request;
    request.role = "drop lead"; request.prompt = "wide emotional progressive lead"; request.key = "F minor";
    request.bpm = 128.0; request.projectSeed = 123456ULL;
    sonara::CyanoryxSoundRequest parsed;
    expect(bridge.deserializeRequest(bridge.serializeRequest(request), parsed), "request round-trip parses");
    expect(parsed.role == request.role && parsed.prompt == request.prompt && parsed.key == request.key, "request text fields round-trip");
    expect(std::abs(parsed.bpm - 128.0) < 0.001 && parsed.projectSeed == request.projectSeed, "request context round-trips");

    juce::ValueTree arrangement("SONARA_ARRANGEMENT");
    arrangement.setProperty("bars", 72, nullptr);
    auto bundle = bridge.serializeInterchange(source, arrangement, 128.0, "F minor");
    sonara::SoundDNA bundledDna; juce::ValueTree bundledArrangement; double bundledBpm=0.0; juce::String bundledKey;
    expect(bridge.deserializeInterchange(bundle, bundledDna, bundledArrangement, bundledBpm, bundledKey), "v4 project bundle round-trip parses");
    expect(bundledDna.seed == source.seed && bundledArrangement.getType().toString() == "SONARA_ARRANGEMENT", "v4 bundle preserves SoundDNA and arrangement");
    expect(std::abs(bundledBpm - 128.0) < 0.001 && bundledKey == "F minor", "v4 bundle preserves musical context");

    juce::ValueTree future("SONARA_CYANORYX_REQUEST");
    future.setProperty("protocolVersion", sonara::CyanoryxBridge::protocolVersion + 1, nullptr);
    future.setProperty("bpm", 128.0, nullptr); future.setProperty("projectSeed", "1", nullptr);
    expect(!bridge.deserializeRequest(future.createXml()->toString(), parsed), "future protocol is rejected");

    juce::ValueTree badBpm("SONARA_CYANORYX_REQUEST");
    badBpm.setProperty("protocolVersion", sonara::CyanoryxBridge::protocolVersion, nullptr); badBpm.setProperty("bpm", 900.0, nullptr); badBpm.setProperty("projectSeed", "1", nullptr);
    expect(!bridge.deserializeRequest(badBpm.createXml()->toString(), parsed), "out-of-range BPM is rejected");

    juce::ValueTree badSeed("SONARA_CYANORYX_REQUEST");
    badSeed.setProperty("protocolVersion", sonara::CyanoryxBridge::protocolVersion, nullptr); badSeed.setProperty("bpm", 120.0, nullptr); badSeed.setProperty("projectSeed", "not-a-seed", nullptr);
    expect(!bridge.deserializeRequest(badSeed.createXml()->toString(), parsed), "malformed project seed is rejected");

    if (failures == 0) std::cout << "Cyanoryx bridge tests passed\n";
    return failures == 0 ? 0 : 1;
}