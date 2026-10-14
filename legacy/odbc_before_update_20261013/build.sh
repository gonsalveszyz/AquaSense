#!/bin/sh
# C11 client: links the ODBC driver manager. Driver 18 is a runtime dependency.
set -eu
cd "$(dirname "$0")"
mkdir -p build
task_cc=${CC:-cc}
task_odbc_prefix=${ODBC_PREFIX:-}
if [ -z "$task_odbc_prefix" ] && command -v brew >/dev/null 2>&1; then
    task_odbc_prefix=$(brew --prefix unixodbc 2>/dev/null || true)
fi
if [ -n "$task_odbc_prefix" ]; then
    "$task_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -I"$task_odbc_prefix/include" -L"$task_odbc_prefix/lib" \
        monitoramento.c config.c input.c db_odbc.c -lodbc \
        -o build/monitoramento
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists odbc; then
    # pkg-config emits compiler/linker arguments; intentional word splitting.
    # shellcheck disable=SC2046
    "$task_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        $(pkg-config --cflags odbc) monitoramento.c config.c input.c db_odbc.c \
        $(pkg-config --libs odbc) -o build/monitoramento
else
    "$task_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        monitoramento.c config.c input.c db_odbc.c -lodbc \
        -o build/monitoramento
fi
printf '%s\n' 'Cliente compilado: build/monitoramento'
