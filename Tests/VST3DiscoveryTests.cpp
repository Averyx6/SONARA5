#include <JuceHeader.h>
#include <iostream>

int main(int argc, char** argv)
{
#if !JUCE_PLUGINHOST_VST3
    std::cerr << "JUCE_PLUGINHOST_VST3 is disabled\n";
    return 2;
#else
    if (argc < 2)
    {
        std::cerr << "Expected path to built SONARA VST3 module\n";
        return 2;
    }

    const juce::File moduleFile(argv[1]);
    if (!moduleFile.existsAsFile())
    {
        std::cerr << "VST3 module binary missing: " << moduleFile.getFullPathName() << "\n";
        return 3;
    }

    // Windows VST3 bundle layout:
    // SONARA.vst3/Contents/x86_64-win/SONARA.vst3
    const juce::File bundle = moduleFile.getParentDirectory()
                                         .getParentDirectory()
                                         .getParentDirectory();
    if (!bundle.isDirectory() || bundle.getFileExtension().toLowerCase() != ".vst3")
    {
        std::cerr << "Invalid VST3 bundle layout: " << bundle.getFullPathName() << "\n";
        return 4;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, bundle.getFullPathName());
    if (types.isEmpty())
    {
        std::cerr << "JUCE VST3 scanner could not discover SONARA\n";
        return 5;
    }

    const auto& d = *types[0];
    if (d.name != "SONARA" || !d.isInstrument || d.pluginFormatName != "VST3")
    {
        std::cerr << "Unexpected plugin description: name=" << d.name
                  << " format=" << d.pluginFormatName
                  << " instrument=" << (d.isInstrument ? "true" : "false") << "\n";
        return 6;
    }

    juce::String error;
    auto instance = format.createInstanceFromDescription(d, 44100.0, 512, error);
    if (instance == nullptr)
    {
        std::cerr << "VST3 discovered but failed to instantiate: " << error << "\n";
        return 7;
    }

    instance->prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    audio.clear();
    instance->processBlock(audio, midi);
    instance->releaseResources();

    std::cout << "SONARA VST3 discovery + instantiation passed: "
              << bundle.getFullPathName() << "\n";
    return 0;
#endif
}
