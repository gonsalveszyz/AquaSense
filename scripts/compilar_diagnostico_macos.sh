#!/usr/bin/env bash
# Compila somente arquivos do projeto; nenhuma ferramenta e instalada.
if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
    printf '%s\n' 'Execute este script com bash; nao use source.' >&2
    return 2
fi
set +x
set -eu
[[ $# -le 1 ]] || { printf '%s\n' 'Argumentos excessivos.' >&2; exit 2; }
agua_probe_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
case "${1:-}" in
    --help) printf '%s\n' 'Uso: bash scripts/compilar_diagnostico_macos.sh'; exit 0 ;;
    '') ;;
    *) printf '%s\n' 'Argumento invalido. Use --help.' >&2; exit 2 ;;
esac
agua_probe_cc=${CC:-clang}
command -v "$agua_probe_cc" >/dev/null 2>&1 || { printf '%s\n' 'Compilador indisponivel; execute o diagnostico do ambiente.' >&2; exit 1; }
agua_probe_odbc=${ODBC_PREFIX:-}
if [[ -z "$agua_probe_odbc" ]] && command -v odbc_config >/dev/null 2>&1; then
    agua_probe_odbc=$(odbc_config --prefix 2>/dev/null || true)
fi
if [[ -z "$agua_probe_odbc" ]] && command -v brew >/dev/null 2>&1; then
    agua_probe_odbc=$(brew --prefix unixodbc 2>/dev/null || true)
fi
[[ -n "$agua_probe_odbc" && -r "$agua_probe_odbc/include/sql.h" && -r "$agua_probe_odbc/include/sqlext.h" ]] || {
    printf '%s\n' 'Cabeçalhos ODBC nao encontrados. Verifique ODBC_PREFIX; nenhuma instalacao foi executada.' >&2; exit 1;
}
mkdir -p "$agua_probe_root/build"
agua_probe_temp=$(mktemp "$agua_probe_root/build/.diagnostico_conexao.XXXXXX")
trap 'rm -f -- "$agua_probe_temp"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
"$agua_probe_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -I"$agua_probe_root" -I"$agua_probe_odbc/include" -L"$agua_probe_odbc/lib" \
    "$agua_probe_root/examples/diagnostico_conexao.c" "$agua_probe_root/db_odbc.c" \
    "$agua_probe_root/config.c" "$agua_probe_root/input.c" "$agua_probe_root/odbc_diag.c" \
    -lodbc -o "$agua_probe_temp"
mv -f -- "$agua_probe_temp" "$agua_probe_root/build/diagnostico_conexao"
printf '%s\n' 'Diagnostico compilado: build/diagnostico_conexao'
