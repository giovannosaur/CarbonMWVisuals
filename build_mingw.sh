#!/bin/sh
# Alternative build without Visual Studio (Linux/MSYS2): requires i686-w64-mingw32-g++
set -e
mkdir -p bin/mingw
i686-w64-mingw32-g++ -std=c++17 -shared -O2 -Wall -o bin/mingw/CarbonMWVisuals.asi src/*.cpp src/imgui/*.cpp \
    -static-libgcc -static-libstdc++ -lgdi32 -ldwmapi -s -Wl,--exclude-all-symbols
echo "OK: bin/mingw/CarbonMWVisuals.asi"
