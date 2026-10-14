#!/bin/sh
# Build the local C11 API with existing libraries; no global changes/install.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$task_root"
task_cc=${CC:-cc}
task_odbc_prefix=${ODBC_PREFIX:-/opt/homebrew/opt/unixodbc}
task_json_prefix=${JSON_C_PREFIX:-/opt/homebrew/opt/json-c}
task_crypto_prefix=${OPENSSL_PREFIX:-/opt/homebrew/opt/openssl@3}
for task_header in "$task_odbc_prefix/include/sql.h" "$task_json_prefix/include/json-c/json.h" "$task_crypto_prefix/include/openssl/evp.h"; do
    if [ ! -r "$task_header" ]; then
        printf '%s\n' 'Cabeçalhos ausentes. Informe ODBC_PREFIX, JSON_C_PREFIX e OPENSSL_PREFIX das bibliotecas existentes.' >&2
        exit 1
    fi
done
mkdir -p build
task_output=$(mktemp build/.aquasense-api.XXXXXX)
trap 'rm -f -- "$task_output"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
"$task_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -I. -Ibackend -I"$task_odbc_prefix/include" -I"$task_json_prefix/include" -I"$task_crypto_prefix/include" \
    -L"$task_odbc_prefix/lib" -L"$task_json_prefix/lib" -L"$task_crypto_prefix/lib" \
    backend/web_server.c backend/web_auth.c backend/web_support.c backend/web_domain.c backend/consumption.c \
    config.c input.c db_odbc.c odbc_diag.c reports_odbc.c report_input.c \
    -lodbc -ljson-c -lcrypto -lm -o "$task_output"
mv -f -- "$task_output" build/aquasense_api
printf '%s\n' 'API C11 compilada: build/aquasense_api'
