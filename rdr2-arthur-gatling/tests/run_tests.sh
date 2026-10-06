#!/bin/sh
# Build the headless stand-in game and run ArthurGatling.asi inside it under Wine.
set -e
cd "$(dirname "$0")"
mkdir -p ../build/test
x86_64-w64-mingw32-g++-posix -std=c++17 -O1 -Wall -o ../build/test/fake_game.exe fake_game_sites.S fake_game.cpp -static
cp ../build/ArthurGatling.asi ../build/test/
cd ../build/test
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
WINEDEBUG=-all "$WINE" fake_game.exe ArthurGatling.asi
echo "--- ArthurGatling.log"
cat ArthurGatling.log
