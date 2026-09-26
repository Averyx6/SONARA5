#pragma once
#include <JuceHeader.h>
#include "../Engine/SoundDNA.h"
#include <atomic>
#include <cmath>

namespace sonara {
struct CyanoryxSoundRequest {
    juce::String role, prompt, key;
    double bpm = 120.0;
    uint64_t projectSeed = 0;
};

class CyanoryxBridge {
public:
    static constexpr int protocolVersion = 4;
    static constexpr int maxPacketChars = 1024 * 1024;

    void connect() noexcept { connected.store(true, std::memory_order_release); }
    void disconnect() noexcept { connected.store(false, std::memory_order_release); }
    bool isConnected() const noexcept { return connected.load(std::memory_order_acquire); }

    juce::String serializePatch(const SoundDNA& dna) const {
        juce::ValueTree packet("SONARA_CYANORYX_PATCH");
        packet.setProperty("protocolVersion", protocolVersion, nullptr);
        packet.setProperty("producer", "SONARA", nullptr);
        packet.setProperty("patchSeed", juce::String(dna.seed), nullptr);
        packet.addChild(dna.toValueTree(), -1, nullptr);
        if (auto xml = packet.createXml()) return xml->toString();
        return {};
    }

    bool deserializePatch(const juce::String& text, SoundDNA& out) const {
        if (!packetSizeOK(text)) return false;
        auto xml = juce::parseXML(text);
        if (!xml) return false;
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid()) return false;

        // Protocol v1 compatibility was a direct SoundDNA tree. Reject unrelated
        // ValueTrees instead of silently turning malformed IPC into an Init patch.
        if (tree.getType().toString() != "SONARA_CYANORYX_PATCH") {
            if (tree.getType().toString() != "SoundDNA") return false;
            out = SoundDNA::fromValueTree(tree);
            return true;
        }

        const int version = (int) tree.getProperty("protocolVersion", 0);
        if (version < 1 || version > protocolVersion || tree.getNumChildren() != 1) return false;
        if (tree.getProperty("producer", "SONARA").toString() != "SONARA") return false;
        auto dnaTree = tree.getChild(0);
        if (!dnaTree.isValid() || dnaTree.getType().toString() != "SoundDNA") return false;
        out = SoundDNA::fromValueTree(dnaTree);
        return true;
    }

    juce::String serializeInterchange(const SoundDNA& dna, const juce::ValueTree& arrangement, double bpm, const juce::String& key) const {
        juce::ValueTree packet("SONARA_CYANORYX_BUNDLE");
        packet.setProperty("protocolVersion", protocolVersion, nullptr);
        packet.setProperty("manifestVersion", 1, nullptr);
        packet.setProperty("producer", "SONARA", nullptr);
        packet.setProperty("consumer", "CYANORYX", nullptr);
        packet.setProperty("tempo", sanitiseBpm(bpm), nullptr);
        packet.setProperty("key", key.substring(0, 32), nullptr);
        packet.setProperty("timingUnits", "beats", nullptr);
        packet.setProperty("containsMidi", arrangement.isValid(), nullptr);
        packet.setProperty("containsSoundDNA", true, nullptr);
        packet.setProperty("containsAutomation", true, nullptr);
        packet.addChild(dna.toValueTree(), -1, nullptr);
        if (arrangement.isValid()) packet.addChild(arrangement.createCopy(), -1, nullptr);
        if (auto xml = packet.createXml()) return xml->toString();
        return {};
    }

    bool deserializeInterchange(const juce::String& text, SoundDNA& dna, juce::ValueTree& arrangement, double& bpm, juce::String& key) const {
        if (!packetSizeOK(text)) return false;
        auto xml = juce::parseXML(text); if (!xml) return false;
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid() || tree.getType().toString() != "SONARA_CYANORYX_BUNDLE") return false;
        const int version = (int) tree.getProperty("protocolVersion", 0);
        if (version < 1 || version > protocolVersion) return false;
        if (tree.getProperty("producer", "SONARA").toString() != "SONARA") return false;
        const auto dnaTree = tree.getChildWithName("SoundDNA"); if (!dnaTree.isValid()) return false;
        dna = SoundDNA::fromValueTree(dnaTree);
        arrangement = tree.getChildWithName("SONARA_ARRANGEMENT").createCopy();
        const double parsedBpm = static_cast<double>(tree.getProperty("tempo", 120.0));
        if (!std::isfinite(parsedBpm) || parsedBpm < 20.0 || parsedBpm > 400.0) return false;
        bpm = parsedBpm; key = tree.getProperty("key", "Unknown").toString().substring(0, 32);
        return true;
    }

    juce::String serializeRequest(const CyanoryxSoundRequest& request) const {
        juce::ValueTree packet("SONARA_CYANORYX_REQUEST");
        packet.setProperty("protocolVersion", protocolVersion, nullptr);
        packet.setProperty("consumer", "CYANORYX", nullptr);
        packet.setProperty("role", request.role.substring(0, 128), nullptr);
        packet.setProperty("prompt", request.prompt.substring(0, 4096), nullptr);
        packet.setProperty("key", request.key.substring(0, 32), nullptr);
        packet.setProperty("bpm", sanitiseBpm(request.bpm), nullptr);
        packet.setProperty("projectSeed", juce::String(request.projectSeed), nullptr);
        if (auto xml = packet.createXml()) return xml->toString();
        return {};
    }

    bool deserializeRequest(const juce::String& text, CyanoryxSoundRequest& out) const {
        if (!packetSizeOK(text)) return false;
        auto xml = juce::parseXML(text);
        if (!xml) return false;
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid() || tree.getType().toString() != "SONARA_CYANORYX_REQUEST") return false;
        const int version = (int) tree.getProperty("protocolVersion", 0);
        if (version < 1 || version > protocolVersion || tree.getNumChildren() != 0) return false;
        if (tree.hasProperty("consumer") && tree.getProperty("consumer").toString() != "CYANORYX") return false;

        CyanoryxSoundRequest candidate;
        candidate.role = tree.getProperty("role", "").toString();
        candidate.prompt = tree.getProperty("prompt", "").toString();
        candidate.key = tree.getProperty("key", "").toString();
        if (candidate.role.length() > 128 || candidate.prompt.length() > 4096 || candidate.key.length() > 32) return false;
        const double bpm = static_cast<double>(tree.getProperty("bpm", 120.0));
        if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0) return false;
        candidate.bpm = bpm;
        const auto seedText = tree.getProperty("projectSeed", "0").toString();
        if (seedText.length() > 20 || seedText.containsOnly("0123456789") == false) return false;
        candidate.projectSeed = static_cast<uint64_t>(seedText.getLargeIntValue());
        out = candidate;
        return true;
    }

private:
    static bool packetSizeOK(const juce::String& text) noexcept { return text.isNotEmpty() && text.length() <= maxPacketChars; }
    static double sanitiseBpm(double bpm) noexcept { return std::isfinite(bpm) ? juce::jlimit(20.0, 400.0, bpm) : 120.0; }
    std::atomic<bool> connected { false };
};
}