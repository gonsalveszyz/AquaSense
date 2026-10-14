#!/usr/bin/env bash
# Credenciais somente no ambiente deste processo/filho; nunca em argv ou arquivo.
if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
    printf '%s\n' 'Execute este script com bash; nao use source.' >&2
    return 2
fi
set +x
set -u
[[ $# -le 1 ]] || { printf '%s\n' 'Argumentos excessivos.' >&2; exit 2; }
agua_connection_no_prompt=0
case "${1:-}" in
    --help) printf '%s\n' 'Uso: bash scripts/testar_conexao.sh [--no-prompt]' 'Configure AGUA_SERVER (DNS/IPv4), AGUA_PORT, AGUA_DATABASE, AGUA_AUTH e AGUA_USER.' 'AGUA_PASSWORD e lida sem eco se necessaria; --no-prompt exige senha no ambiente.'; exit 0 ;;
    --no-prompt) agua_connection_no_prompt=1 ;;
    '') ;;
    *) printf '%s\n' 'Argumento invalido. Use --help.' >&2; exit 2 ;;
esac
agua_connection_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)" || exit 1
agua_connection_binary="$agua_connection_root/build/diagnostico_conexao"
agua_connection_server=${AGUA_SERVER:-}
case "$agua_connection_server" in
    ''|*[!A-Za-z0-9.-]*) printf '%s\n' 'AGUA_SERVER pendente/invalido: informe somente DNS/IPv4; AGUA_PORT separada.' >&2; exit 2 ;;
esac
[[ ${#agua_connection_server} -le 253 ]] || { printf '%s\n' 'AGUA_SERVER excede o limite.' >&2; exit 2; }
agua_connection_port=${AGUA_PORT:-1433}
case "$agua_connection_port" in
    ''|*[!0-9]*) printf '%s\n' 'AGUA_PORT invalida.' >&2; exit 2 ;;
esac
[[ ${#agua_connection_port} -le 5 ]] || { printf '%s\n' 'AGUA_PORT invalida.' >&2; exit 2; }
[[ $((10#$agua_connection_port)) -ge 1 && $((10#$agua_connection_port)) -le 65535 ]] || {
    printf '%s\n' 'AGUA_PORT fora de 1 a 65535.' >&2; exit 2;
}
agua_connection_database=${AGUA_DATABASE:-MonitoramentoAgua}
case "$agua_connection_database" in
    ''|*[!A-Za-z0-9_]*) printf '%s\n' 'AGUA_DATABASE invalido.' >&2; exit 2 ;;
esac
[[ ${#agua_connection_database} -le 128 ]] || { printf '%s\n' 'AGUA_DATABASE excede o limite.' >&2; exit 2; }
agua_connection_auth=${AGUA_AUTH:-sql}
case "$agua_connection_auth" in
    sql) [[ -n "${AGUA_USER:-}" ]] || { printf '%s\n' 'AGUA_USER pendente; usuario SQL necessario.' >&2; exit 2; } ;;
    integrated) ;;
    *) printf '%s\n' 'AGUA_AUTH invalida: use sql ou integrated.' >&2; exit 2 ;;
esac
[[ -x "$agua_connection_binary" ]] || {
    printf '%s\n' 'Diagnostico C ausente; compile com bash scripts/compilar_macos.sh ou scripts/compilar_diagnostico_macos.sh.' >&2; exit 1;
}
agua_connection_cleanup() { unset AGUA_PASSWORD; }
trap agua_connection_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [[ "$agua_connection_auth" == sql && -z "${AGUA_PASSWORD:-}" ]]; then
    if [[ $agua_connection_no_prompt -eq 1 || ! -t 0 ]]; then
        printf '%s\n' 'AGUA_PASSWORD pendente. Sem terminal interativo, use configuracao segura no ambiente; nao forneca senha em argumentos.' >&2
        exit 2
    fi
    if ! IFS= read -r -s -p 'Senha SQL Server: ' AGUA_PASSWORD; then
        printf '\n%s\n' 'Senha nao lida; conexao nao iniciada.' >&2; exit 2
    fi
    printf '\n'
    [[ -n "$AGUA_PASSWORD" ]] || { printf '%s\n' 'Senha vazia; conexao nao iniciada.' >&2; exit 2; }
fi
export AGUA_SERVER="$agua_connection_server" AGUA_PORT="$agua_connection_port" AGUA_DATABASE="$agua_connection_database" AGUA_AUTH="$agua_connection_auth"
if [[ "$agua_connection_auth" == sql ]]; then export AGUA_PASSWORD; else unset AGUA_PASSWORD; fi
printf '%s\n' 'Tentativa explicita de conexao ODBC; dados nao serao modificados.'
"$agua_connection_binary"
exit "$?"
