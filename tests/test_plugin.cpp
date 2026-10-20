#include "../plugin/PluginProcessor.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>

static void set(Open5150Processor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    assert(parameter);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

static void waitForIR(Open5150Processor& processor, const juce::String& prefix) {
    for (int i = 0; i < 200; ++i) {
        if (processor.getIRStatus().startsWith(prefix)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "IR loader timed out");
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MidiBuffer midi;
    if (argc == 3) {
        // Register only in this test process, without installing into the user's plugin folders.
        const auto path = juce::String(argv[1]);
        auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8*>(path.toRawUTF8()), path.getNumBytesAsUTF8(), true);
        assert(url);
        auto bundle = CFBundleCreate(nullptr, url);
        CFRelease(url);
        assert(bundle && CFBundleLoadExecutable(bundle));
        auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
            CFBundleGetFunctionPointerForName(bundle, CFSTR("OPEN5150AUFactory")));
        assert(factory);
        const AudioComponentDescription component{'aufx', 'Ofiv', 'Opam', 0, 0};
        assert(AudioComponentRegister(&component, CFSTR("OPEN5150: OPEN5150"), 0x100, factory));
        juce::AudioPluginFormatManager formats;
        formats.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
        for (int i = 0; i < 2; ++i) {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            formats.getFormat(i)->findAllTypesForFile(descriptions, argv[i + 1]);
            assert(descriptions.size() == 1);
            juce::String error;
            auto instance = formats.createPluginInstance(*descriptions[0], 48000, 64, error);
            if (!instance) std::cerr << error << '\n';
            assert(instance);
            instance->prepareToPlay(48000, 64);
            juce::AudioBuffer<float> audio(2, 64);
            audio.clear(); audio.setSample(0, 0, .1f); audio.setSample(1, 0, .1f);
            instance->processBlock(audio, midi);
            assert(audio.getMagnitude(0, 64) > 0);
            for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 64; ++sample)
                assert(std::isfinite(audio.getSample(ch, sample)));
            juce::MemoryBlock state;
            instance->getStateInformation(state);
            assert(state.getSize() > 0);
            instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
            assert(editor && editor->getWidth() > 0);
            editor.reset();
            instance->releaseResources();
            std::cout << formats.getFormat(i)->getName() << " bundle loaded and processed.\n";
        }
        CFRelease(bundle);
        return 0;
    }
    for (double rate : {44100., 48000., 96000.}) {
        for (int channels : {1, 2}) {
            Open5150Processor processor;
            const auto layout = juce::AudioChannelSet::canonicalChannelSet(channels);
            assert(processor.setBusesLayout({{layout}, {layout}}));
            processor.prepareToPlay(rate, 64);
            for (int mode = 0; mode < 3; ++mode) {
                set(processor, "mode", static_cast<float>(mode));
                juce::AudioBuffer<float> audio(channels, 1057);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < audio.getNumSamples(); ++i)
                        audio.setSample(ch, i, .1f * std::sin(i * .13f));
                processor.processBlock(audio, midi);
                assert(audio.getMagnitude(0, audio.getNumSamples()) > 0);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < audio.getNumSamples(); ++i) {
                        assert(std::isfinite(audio.getSample(ch, i)));
                        assert(std::abs(audio.getSample(ch, i)) <= .94401f);
                    }
            }
        }
    }
    Open5150Processor original;
    assert(original.setBusesLayout({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::stereo()}}));
    assert(!original.isBusesLayoutSupported({{juce::AudioChannelSet::stereo()}, {juce::AudioChannelSet::mono()}}));
    original.prepareToPlay(48000, 64);
    set(original, "pre", 8.5f); set(original, "mode", 2);
    auto wav = juce::File::createTempFile(".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
        assert(stream);
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        assert(writer);
        juce::AudioBuffer<float> impulse(1, 512);
        impulse.clear(); impulse.setSample(0, 0, .5f);
        assert(writer->writeFromAudioSampleBuffer(impulse, 0, impulse.getNumSamples()));
    }
    original.requestIR(wav.getFullPathName());
    waitForIR(original, "IR:");
    juce::MemoryBlock saved;
    original.getStateInformation(saved);
    Open5150Processor restored;
    restored.prepareToPlay(48000, 64);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    waitForIR(restored, "IR:");
    assert(restored.parameters.getRawParameterValue("pre")->load() == 8.5f);
    assert(restored.parameters.getRawParameterValue("mode")->load() == 2.f);
    juce::AudioBuffer<float> audio(2, 128);
    audio.clear(); audio.setSample(0, 0, .1f);
    original.processBlock(audio, midi);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        assert(audio.getSample(0, i) == audio.getSample(1, i));
    restored.requestIR(wav.getFullPathName() + ".missing.wav");
    waitForIR(restored, "Error:");
    juce::MemoryBlock afterError;
    restored.getStateInformation(afterError);
    assert(afterError == saved);
    restored.setStateInformation("invalid", 7);
    assert(restored.parameters.getRawParameterValue("pre")->load() == 8.5f);
    restored.prepareToPlay(96000, 128);
    waitForIR(restored, "IR:");
    audio.clear();
    restored.processBlock(audio, midi);
    assert(wav.deleteFile());
    std::cout << "Plugin regression checks passed.\n";
}
