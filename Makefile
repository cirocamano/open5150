CXX = clang++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
LDFLAGS = -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework Accelerate
HEADERS = $(wildcard *.hpp dsp/*.hpp)
UI = $(wildcard ui/*)
APP = build/Open5150.app

.PHONY: all clean test plugins
all: build/open5150_live $(APP)/Contents/MacOS/Open5150 $(APP)/Contents/Resources/ui/index.html

build:
	mkdir -p build

build/open5150_live: main.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -pedantic main.cpp -o $@ $(LDFLAGS)

$(APP)/Contents/MacOS/Open5150: main_gui.mm Info.plist $(HEADERS)
	mkdir -p $(APP)/Contents/MacOS
	$(CXX) $(CXXFLAGS) main_gui.mm -o $@ $(LDFLAGS) -framework Cocoa -framework WebKit
	cp Info.plist $(APP)/Contents/Info.plist

$(APP)/Contents/Resources/ui/index.html: $(UI)
	mkdir -p $(APP)/Contents/Resources/ui
	cp $(UI) $(APP)/Contents/Resources/ui/
	touch $@

build/test_audio: test_audio.cpp tests/fixtures/DspRegression.hpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -UNDEBUG test_audio.cpp -o $@ $(LDFLAGS)

test: build/test_audio build/open5150_live
	./build/test_audio
	./build/open5150_live --help >/dev/null
	! ./build/open5150_live --buffer 64oops >/dev/null 2>&1
	! ./build/open5150_live --buffer 4294967360 >/dev/null 2>&1
	! ./build/open5150_live --ir >/dev/null 2>&1

plugins:
	cmake -S . -B build/plugins -DCMAKE_BUILD_TYPE=Release
	cmake --build build/plugins --parallel

clean:
	rm -rf build
