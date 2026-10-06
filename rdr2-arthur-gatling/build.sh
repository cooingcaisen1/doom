#!/bin/sh
# Build ArthurGatling.asi (Windows x64) with MinGW-w64. Preflight + codegen run first.
set -e
cd "$(dirname "$0")"
python3 -I tools/gen.py "$@"
mkdir -p build
MH=third_party/minhook
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-unused-parameter \
  -I"$MH/include" -c src/runtime.cpp -o build/runtime.o
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -c src/gatling.cpp -o build/gatling.o
for f in buffer hook trampoline hde/hde64; do
  x86_64-w64-mingw32-gcc -O2 -I"$MH/include" -c "$MH/src/$f.c" -o "build/mh_$(basename $f).o"
done
x86_64-w64-mingw32-g++-posix -shared -o build/ArthurGatling.asi build/*.o \
  -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows -s
echo "built build/ArthurGatling.asi ($(stat -c %s build/ArthurGatling.asi) bytes)"
