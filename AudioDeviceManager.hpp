#pragma once
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string>
#include <vector>
#include <iostream>

namespace Amp5650 {

struct AudioDeviceInfo {
    AudioDeviceID id = 0;
    std::string name;
    std::string uid;
    int inputChannels = 0;
    int outputChannels = 0;
    double sampleRate = 48000.0;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
};

class AudioDeviceManager {
public:
    static std::string getDeviceName(AudioDeviceID devId) {
        AudioObjectPropertyAddress addr = {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        CFStringRef nameRef = nullptr;
        UInt32 size = sizeof(nameRef);
        OSStatus err = AudioObjectGetPropertyData(devId, &addr, 0, nullptr, &size, &nameRef);
        if (err == noErr && nameRef) {
            char buf[256];
            CFStringGetCString(nameRef, buf, sizeof(buf), kCFStringEncodingUTF8);
            CFRelease(nameRef);
            return std::string(buf);
        }
        return "Unknown Device";
    }

    static std::string getDeviceUID(AudioDeviceID devId) {
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        CFStringRef uidRef = nullptr;
        UInt32 size = sizeof(uidRef);
        OSStatus err = AudioObjectGetPropertyData(devId, &addr, 0, nullptr, &size, &uidRef);
        if (err == noErr && uidRef) {
            char buf[256];
            CFStringGetCString(uidRef, buf, sizeof(buf), kCFStringEncodingUTF8);
            CFRelease(uidRef);
            return std::string(buf);
        }
        return "";
    }

    static int getChannelCount(AudioDeviceID devId, AudioObjectPropertyScope scope) {
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyStreamConfiguration,
            scope,
            kAudioObjectPropertyElementMain
        };
        UInt32 size = 0;
        OSStatus err = AudioObjectGetPropertyDataSize(devId, &addr, 0, nullptr, &size);
        if (err != noErr || size == 0) return 0;

        std::vector<uint8_t> buf(size);
        AudioBufferList* abl = reinterpret_cast<AudioBufferList*>(buf.data());
        err = AudioObjectGetPropertyData(devId, &addr, 0, nullptr, &size, abl);
        if (err != noErr) return 0;

        int channels = 0;
        for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) {
            channels += abl->mBuffers[i].mNumberChannels;
        }
        return channels;
    }

    static double getSampleRate(AudioDeviceID devId) {
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        Float64 sr = 48000.0;
        UInt32 size = sizeof(sr);
        AudioObjectGetPropertyData(devId, &addr, 0, nullptr, &size, &sr);
        return sr;
    }

    static AudioDeviceID getDefaultInputDeviceID() {
        AudioObjectPropertyAddress addr = {
            kAudioHardwarePropertyDefaultInputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioDeviceID devId = 0;
        UInt32 size = sizeof(devId);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &devId);
        return devId;
    }

    static AudioDeviceID getDefaultOutputDeviceID() {
        AudioObjectPropertyAddress addr = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioDeviceID devId = 0;
        UInt32 size = sizeof(devId);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &devId);
        return devId;
    }

    static std::vector<AudioDeviceInfo> enumerateDevices() {
        std::vector<AudioDeviceInfo> list;
        AudioObjectPropertyAddress addr = {
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        UInt32 size = 0;
        OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
        if (err != noErr || size == 0) return list;

        size_t count = size / sizeof(AudioDeviceID);
        std::vector<AudioDeviceID> ids(count);
        err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data());
        if (err != noErr) return list;

        AudioDeviceID defIn = getDefaultInputDeviceID();
        AudioDeviceID defOut = getDefaultOutputDeviceID();

        for (AudioDeviceID id : ids) {
            AudioDeviceInfo info;
            info.id = id;
            info.name = getDeviceName(id);
            info.uid = getDeviceUID(id);
            info.inputChannels = getChannelCount(id, kAudioDevicePropertyScopeInput);
            info.outputChannels = getChannelCount(id, kAudioDevicePropertyScopeOutput);
            info.sampleRate = getSampleRate(id);
            info.isDefaultInput = (id == defIn);
            info.isDefaultOutput = (id == defOut);
            list.push_back(info);
        }
        return list;
    }
};

} // namespace Amp5650
