#include "AudioEngineHAL.hpp"
#include "tests/fixtures/DspRegression.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

using namespace Amp5650;

static void wav(const std::string& path, const std::vector<float>& samples, uint32_t rate,
                uint16_t channels = 1, uint16_t bits = 32, uint16_t format = 3) {
    std::ofstream f(path, std::ios::binary);
    auto put16 = [&](uint16_t n) { f.put(n & 255); f.put(n >> 8); };
    auto put32 = [&](uint32_t n) { put16(n & 65535); put16(n >> 16); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * bits / 8);
    f.write("RIFF", 4); put32(36 + bytes); f.write("WAVEfmt ", 8); put32(16);
    put16(format); put16(channels); put32(rate); put32(rate * channels * bits / 8);
    put16(channels * bits / 8); put16(bits); f.write("data", 4); put32(bytes);
    for (float x : samples) {
        if (format == 3) f.write(reinterpret_cast<const char*>(&x), 4);
        else {
            int32_t v = static_cast<int32_t>(x * (bits == 16 ? 32767 : 8388607));
            for (int byte = 0; byte < bits / 8; ++byte) f.put((v >> (8 * byte)) & 255);
        }
    }
    assert(f.good());
}

namespace Amp5650 {
struct LiveAudioEngineTest {
    static void run(const std::string& irPath) {
        LiveAudioEngine engine;
        engine.ampL.prepare(48000); engine.ampR.prepare(48000);
        engine.activeCab = std::make_unique<CabSim>();
        engine.setCabEnabled(false);
        engine.setPreGain(8); engine.setBass(9);
        engine.setMode(0, 1); engine.setBright(1);
        engine.setToneStack(2, 3, 4);
        engine.applyPendingParameters();
        assert(engine.ampL.channel == 0 && engine.ampL.crunch == 1 && engine.ampL.bright == 1);
        assert(engine.ampL.toneStack.currentBright == 1);
        assert(engine.ampL.bass == 2 && engine.ampL.mid == 3 && engine.ampL.treble == 4);
        const auto before = engine.ampL.toneStack.bassBoost.filters[0];
        engine.setBright(1); engine.applyPendingParameters();
        assert(engine.ampL.toneStack.bassBoost.filters[0].b0 == before.b0);
        engine.setChannel(1); engine.applyPendingParameters();
        assert(engine.ampL.preRed == 8 && engine.ampL.bass == 9);
        engine.setChannel(0); engine.applyPendingParameters();
        assert(engine.ampL.bass == 2 && engine.ampL.preGreen == 5);
        engine.setBass(std::numeric_limits<float>::quiet_NaN());
        assert(engine.getParameters().channels[0].bass == 2);
        engine.setPreGain(99);
        assert(engine.getParameters().channels[0].pre == 10);

        std::thread writer([&] {
            for (int i = 0; i < 2000; ++i) {
                engine.setBass(i % 11); engine.setBright(i % 2);
                if (i % 20 == 0) assert(engine.loadCabinetIR(irPath));
            }
        });
        float l[64]{}, r[64]{};
        for (int i = 0; i < 2000; ++i) {
            engine.processDSP(l, r, 64);
            for (float value : l) assert(std::isfinite(value));
        }
        writer.join();
    }
};
}

static void cabinets(const std::filesystem::path& temp) {
    std::vector<float> immediate(2048), delayed(2048);
    immediate[0] = 1; delayed[1400] = 1;
    const auto a = (temp / "immediate.wav").string(), b = (temp / "delayed.wav").string();
    wav(a, immediate, 48000); wav(b, delayed, 48000);
    CabSim ca, cb;
    ca.prepare(48000); cb.prepare(48000);
    assert(!ca.enabled && ca.lastError.empty());
    float dryL = .25f, dryR = -.5f;
    ca.processBlock(&dryL, &dryR, 1);
    assert(dryL == .25f && dryR == -.5f);
    assert(ca.loadIR(a) && cb.loadIR(b));
    std::vector<float> al(1600), ar(1600), bl(1600), br(1600);
    al[0] = ar[0] = bl[0] = br[0] = 1;
    ca.processBlock(al.data(), ar.data(), al.size());
    cb.processBlock(bl.data(), br.data(), bl.size());
    for (int i = 0; i < 200; ++i) assert(std::abs(al[i] - bl[i+1400]) < 1e-5f);
    assert(std::abs(bl[1400]) > .01f);
    assert(!cb.loadIR((temp / "missing.wav").string()));
    assert(cb.customIrPath == b);
    std::ofstream(temp / "truncated.wav", std::ios::binary).write("RIFF0000WAVE", 12);
    assert(!cb.loadIR((temp / "truncated.wav").string()));
    std::filesystem::copy_file(a, temp / "unsupported.aacabnew");
    assert(!cb.loadIR((temp / "unsupported.aacabnew").string()));
    assert(cb.customIrPath == b && cb.enabled);
    delayed[0] = std::numeric_limits<float>::quiet_NaN();
    wav((temp / "nan.wav").string(), delayed, 48000);
    assert(!cb.loadIR((temp / "nan.wav").string()));
    wav((temp / "long.wav").string(), std::vector<float>(96001), 48000);
    assert(!cb.loadIR((temp / "long.wav").string()));
    assert(cb.customIrPath == b && cb.enabled);
    cb.setEnabled(false);
    cb.prepare(96000);
    assert(!cb.enabled && cb.customIrPath == b);

    for (int bits : {16, 24, 32}) {
        std::vector<float> stereo(4096);
        stereo[120] = .25f; stereo[121] = .5f;
        const auto file = (temp / ("stereo" + std::to_string(bits) + ".wav")).string();
        wav(file, stereo, 48000, 2, bits, bits == 32 ? 3 : 1);
        assert(ca.loadIR(file));
        std::fill(al.begin(), al.end(), 0); std::fill(ar.begin(), ar.end(), 0);
        al[0] = ar[0] = 1;
        ca.processBlock(al.data(), ar.data(), al.size());
        for (size_t i = 0; i < al.size(); ++i) assert(std::abs(ar[i] - 2 * al[i]) < 1e-4f);
    }

    std::vector<float> ir(4410);
    ir[2205] = 1;
    const auto file = (temp / "resample.wav").string();
    wav(file, ir, 44100);
    assert(ca.loadIR(file));
    al.assign(4800, 0); ar.assign(4800, 0); al[0] = ar[0] = 1;
    ca.processBlock(al.data(), ar.data(), al.size());
    const auto peak = std::max_element(al.begin(), al.end(), [](float x, float y) { return std::abs(x) < std::abs(y); });
    assert(std::abs((peak - al.begin()) - 2400) < 10);
}

static void blockAndReset() {
    for (float rate : {44100.f, 48000.f, 96000.f}) for (int mode = 0; mode < 3; ++mode) {
        Amp5650Engine whole, split;
        for (auto* amp : {&whole, &split}) {
            amp->channel = mode == 0; amp->crunch = mode == 1;
            amp->prepare(rate);
        }
        std::array<float, 4096> input{}, a, b;
        for (size_t i = 0; i < input.size(); ++i) input[i] = i < 1024 ? .1f * std::sin(i * .13f) : 0;
        a = b = input;
        whole.processBlock(a.data(), a.size());
        for (size_t offset = 0; offset < b.size(); offset += 73)
            split.processBlock(b.data() + offset, std::min<size_t>(73, b.size() - offset));
        assert(a == b);
        split.reset(); b = input; split.processBlock(b.data(), b.size());
        assert(a == b);
    }
}

static void dspRegression() {
    for (const auto& fixture : dspRegressionCases) {
        TubeStage tube;
        PowerAmp power;
        if (fixture.tubeConfig >= 0) {
            const int config = fixture.tubeConfig;
            tube.prepare(fixture.rate, config == 1 ? 43.9458f : config == 2 ? 5.14f : 1.f, config == 2 ? .5f : .7f);
            tube.dryGain = config == 1 ? -.0512f : config == 2 ? .1f : 0.f;
        } else {
            power.prepare(fixture.rate);
            power.presenceAmount = .159776f; power.resonanceAmount = 1.77f;
        }
        for (int i = 0; i < 24000; ++i) {
            const float input = i < 8000 ? -.7f : i < 16000 ? .8f * std::sin(i * .137f) : 0.f;
            const float output = fixture.tubeConfig >= 0 ? tube.process(input) : power.process(input);
            if (i % 1000 == 0) assert(std::abs(output - fixture.expected[i / 1000]) < (fixture.tubeConfig >= 0 ? 4e-6f : 2e-6f));
        }
    }
}

int main() {
    char directory[] = "/tmp/5650-audio-tests-XXXXXX";
    assert(mkdtemp(directory));
    const std::filesystem::path temp(directory);
    dspRegression();
    blockAndReset();
    cabinets(temp);
    LiveAudioEngineTest::run((temp / "immediate.wav").string());
    for (float rate : {44100.f, 48000.f, 96000.f}) {
        TubeStage tube;
        tube.prepare(rate);
        const double expected = 1 - 1 / (double(rate) * .001 * 3 * 4.535147190093994);
        assert(std::abs(tube.dcPole - expected) < 1e-12);
        Amp5650Engine amp;
        amp.prepare(rate);
        for (int channel : {0, 1}) for (int crunch : {0, 1}) for (float gain : {0.f, 5.f, 10.f}) {
            amp.channel = channel; amp.crunch = crunch; amp.preRed = amp.preGreen = gain;
            amp.updateGains();
            float block[512];
            for (int i = 0; i < 512; ++i) block[i] = .1f * std::sin(float(i) * .13f);
            amp.processBlock(block, 512);
            for (float x : block) assert(std::isfinite(x));
        }
    }
    Biquad filter;
    filter.setup(FilterType::Lowpass, 1000, .7f, 0, 48000);
    const double before = filter.b0;
    filter.setup(FilterType::Lowpass, 10000, .7f, 0, 48000);
    assert(filter.b0 == before);
    for (int i = 0; i < 240; ++i) assert(std::isfinite(filter.process(.1f)));
    assert(filter.b0 != before);
    filter.setup(FilterType::Lowpass, 96000, .7f, 0, 44100);
    for (int i = 0; i < 512; ++i) assert(std::isfinite(filter.process(.1f)));
    std::filesystem::remove_all(temp);
    std::cout << "Audio regression checks passed.\n";
}
