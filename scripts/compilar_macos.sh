#!/bin/bash
# Reuses the current macOS toolchain. Never installs or changes global settings.
if [ "${BASH_SOURCE[0]}" != "$0" ]; then
    printf '%s\n' 'Execute com bash; não use source para compilar.' >&2
    return 2
fi
set +x
set -eu

if [ "$#" -gt 0 ]; then
    if [ "$#" -eq 1 ] && [ "$1" = '--help' ]; then
        printf '%s\n' 'Uso: bash scripts/compilar_macos.sh' \
            'Compila com Clang/C11 e unixODBC existentes. CC e ODBC_PREFIX podem selecionar instalações existentes.'
        exit 0
    fi
    printf '%s\n' 'Argumento inválido. Use --help.' >&2
    exit 2
fi

task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" != Darwin ]; then
    printf '%s\n' 'Com erro: este script é destinado ao macOS. Use o script correspondente ao seu sistema.' >&2
    exit 1
fi
task_cc=${CC:-clang}
if ! command -v "$task_cc" >/dev/null 2>&1; then
    printf '%s\n' 'Pendente: compilador C não acessível no Terminal. Verifique clang --version e xcode-select -p.' >&2
    exit 1
fi
task_odbc_prefix=${ODBC_PREFIX:-}
if [ -z "$task_odbc_prefix" ] && command -v odbc_config >/dev/null 2>&1; then
    task_odbc_prefix=$(odbc_config --prefix 2>/dev/null || true)
fi
if [ -z "$task_odbc_prefix" ] && command -v brew >/dev/null 2>&1; then
    task_odbc_prefix=$(brew --prefix unixodbc 2>/dev/null || true)
fi
if [ -z "$task_odbc_prefix" ] || [ ! -r "$task_odbc_prefix/include/sql.h" ] || [ ! -r "$task_odbc_prefix/include/sqlext.h" ]; then
    printf '%s\n' 'Com erro: cabeçalhos unixODBC não encontrados. Execute bash scripts/verificar_ambiente_macos.sh.' >&2
    exit 1
fi
if [ ! -r "$task_odbc_prefix/lib/libodbc.dylib" ] && [ ! -r "$task_odbc_prefix/lib/libodbc.a" ]; then
    printf '%s\n' 'Com erro: biblioteca libodbc não encontrada no prefixo selecionado.' >&2
    exit 1
fi
if ! printf '%s\n' '#include <sql.h>' '#include <sqlext.h>' \
        '#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L' '#error C11 necessário' '#endif' \
        'int main(void) { (void)SQLAllocHandle; return 0; }' | \
        "$task_cc" -std=c11 -I"$task_odbc_prefix/include" -x c -fsyntax-only - >/dev/null 2>&1; then
    printf '%s\n' 'Com erro: compilador/C11 ou cabeçalhos ODBC incompatíveis. Nenhuma instalação foi alterada.' >&2
    exit 1
fi

printf '%s\n' 'AquaSense: compilação com as ferramentas existentes; nenhuma instalação ou configuração global será alterada.'
CC="$task_cc" ODBC_PREFIX="$task_odbc_prefix" sh "$task_root/build.sh"
task_alias_temp=$(mktemp "$task_root/build/.aquasense.XXXXXX")
trap 'rm -f -- "$task_alias_temp"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cp "$task_root/build/monitoramento" "$task_alias_temp"
mv -f -- "$task_alias_temp" "$task_root/build/aquasense"
if [ -f "$task_root/scripts/compilar_diagnostico_macos.sh" ]; then
    CC="$task_cc" ODBC_PREFIX="$task_odbc_prefix" bash "$task_root/scripts/compilar_diagnostico_macos.sh"
fi
printf '%s\n' 'Compilação concluída: build/monitoramento e build/aquasense.' \
    'A compilação não comprova conexão SQL Server. Para validá-la: bash scripts/testar_conexao.sh.'
