#!/usr/bin/env bash
set -euo pipefail
PREFIX="$(brew --prefix unixodbc)"
mkdir -p build
clang -std=c11 -Wall -Wextra -Wpedantic -O2 src/aquasense.c -I"$PREFIX/include" -L"$PREFIX/lib" -o build/aquasense -lodbc -lm
echo 'Execute: ./build/aquasense --demo ou ./build/aquasense'
