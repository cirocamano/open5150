#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <iostream>
#include <string>
#include <sstream>
#include "AudioEngineHAL.hpp"
#include "AudioDeviceManager.hpp"

using namespace Amp5650;

@interface AppDelegate : NSObject <NSApplicationDelegate, WKScriptMessageHandler, NSWindowDelegate>
@property (strong, nonatomic) NSWindow *window;
@property (strong, nonatomic) WKWebView *webView;
@property (strong, nonatomic) NSTimer *meterTimer;
@end

static LiveAudioEngine *gEngine = nullptr;

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    gEngine = new LiveAudioEngine();
    gEngine->bufferSize = 64; // Default to 64 samples (~1.33 ms)
    gEngine->start();

    // 1. Create Native macOS Window
    NSRect frame = NSMakeRect(100, 100, 960, 560);
    NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:styleMask
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"OPEN5150 - High Gain Tube Amplifier & 3D Cab Studio";
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [self.window setMinSize:NSMakeSize(820, 490)];
    self.window.delegate = self;
    [self.window center];

    // 2. Setup WKWebView with Script Message Handler
    WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
    WKUserContentController *userContent = [[WKUserContentController alloc] init];
    [userContent addScriptMessageHandler:self name:@"audioEngine"];
    config.userContentController = userContent;

    self.webView = [[WKWebView alloc] initWithFrame:self.window.contentView.bounds configuration:config];
    self.webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.window.contentView addSubview:self.webView];

    // 3. Load HTML UI
    NSString *htmlPath = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"ui/index.html"];

    NSURL *fileURL = [NSURL fileURLWithPath:htmlPath];
    NSURL *dirURL = [fileURL URLByDeletingLastPathComponent];
    [self.webView loadFileURL:fileURL allowingReadAccessToURL:dirURL];

    // 4. Timer for 60 FPS live meters
    self.meterTimer = [NSTimer scheduledTimerWithTimeInterval:0.033 // ~30-60 Hz
                                                       target:self
                                                     selector:@selector(sendMeterUpdates)
                                                     userInfo:nil
                                                      repeats:YES];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)sendDevicePopulation {
    auto devices = AudioDeviceManager::enumerateDevices();
    std::stringstream jsonIn, jsonOut;
    jsonIn << "[";
    jsonOut << "[";
    bool firstIn = true, firstOut = true;

    for (const auto& d : devices) {
        if (d.inputChannels > 0) {
            if (!firstIn) jsonIn << ",";
            jsonIn << "{\"id\":" << d.id << ",\"name\":\"" << d.name << "\",\"inCh\":" << d.inputChannels << "}";
            firstIn = false;
        }
        if (d.outputChannels > 0) {
            if (!firstOut) jsonOut << ",";
            jsonOut << "{\"id\":" << d.id << ",\"name\":\"" << d.name << "\",\"outCh\":" << d.outputChannels << "}";
            firstOut = false;
        }
    }
    jsonIn << "]";
    jsonOut << "]";

    NSString *js = [NSString stringWithFormat:@"populateDevices(%s, %s, %u, %u, %u, %f);",
                    jsonIn.str().c_str(),
                    jsonOut.str().c_str(),
                    gEngine->inputDeviceID,
                    gEngine->outputDeviceID,
                    gEngine->bufferSize,
                    gEngine->sampleRate];

    [self.webView evaluateJavaScript:js completionHandler:nil];
}

- (void)sendAmpParameters {
    const auto state = gEngine->getParameters();
    const auto& c = state.channels[state.channel];
    NSDictionary *values = @{@"preGain": @(c.pre), @"postGain": @(c.post),
        @"bass": @(c.bass), @"mid": @(c.mid), @"treble": @(c.treble),
        @"presence": @(c.presence), @"resonance": @(c.resonance)};
    NSData *json = [NSJSONSerialization dataWithJSONObject:values options:0 error:nil];
    NSString *payload = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    [self.webView evaluateJavaScript:[NSString stringWithFormat:@"restoreAmpParameters(%@);", payload]
                  completionHandler:nil];
}

- (void)showCabError {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"No se pudo cargar la cabina";
    alert.informativeText = @"La IR no es válida o no se puede leer. Se conserva la cabina anterior.";
    [alert runModal];
}

- (void)sendCabState {
    const auto state = gEngine->getParameters();
    NSDictionary *values = @{@"cabEnabled": @(state.cabEnabled),
        @"customPath": [NSString stringWithUTF8String:gEngine->cabSim.customIrPath.c_str()]};
    NSData *json = [NSJSONSerialization dataWithJSONObject:values options:0 error:nil];
    NSString *payload = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    [self.webView evaluateJavaScript:[NSString stringWithFormat:@"restoreCabState(%@);", payload]
                  completionHandler:nil];
}

- (void)sendMeterUpdates {
    if (!gEngine || !gEngine->isRunning) return;

    float pIn = gEngine->meters.peakIn.load(std::memory_order_relaxed);
    float rIn = gEngine->meters.rmsIn.load(std::memory_order_relaxed);
    bool gOpen = gEngine->meters.gateOpen.load(std::memory_order_relaxed);
    float gRed = gEngine->meters.gateReductionDb.load(std::memory_order_relaxed);
    float pOut = gEngine->meters.peakOut.load(std::memory_order_relaxed);
    float rOut = gEngine->meters.rmsOut.load(std::memory_order_relaxed);
    bool isRec = gEngine->isRecording();
    double recSecs = gEngine->getRecordingDuration();

    NSString *js = [NSString stringWithFormat:@"updateMeters(%f, %f, %s, %f, %f, %f, %s, %f);",
                    pIn, rIn, gOpen ? "true" : "false", gRed, pOut, rOut,
                    isRec ? "true" : "false", recSecs];

    [self.webView evaluateJavaScript:js completionHandler:nil];
}

// Handle messages sent from JavaScript UI
- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.body isKindOfClass:[NSDictionary class]]) return;
    NSDictionary *dict = message.body;
    NSString *action = dict[@"action"];

    if ([action isEqualToString:@"uiReady"]) {
        [self sendDevicePopulation];
        [self sendAmpParameters];
        [self sendCabState];
    } else if ([action isEqualToString:@"setParam"]) {
        NSString *pid = dict[@"id"];
        float val = [dict[@"value"] floatValue];

        if ([pid isEqualToString:@"preGain"]) gEngine->setPreGain(val);
        else if ([pid isEqualToString:@"postGain"]) gEngine->setPostGain(val);
        else if ([pid isEqualToString:@"gateThreshold"]) gEngine->setNoiseGateThreshold(val);
        else if ([pid isEqualToString:@"bass"]) gEngine->setBass(val);
        else if ([pid isEqualToString:@"mid"]) gEngine->setMid(val);
        else if ([pid isEqualToString:@"treble"]) gEngine->setTreble(val);
        else if ([pid isEqualToString:@"resonance"]) gEngine->setResonance(val);
        else if ([pid isEqualToString:@"presence"]) gEngine->setPresence(val);
    } else if ([action isEqualToString:@"setMode"]) {
        gEngine->setMode([dict[@"channel"] intValue], [dict[@"crunch"] intValue]);
        [self sendAmpParameters];
    } else if ([action isEqualToString:@"setChannel"]) {
        int ch = [dict[@"channel"] intValue];
        gEngine->setChannel(ch);
        [self sendAmpParameters];
    } else if ([action isEqualToString:@"setCrunch"]) {
        int cr = [dict[@"value"] intValue];
        gEngine->setCrunch(cr);
    } else if ([action isEqualToString:@"setBright"]) {
        int br = [dict[@"value"] intValue];
        gEngine->setBright(br);
    } else if ([action isEqualToString:@"changeDevice"]) {
        NSString *type = dict[@"type"];
        AudioDeviceID did = [dict[@"id"] unsignedIntValue];
        gEngine->stop();
        if ([type isEqualToString:@"input"]) gEngine->inputDeviceID = did;
        else if ([type isEqualToString:@"output"]) gEngine->outputDeviceID = did;
        gEngine->start();
        [self sendDevicePopulation];
    } else if ([action isEqualToString:@"changeBuffer"]) {
        UInt32 buf = [dict[@"bufferSize"] unsignedIntValue];
        gEngine->stop();
        gEngine->bufferSize = buf;
        gEngine->start();
        [self sendDevicePopulation];
    } else if ([action isEqualToString:@"setMic"]) {
        int micId = [dict[@"micId"] intValue];
        gEngine->setMicrophone(micId);
    } else if ([action isEqualToString:@"setMicPosition"]) {
        float x = [dict[@"x"] floatValue];
        float z = [dict[@"z"] floatValue];
        BOOL is45 = [dict[@"is45"] boolValue];
        gEngine->setMicPosition(x, z, is45);
    } else if ([action isEqualToString:@"setCabFilters"]) {
        float lowCut = [dict[@"lowCut"] floatValue];
        float highCut = [dict[@"highCut"] floatValue];
        gEngine->setCabFilters(lowCut, highCut);
    } else if ([action isEqualToString:@"setCabEnabled"]) {
        BOOL en = [dict[@"enabled"] boolValue];
        gEngine->setCabEnabled(en);
        [self sendCabState];
    } else if ([action isEqualToString:@"browseCustomIr"]) {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        panel.allowedFileTypes = @[@"wav", @"WAV"];
#pragma clang diagnostic pop
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            NSString *filePath = [url path];
            if (!gEngine->loadCabinetIR(std::string([filePath UTF8String]))) {
                [self showCabError];
                [self sendCabState];
                return;
            }
            NSString *fileName = [filePath lastPathComponent];
            NSData *json = [NSJSONSerialization dataWithJSONObject:@[fileName, filePath] options:0 error:nil];
            NSString *payload = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
            NSString *js = [NSString stringWithFormat:@"onCustomIrLoaded(...%@);", payload];
            [self.webView evaluateJavaScript:js completionHandler:nil];
        }
        [self sendCabState];
    } else if ([action isEqualToString:@"startRecording"]) {
        gEngine->startRecording();
    } else if ([action isEqualToString:@"stopRecording"]) {
        gEngine->stopRecording();
        std::string lastPath = gEngine->getLastRecordingPath();
        NSString *js = [NSString stringWithFormat:@"onRecordingFinished('%@');", [NSString stringWithUTF8String:lastPath.c_str()]];
        [self.webView evaluateJavaScript:js completionHandler:nil];
    } else if ([action isEqualToString:@"revealInFinder"]) {
        NSString *path = dict[@"path"];
        if (path && [path length] > 0 && [[NSFileManager defaultManager] fileExistsAtPath:path]) {
            [[NSWorkspace sharedWorkspace] selectFile:path inFileViewerRootedAtPath:@""];
        } else {
            char cwd[1024];
            getcwd(cwd, sizeof(cwd));
            NSString *recDir = [NSString stringWithFormat:@"%s/recordings", cwd];
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:recDir]];
        }
    }
}

- (void)windowWillClose:(NSNotification *)notification {
    [self.meterTimer invalidate];
    if (gEngine) {
        gEngine->stop();
        delete gEngine;
        gEngine = nullptr;
    }
    [NSApp terminate:nil];
}

@end

int main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;

        // Standard macOS App Menu
        NSMenu *menubar = [[NSMenu alloc] init];
        NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
        [menubar addItem:appMenuItem];
        [app setMainMenu:menubar];

        NSMenu *appMenu = [[NSMenu alloc] init];
        NSString *appName = @"OPEN5150";
        NSString *quitTitle = [@"Quit " stringByAppendingString:appName];
        NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
        [appMenu addItem:quitMenuItem];
        [appMenuItem setSubmenu:appMenu];

        [app run];
    }
    return 0;
}
