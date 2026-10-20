#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <limits>
#include "AudioEngineHAL.hpp"
#include "AudioDeviceManager.hpp"

using namespace Amp5650;

std::atomic<bool> gRunning(true);

void sigHandler(int) {
    gRunning.store(false);
}

// POSIX Non-blocking keyboard helper
class NonBlockingInput {
public:
    NonBlockingInput() {
        tcgetattr(STDIN_FILENO, &orig_termios);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    }
    ~NonBlockingInput() {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        fcntl(STDIN_FILENO, F_SETFL, oldf);
    }
    int getChar() {
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) > 0) return ch;
        return -1;
    }
private:
    struct termios orig_termios;
    int oldf;
};

// Render an ASCII VU meter bar
std::string renderBar(float db, int width = 24) {
    // Range: -60 dB to 0 dB
    float norm = (db + 60.0f) / 60.0f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    int filled = (int)(norm * width);

    std::string bar = "";
    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            float posDb = -60.0f + (float)i / width * 60.0f;
            if (posDb > -3.0f) {
                bar += "\033[1;31m=\033[0m"; // Red
            } else if (posDb > -12.0f) {
                bar += "\033[1;33m=\033[0m"; // Yellow
            } else {
                bar += "\033[1;32m=\033[0m"; // Green
            }
        } else {
            bar += "\033[90m-\033[0m";
        }
    }
    return bar;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    std::cout << "\033[2J\033[H"; // Clear screen
    std::cout << "\033[1;36m===================================================================\033[0m\n";
    std::cout << "\033[1;37m   OPEN5150 Live Guitar Amplifier & Real-Time Low-Latency DSP Engine  \033[0m\n";
    std::cout << "\033[1;36m===================================================================\033[0m\n\n";

    auto devices = AudioDeviceManager::enumerateDevices();

    if (argc >= 2 && std::string(argv[1]) == "--list-devices") {
        std::cout << "Dispositivos de audio disponibles en macOS:\n";
        for (const auto& d : devices) {
            std::cout << " [" << d.id << "] " << d.name
                      << " (In: " << d.inputChannels << ", Out: " << d.outputChannels << ")"
                      << (d.isDefaultInput ? " \033[1;32m[Default Input]\033[0m" : "")
                      << (d.isDefaultOutput ? " \033[1;34m[Default Output]\033[0m" : "")
                      << "\n";
        }
        return 0;
    }

    AudioDeviceID inDevId = AudioDeviceManager::getDefaultInputDeviceID();
    AudioDeviceID outDevId = AudioDeviceManager::getDefaultOutputDeviceID();
    UInt32 requestedBuffer = 64; // 64 samples = 1.33 ms a 48kHz por defecto

    std::string irPath;
    std::vector<std::string> positional;
    const auto number = [](const std::string& text) -> UInt32 {
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Número no válido: " + text);
        const auto value = std::stoul(text);
        if (value > std::numeric_limits<UInt32>::max()) throw std::out_of_range("Número demasiado grande");
        return static_cast<UInt32>(value);
    };
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Uso: open5150_live [entrada salida [buffer]] [--buffer N] [--ir archivo.wav]\n";
                return 0;
            }
            if ((arg == "--buffer" || arg == "-b" || arg == "--ir") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (arg == "--ir") irPath = value;
                else requestedBuffer = number(value);
            } else if (!arg.empty() && arg[0] != '-') positional.push_back(arg);
            else throw std::invalid_argument("Argumento desconocido o sin valor: " + arg);
        }
        if (!positional.empty()) {
            if (positional.size() < 2 || positional.size() > 3)
                throw std::invalid_argument("Indica entrada y salida, y opcionalmente buffer");
            inDevId = number(positional[0]);
            outDevId = number(positional[1]);
            if (positional.size() == 3) requestedBuffer = number(positional[2]);
        }
        if (requestedBuffer != 32 && requestedBuffer != 64 && requestedBuffer != 128 && requestedBuffer != 256)
            throw std::invalid_argument("Buffer permitido: 32, 64, 128 o 256");
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    if (positional.empty()) {
        std::cout << "\033[1;33m[1/3] Seleccione Dispositivo de ENTRADA (Guitarra / Mic):\033[0m\n";
        std::vector<AudioDeviceInfo> inputs;
        for (const auto& d : devices) {
            if (d.inputChannels > 0) inputs.push_back(d);
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            std::cout << "  " << (i + 1) << ") [" << inputs[i].id << "] " << inputs[i].name
                      << (inputs[i].isDefaultInput ? " \033[1;32m(Por Defecto)\033[0m" : "") << "\n";
        }
        std::cout << "Presione [Enter] para predeterminado o escriba número (1-" << inputs.size() << "): ";
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) {
            int idx = std::stoi(line) - 1;
            if (idx >= 0 && idx < (int)inputs.size()) inDevId = inputs[idx].id;
        }

        std::cout << "\n\033[1;33m[2/3] Seleccione Dispositivo de SALIDA (Monitores / Auriculares):\033[0m\n";
        std::vector<AudioDeviceInfo> outputs;
        for (const auto& d : devices) {
            if (d.outputChannels > 0) outputs.push_back(d);
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            std::cout << "  " << (i + 1) << ") [" << outputs[i].id << "] " << outputs[i].name
                      << (outputs[i].isDefaultOutput ? " \033[1;34m(Por Defecto)\033[0m" : "") << "\n";
        }
        std::cout << "Presione [Enter] para predeterminado o escriba número (1-" << outputs.size() << "): ";
        std::getline(std::cin, line);
        if (!line.empty()) {
            int idx = std::stoi(line) - 1;
            if (idx >= 0 && idx < (int)outputs.size()) outDevId = outputs[idx].id;
        }

        std::cout << "\n\033[1;33m[3/3] Seleccione Tamaño de Buffer:\033[0m\n";
        std::cout << "  0) \033[1;35m32 samples\033[0m  (~0.67 ms a 48 kHz - Latencia extrema / tiempo real)\n";
        std::cout << "  1) \033[1;32m64 samples\033[0m  (~1.33 ms a 48 kHz - Ultra-baja latencia) [Recomendado]\n";
        std::cout << "  2) 128 samples (~2.67 ms a 48 kHz - Balance seguro)\n";
        std::cout << "  3) 256 samples (~5.33 ms a 48 kHz - Baja carga CPU)\n";
        std::cout << "Presione [Enter] para 64 samples o elija (0-3): ";
        std::getline(std::cin, line);
        if (!line.empty()) {
            if (line == "0" || line == "32") requestedBuffer = 32;
            else if (line == "1" || line == "64") requestedBuffer = 64;
            else if (line == "2" || line == "128") requestedBuffer = 128;
            else if (line == "3" || line == "256") requestedBuffer = 256;
        }
    }

    LiveAudioEngine engine;
    engine.inputDeviceID = inDevId;
    engine.outputDeviceID = outDevId;
    engine.bufferSize = requestedBuffer;

    std::string inName = AudioDeviceManager::getDeviceName(inDevId);
    std::string outName = AudioDeviceManager::getDeviceName(outDevId);

    std::cout << "\nIniciando motor de audio CoreAudio HAL...\n";
    std::cout << "  Entrada: [" << inDevId << "] " << inName << "\n";
    std::cout << "  Salida:  [" << outDevId << "] " << outName << "\n";
    std::cout << "  Buffer solicitado: " << requestedBuffer << " samples\n";
    std::cout << "  Salida:  [" << outDevId << "] " << outName << "\n";

    if (!engine.start()) {
        std::cerr << "\033[1;31mError fatal: No se pudo inicializar CoreAudio HAL en los dispositivos seleccionados.\033[0m\n";
        return 1;
    }

    if (!irPath.empty() && !engine.loadCabinetIR(irPath)) {
        std::cerr << "No se pudo cargar la IR WAV: " << irPath << "\n";
        engine.stop();
        return 1;
    }

    // Parámetros iniciales
    float preGain = 5.0f;
    float postGain = 3.0f;
    float bass = 5.0f;
    float mid = 5.0f;
    float treble = 5.0f;
    float presence = 5.0f;
    float resonance = 5.0f;
    float gateThresh = -55.0f;
    int channel = 1; // 1 = Red, 0 = Green

    engine.setChannel(channel);
    engine.setPreGain(preGain);
    engine.setPostGain(postGain);
    engine.setToneStack(bass, mid, treble);
    engine.setPowerAmp(presence, resonance, postGain);
    engine.setNoiseGateThreshold(gateThresh);

    NonBlockingInput input;
    std::cout << "\033[?25l"; // Ocultar cursor

    auto lastRefresh = std::chrono::steady_clock::now();

    while (gRunning.load()) {
        int ch = input.getChar();
        if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == 'c' || ch == 'C') {
            channel = (channel == 1) ? 0 : 1;
            engine.setChannel(channel);
            const auto state = engine.getParameters();
            const auto& selected = state.channels[state.channel];
            preGain = selected.pre; postGain = selected.post;
            bass = selected.bass; mid = selected.mid; treble = selected.treble;
            presence = selected.presence; resonance = selected.resonance;
        } else if (ch == 'g' || ch == 'G') {
            gateThresh += 3.0f;
            if (gateThresh > -25.0f) gateThresh = -80.0f;
            engine.setNoiseGateThreshold(gateThresh);
        } else if (ch == 'p') {
            preGain = std::max(0.0f, preGain - 0.5f);
            engine.setPreGain(preGain);
        } else if (ch == 'P') {
            preGain = std::min(10.0f, preGain + 0.5f);
            engine.setPreGain(preGain);
        } else if (ch == 'b') {
            bass = std::max(0.0f, bass - 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 'B') {
            bass = std::min(10.0f, bass + 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 'm') {
            mid = std::max(0.0f, mid - 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 'M') {
            mid = std::min(10.0f, mid + 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 't') {
            treble = std::max(0.0f, treble - 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 'T') {
            treble = std::min(10.0f, treble + 0.5f);
            engine.setToneStack(bass, mid, treble);
        } else if (ch == 'v') {
            postGain = std::max(0.0f, postGain - 0.5f);
            engine.setPostGain(postGain);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'V') {
            postGain = std::min(10.0f, postGain + 0.5f);
            engine.setPostGain(postGain);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'r') {
            resonance = std::max(0.0f, resonance - 0.5f);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'R') {
            resonance = std::min(10.0f, resonance + 0.5f);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'e') {
            presence = std::max(0.0f, presence - 0.5f);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'E') {
            presence = std::min(10.0f, presence + 0.5f);
            engine.setPowerAmp(presence, resonance, postGain);
        } else if (ch == 'i' || ch == 'I') {
            static int mId = 0;
            mId = (mId + 1) % 3;
            engine.setMicrophone(mId);
        } else if (ch == 'x') {
            engine.setMicPosition(std::max(0.0f, engine.cabSim.posX - 0.05f), engine.cabSim.distanceZ, engine.cabSim.is45Degrees);
        } else if (ch == 'X') {
            engine.setMicPosition(std::min(1.0f, engine.cabSim.posX + 0.05f), engine.cabSim.distanceZ, engine.cabSim.is45Degrees);
        } else if (ch == 'o' || ch == 'O') {
            engine.setCabEnabled(!engine.cabSim.enabled);
        } else if (ch == ' ' || ch == 'u' || ch == 'U') {
            if (!engine.isRecording()) {
                engine.startRecording();
            } else {
                engine.stopRecording();
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh).count() >= 33) {
            lastRefresh = now;

            float pIn = engine.meters.peakIn.load(std::memory_order_relaxed);
            float rIn = engine.meters.rmsIn.load(std::memory_order_relaxed);
            float pInDb = (pIn > 1e-4f) ? 20.0f * std::log10(pIn) : -80.0f;
            float rInDb = (rIn > 1e-4f) ? 20.0f * std::log10(rIn) : -80.0f;

            bool gOpen = engine.meters.gateOpen.load(std::memory_order_relaxed);
            float gRed = engine.meters.gateReductionDb.load(std::memory_order_relaxed);

            float pOut = engine.meters.peakOut.load(std::memory_order_relaxed);
            float rOut = engine.meters.rmsOut.load(std::memory_order_relaxed);
            float pOutDb = (pOut > 1e-4f) ? 20.0f * std::log10(pOut) : -80.0f;
            float rOutDb = (rOut > 1e-4f) ? 20.0f * std::log10(rOut) : -80.0f;

            std::cout << "\033[H"; // Move cursor to top-left without flickering
            std::cout << "\033[1;36m===================================================================\033[0m\n";
            std::cout << "\033[1;37m   OPEN5150 Live Guitar Amplifier & Real-Time Low-Latency DSP Engine  \033[0m\n";
            std::cout << "\033[1;36m===================================================================\033[0m\n";
            std::cout << " Entrada: \033[1;32m" << inName << "\033[0m | Salida: \033[1;34m" << outName << "\033[0m | "
                      << (int)engine.sampleRate << " Hz\n";
            float latencyMs = (float)engine.bufferSize / (float)engine.sampleRate * 1000.0f;
            std::cout << " Latencia buffer: \033[1;33m~" << std::fixed << std::setprecision(2) << latencyMs
                      << " ms\033[0m (" << engine.bufferSize << " samples)\n\n";

            // VUMETERS
            std::cout << "\033[1;37m--- VÚMETROS EN TIEMPO REAL ---\033[0m\n";
            std::cout << " IN  [ " << renderBar(pInDb, 28) << " ] "
                      << std::fixed << std::setprecision(1) << std::setw(5) << pInDb << " dBFS (RMS " << rInDb << ")\n";

            std::cout << " GATE: ";
            if (gOpen) {
                std::cout << "\033[1;42;30m OPEN \033[0m [ " << "\033[1;32mPASS-THROUGH\033[0m            ]   0.0 dB\n";
            } else {
                std::cout << "\033[1;41;37m MUTE \033[0m [ " << "\033[1;31mATTENUATING\033[0m             ] "
                          << std::fixed << std::setprecision(1) << std::setw(5) << gRed << " dB\n";
            }

            std::cout << " OUT [ " << renderBar(pOutDb, 28) << " ] "
                      << std::fixed << std::setprecision(1) << std::setw(5) << pOutDb << " dBFS (RMS " << rOutDb << ")\n\n";

            // AMP CONTROLS
            std::cout << "\033[1;37m--- ESTADO DEL AMPLIFICADOR OPEN5150 ---\033[0m\n";
            std::cout << " CANAL:   ";
            if (channel == 1) {
                std::cout << "\033[1;41;37m [ RED / LEAD ] \033[0m (8 etapas DSP de alta ganancia)\n";
            } else {
                std::cout << "\033[1;42;30m [ GREEN / RHYTHM ] \033[0m (4 etapas crunch)\n";
            }

            std::cout << " GATE THRESHOLD:  \033[1;33m" << std::fixed << std::setprecision(1) << gateThresh << " dBFS\033[0m\n";
            std::cout << " PRE GAIN:        \033[1;33m" << std::fixed << std::setprecision(1) << preGain << "\033[0m / 10.0\n";
            std::cout << " BASS:   \033[1;33m" << bass << "\033[0m  |  MID: \033[1;33m" << mid << "\033[0m  |  TREBLE: \033[1;33m" << treble << "\033[0m\n";
            std::cout << " POST GAIN (VOL): \033[1;33m" << postGain << "\033[0m / 10.0\n";
            std::cout << " RESONANCE:       \033[1;33m" << resonance << "\033[0m  |  PRESENCE: \033[1;33m" << presence << "\033[0m\n\n";

            // CAB & MIC STATUS
            std::cout << "\033[1;37m--- ESTUDIO DE GABINETE & MICROFONÍA 3D ---\033[0m\n";
            std::cout << " ESTADO:   " << (engine.cabSim.enabled ? "\033[1;32m[ CAB ACTIVO ]\033[0m" : "\033[1;31m[ BYPASS ]\033[0m") << "\n";
            std::cout << " CAB:      \033[1;36m" << (engine.cabSim.customIrPath.empty() ? "Sin IR WAV" : engine.cabSim.customIrPath) << "\033[0m\n";
            const char* micNames[] = {"DYN 57 (Shure SM57)", "DYN 421 (Sennheiser MD421)", "RBN 121 (Royer R-121)"};
            std::cout << " MIC:      \033[1;35m" << micNames[engine.cabSim.currentMic] << "\033[0m\n";
            std::cout << " POSICIÓN: \033[1;33mX = " << std::fixed << std::setprecision(2) << engine.cabSim.posX
                      << "\033[0m (" << (engine.cabSim.posX < 0.2f ? "Centro - Mordida" : (engine.cabSim.posX < 0.5f ? "Cap-Edge - Sweet Spot" : "Cono - Cálido")) << ")\n\n";

            // RECORDING STATUS
            std::cout << "\033[1;37m--- GRABADORA DE AUDIO (32-BIT FLOAT WAV) ---\033[0m\n";
            if (engine.isRecording()) {
                double dur = engine.getRecordingDuration();
                int totalSecs = (int)dur;
                int mins = totalSecs / 60;
                int secs = totalSecs % 60;
                int tenths = (int)((dur - totalSecs) * 10);
                std::cout << " ESTADO:   \033[1;41;37m ● GRABANDO \033[0m \033[1;31m["
                          << std::setfill('0') << std::setw(2) << mins << ":"
                          << std::setfill('0') << std::setw(2) << secs << "."
                          << tenths << "]\033[0m  Archivo: " << engine.recorder.getCurrentRecordingPath() << "\n\n";
            } else {
                std::cout << " ESTADO:   \033[1;30;47m DETENIDO \033[0m";
                std::string lastPath = engine.getLastRecordingPath();
                if (!lastPath.empty()) {
                    std::cout << "  Última toma: \033[1;32m" << lastPath << "\033[0m";
                }
                std::cout << "\n\n";
            }

            std::cout << "\033[1;37m--- ATAJOS DE TECLADO EN VIVO ---\033[0m\n";
            std::cout << " [\033[1;32mC\033[0m] Conmutar Red/Green    [\033[1;32mG\033[0m] Ciclo Umbral Gate (-80dB a -25dB)\n";
            std::cout << " [\033[1;32mp/P\033[0m] Pre Gain (-/+)       [\033[1;32mv/V\033[0m] Post Volume (-/+)\n";
            std::cout << " [\033[1;32mb/B\033[0m] Bass (-/+)           [\033[1;32mm/M\033[0m] Mid (-/+)           [\033[1;32mt/T\033[0m] Treble (-/+)\n";
            std::cout << " [\033[1;32mr/R\033[0m] Resonance (-/+)      [\033[1;32me/E\033[0m] Presence (-/+)\n";
            std::cout << " IR: --ir archivo.wav    [\033[1;35mI\033[0m] Cambiar Mic (57/421/121)   [\033[1;33mx/X\033[0m] Mover Mic X\n";
            std::cout << " [\033[1;34mO\033[0m] Cab On/Bypass         [\033[1;31mESPACIO\033[0m] Grabar ●/■       [\033[1;31mQ\033[0m] Salir\n";
            std::cout.flush();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\033[?25h"; // Restaurar cursor
    std::cout << "\n\033[1;33mDeteniendo motor de audio...\033[0m\n";
    engine.stop();
    std::cout << "\033[1;32mPrograma finalizado con éxito.\033[0m\n";
    return 0;
}
