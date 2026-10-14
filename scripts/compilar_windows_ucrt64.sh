#!/usr/bin/env bash
# Compila com o ambiente UCRT64 existente. Nao instala nem altera o PATH.
if [[ ${BASH_SOURCE[0]} != "$0" ]]; then
    printf '%s\n' 'Execute este script com bash; nao use source.' >&2
    return 2
fi
set -euo pipefail

usage() {
    printf '%s\n' 'Uso: bash scripts/compilar_windows_ucrt64.sh [--help]'
    printf '%s\n' 'Execute no terminal MSYS2 UCRT64 ja configurado (GCC x86_64).'
}
if [[ $# -gt 0 ]]; then
    if [[ $# -eq 1 && ( $1 == --help || $1 == -h ) ]]; then
        usage
        exit 0
    fi
    usage >&2
    exit 2
fi
if [[ ${MSYSTEM-} != UCRT64 ]]; then
    printf '%s\n' 'Compilacao nao concluida: abra o terminal MSYS2 UCRT64 existente.' >&2
    exit 1
fi
if ! command -v gcc >/dev/null 2>&1; then
    printf '%s\n' 'Compilacao nao concluida: GCC nao esta acessivel neste terminal.' >&2
    printf '%s\n' 'Revise o diagnostico antes de propor qualquer instalacao.' >&2
    exit 1
fi
if ! target=$(gcc -dumpmachine); then
    printf '%s\n' 'Compilacao nao concluida: nao foi possivel verificar o alvo do GCC.' >&2
    exit 1
fi
if [[ $target != x86_64-w64-mingw32 ]]; then
    printf '%s\n' 'Compilacao nao concluida: GCC deve gerar executaveis Windows x86_64.' >&2
    exit 1
fi
project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$project_dir/build"
cd -- "$project_dir"

# Sem arquivo temporario ou mudanca global: valida C11, UCRT e os headers ODBC.
if ! gcc -std=c11 -Wall -Wextra -Wpedantic -fsyntax-only -x c - <<'C'
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdint.h>
#ifndef _UCRT
#error GCC fora do ambiente UCRT64
#endif
_Static_assert(sizeof(void *) == 8, "ODBC x64 exige ponteiros de 64 bits");
int main(void) { SQLHENV env = SQL_NULL_HENV; return env != SQL_NULL_HENV; }
C
then
    printf '%s\n' 'Compilacao nao concluida: C11/UCRT/headers ODBC nao foram validados.' >&2
    exit 1
fi
if ! odbc_library=$(gcc -print-file-name=libodbc32.a); then
    printf '%s\n' 'Compilacao nao concluida: nao foi possivel verificar a importacao ODBC do GCC.' >&2
    exit 1
fi
if [[ $odbc_library == libodbc32.a || ! -f $odbc_library ]]; then
    printf '%s\n' 'Compilacao nao concluida: biblioteca de importacao Windows ODBC nao localizada.' >&2
    exit 1
fi
if [[ ! -f examples/diagnostico_conexao.c ]]; then
    printf '%s\n' 'Compilacao nao concluida: examples/diagnostico_conexao.c ausente.' >&2
    exit 1
fi
mkdir -p -- "$build_dir"
stage=$(mktemp -d "$build_dir/.ucrt64.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
flags=(-std=c11 -Wall -Wextra -Wpedantic -O2 -static-libgcc -D_CRT_SECURE_NO_WARNINGS -I.)
sources=(monitoramento.c config.c input.c db_odbc.c odbc_diag.c reports_odbc.c report_input.c csv_report.c)
gcc "${flags[@]}" "${sources[@]}" -o "$stage/monitoramento.exe" -lodbc32
gcc "${flags[@]}" examples/diagnostico_conexao.c db_odbc.c config.c odbc_diag.c input.c \
    -o "$stage/diagnostico_conexao.exe" -lodbc32
cp -- "$stage/monitoramento.exe" "$stage/aquasense.exe"

# As DLLs do Driver 18 permanecem na instalacao Microsoft. Nao redistribui o driver.
# Examina imports para avisar sobre runtimes GCC sem tocar em configuracoes do SO.
if command -v objdump >/dev/null 2>&1; then
    compiler_dir=$(dirname -- "$(command -v gcc)")
    for binary in "$stage/aquasense.exe" "$stage/diagnostico_conexao.exe"; do
        imports=$(objdump -p "$binary")
        for runtime in libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll; do
            if [[ $imports == *"DLL Name: $runtime"* ]]; then
                runtime_file="$compiler_dir/$runtime"
                if [[ ! -f $runtime_file ]]; then
                    runtime_file=$(gcc -print-file-name="$runtime")
                fi
                if [[ $runtime_file == "$runtime" || ! -f $runtime_file ]]; then
                    printf 'Compilacao nao concluida: runtime %s nao localizado no GCC existente.\n' "$runtime" >&2
                    exit 1
                fi
                cp -- "$runtime_file" "$stage/$runtime"
            fi
        done
    done
else
    printf '%s\n' 'AVISO: objdump ausente; dependencias DLL exigem verificacao antes de distribuir.' >&2
fi
for artifact in "$stage"/*; do
    mv -f -- "$artifact" "$build_dir/"
done
printf '%s\n' 'Compilacao concluida: build/monitoramento.exe, build/aquasense.exe e build/diagnostico_conexao.exe.'
printf '%s\n' 'Execute pelo CMD; Driver 18 x64 continua obrigatorio. O teste SQL e separado e explicito.'
