#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 src/aquasense.c -o build/aquasense.exe -lodbc32 -lm -static-libgcc
echo 'Execute: ./build/aquasense.exe --demo ou ./build/aquasense.exe'
