#!/usr/bin/env bash
# Executar: bash examples/connect.sh <DNS-do-servidor> <usuario-SQL>
# As configuracoes existentes no ambiente prevalecem sobre os argumentos.
set -u

AGUA_EXAMPLE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)" || exit 1
AGUA_EXAMPLE_BINARY="$AGUA_EXAMPLE_DIR/../build/monitoramento"
AGUA_SERVER="${AGUA_SERVER:-${1:-}}"
AGUA_USER="${AGUA_USER:-${2:-}}"

if [[ -z "$AGUA_SERVER" || -z "$AGUA_USER" ]]; then
    printf 'Uso: bash examples/connect.sh <DNS-do-servidor> <usuario-SQL>\n' >&2
    exit 2
fi
if [[ ! -x "$AGUA_EXAMPLE_BINARY" ]]; then
    printf 'Cliente ausente: compile primeiro com sh build.sh.\n' >&2
    exit 1
fi

export AGUA_SERVER AGUA_USER
export AGUA_PORT="${AGUA_PORT:-1433}"
export AGUA_DATABASE="${AGUA_DATABASE:-MonitoramentoAgua}"
export AGUA_AUTH=sql
export AGUA_LOGIN_TIMEOUT="${AGUA_LOGIN_TIMEOUT:-5}"
export AGUA_CONNECTION_TIMEOUT="${AGUA_CONNECTION_TIMEOUT:-5}"
export AGUA_QUERY_TIMEOUT="${AGUA_QUERY_TIMEOUT:-10}"

agua_example_cleanup() {
    unset AGUA_PASSWORD
}
trap agua_example_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# read e printf sao builtins; a senha nao integra argumentos de comandos.
if ! IFS= read -r -s -p 'Senha SQL Server: ' AGUA_PASSWORD; then
    printf '\nNao foi possivel ler a senha.\n' >&2
    exit 1
fi
printf '\n'
export AGUA_PASSWORD
"$AGUA_EXAMPLE_BINARY" --check
exit "$?"
