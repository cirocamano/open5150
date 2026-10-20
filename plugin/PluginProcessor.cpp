#include "PluginProcessor.hpp"
#include <limits>

juce::AudioProcessorValueTreeState::ParameterLayout Open5150Processor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1},
        "Canal", juce::StringArray{"Red Lead", "Green Crunch", "Green Clean"}, 0));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bright", 1}, "Bright", false));
    const char* names[] = {"Pre Gain", "Post Gain", "Bass", "Mid", "Treble", "Presence", "Resonance"};
    for (int i = 0; i < 7; ++i)
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[i + 2], 1},
            names[i], juce::NormalisableRange<float>{0, 10, .1f}, i == 1 ? 3.f : 5.f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gate", 1},
        "Noise Gate (dB)", juce::NormalisableRange<float>{-90, -20, 1}, -55));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"cab", 1}, "Cabina IR", true));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mic", 1},
        "EQ de micro (aproximacion)", juce::StringArray{"Dynamic 57", "Dynamic 421", "Ribbon 121"}, 0));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"micX", 1}, "Centro / borde", 0.f, 1.f, .25f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"micZ", 1}, "Distancia", 0.f, 1.f, .15f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"mic45", 1}, "Angulo 45", false));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lowCut", 1}, "Low Cut (Hz)", 20.f, 300.f, 75.f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"highCut", 1}, "High Cut (Hz)", 3000.f, 20000.f, 11500.f));
    return result;
}

Open5150Processor::Open5150Processor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      Thread("OPEN5150 IR loader"), parameters(*this, nullptr, "OPEN5150", layout()),
      cab(std::make_unique<Amp5650::CabSim>()) {
    for (size_t i = 0; i < values.size(); ++i) values[i] = parameters.getRawParameterValue(ids[i]);
    applied.fill(std::numeric_limits<float>::quiet_NaN());
    startThread();
}

Open5150Processor::~Open5150Processor() { stopThread(-1); }

bool Open5150Processor::isBusesLayoutSupported(const BusesLayout& buses) const {
    const auto in = buses.getMainInputChannelSet(), out = buses.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) &&
           (out == in || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()));
}

void Open5150Processor::prepareToPlay(double rate, int) {
    for (auto& amp : amps) amp.prepare(static_cast<float>(rate));
    gate.prepare(static_cast<float>(rate));
    applied.fill(std::numeric_limits<float>::quiet_NaN());
    cab = std::make_unique<Amp5650::CabSim>();
    cab->prepare(static_cast<float>(rate));
    {
        std::lock_guard<std::mutex> lock(irMutex);
        irRate = rate;
        pendingCab.reset();
        pending = false;
        ++requestedVersion;
        irStatus = requestedPath.isEmpty() ? "Sin IR WAV: cabina en bypass" : "Cargando IR WAV…";
    }
    notify();
}

void Open5150Processor::requestIR(const juce::String& path) {
    {
        std::lock_guard<std::mutex> lock(irMutex);
        requestedPath = path;
        ++requestedVersion;
        irStatus = "Cargando IR WAV…";
    }
    notify();
}

juce::String Open5150Processor::getIRStatus() {
    std::lock_guard<std::mutex> lock(irMutex);
    return irStatus;
}

void Open5150Processor::run() {
    while (!threadShouldExit()) {
        juce::String path;
        double rate;
        uint64_t version;
        {
            std::lock_guard<std::mutex> lock(irMutex);
            version = requestedVersion;
            path = requestedPath;
            rate = irRate;
        }
        if (version != loadedVersion) {
            auto next = std::make_unique<Amp5650::CabSim>();
            next->prepare(static_cast<float>(rate));
            const bool ok = path.isEmpty() || next->loadIR(path.toStdString());
            std::lock_guard<std::mutex> lock(irMutex);
            if (version == requestedVersion) {
                if (ok) {
                    savedPath = path;
                    pendingCab.swap(next);
                    pending = true;
                    irStatus = path.isEmpty() ? "Sin IR WAV: cabina en bypass" : "IR: " + juce::File(path).getFileName();
                } else {
                    irStatus = "Error: " + juce::String(next->lastError) + ". Se conserva la IR anterior.";
                    requestedPath = savedPath;
                }
                loadedVersion = version;
            }
        }
        wait(100);
    }
}

void Open5150Processor::updateParameters() {
    std::array<float, std::size(ids)> next;
    for (size_t i = 0; i < next.size(); ++i) next[i] = values[i]->load();
    if (next == applied) return;
    applied = next;
    for (auto& amp : amps) {
        amp.channel = next[0] == 0 ? 1 : 0;
        amp.crunch = next[0] == 1;
        amp.bright = next[1] >= .5f;
        Amp5650::ChannelParameters channel;
        channel.pre = next[2]; channel.post = next[3];
        channel.bass = next[4]; channel.mid = next[5]; channel.treble = next[6];
        channel.presence = next[7]; channel.resonance = next[8];
        channel.apply(amp);
    }
    gate.setThreshold(next[9]);
}

void Open5150Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    {
        std::unique_lock<std::mutex> lock(irMutex, std::try_to_lock);
        if (lock.owns_lock() && pending) {
            cab.swap(pendingCab); // Retire the old convolution buffers on the loader thread.
            pending = false;
        }
    }
    updateParameters();
    cab->setEnabled(applied[10] >= .5f);
    cab->setMicrophone(static_cast<int>(applied[11]));
    cab->setMicPosition(applied[12], applied[13], applied[14] >= .5f);
    cab->setFilters(applied[15], applied[16]);
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() == 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    float monoRight[512];
    for (int offset = 0; offset < buffer.getNumSamples(); offset += 512) {
        const int count = std::min(512, buffer.getNumSamples() - offset);
        float* left = buffer.getWritePointer(0, offset);
        float* right = buffer.getNumChannels() == 2 ? buffer.getWritePointer(1, offset) : monoRight;
        if (right == monoRight) std::copy_n(left, count, right);
        for (int i = 0; i < count; ++i) {
            gate.process(std::abs(left[i]) > std::abs(right[i]) ? left[i] : right[i]);
            left[i] *= gate.currentGain; right[i] *= gate.currentGain;
        }
        amps[0].processBlock(left, count); amps[1].processBlock(right, count);
        cab->processBlock(left, right, static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            left[i] = limiter.process(left[i]); right[i] = limiter.process(right[i]);
        }
    }
}

void Open5150Processor::getStateInformation(juce::MemoryBlock& data) {
    auto state = parameters.copyState();
    {
        std::lock_guard<std::mutex> lock(irMutex);
        // shortcut: sessions reference external WAVs; embed IR data when portable sessions are required.
        state.setProperty("irPath", requestedPath, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, data);
}

void Open5150Processor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("OPEN5150")) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;
    parameters.replaceState(state);
    requestIR(state.getProperty("irPath").toString());
}

class Open5150Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Open5150Editor(Open5150Processor& processor)
        : AudioProcessorEditor(processor), owner(processor), controls(processor) {
        addAndMakeVisible(controls); addAndMakeVisible(browse); addAndMakeVisible(status);
        browse.setButtonText("Cargar IR WAV…");
        browse.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Selecciona una IR WAV", juce::File{}, "*.wav;*.WAV");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<Open5150Editor>(this)](const juce::FileChooser& selected) {
                    if (safe && selected.getResult().existsAsFile())
                        safe->owner.requestIR(selected.getResult().getFullPathName());
                });
        };
        setSize(560, 700);
        startTimerHz(5);
        timerCallback();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        browse.setBounds(area.removeFromTop(30));
        status.setBounds(area.removeFromTop(42));
        controls.setBounds(area);
    }
private:
    Open5150Processor& owner;
    juce::GenericAudioProcessorEditor controls;
    juce::TextButton browse;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override { status.setText(owner.getIRStatus(), juce::dontSendNotification); }
};

juce::AudioProcessorEditor* Open5150Processor::createEditor() { return new Open5150Editor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Open5150Processor(); }
