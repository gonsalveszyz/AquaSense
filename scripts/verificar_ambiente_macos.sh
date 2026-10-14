#!/bin/bash
# Read-only diagnosis. Network testing requires the explicit --test-connection option.
if [ "${BASH_SOURCE[0]}" != "$0" ]; then
    printf '%s\n' 'Execute com bash; não use source para este diagnóstico.' >&2
    return 2
fi
set +x
set -u

task_test_connection=0
case "$#" in
    0) ;;
    1) case "$1" in
        --test-connection) task_test_connection=1 ;;
        --help) printf '%s\n' 'Uso: bash scripts/verificar_ambiente_macos.sh [--test-connection]' \
            'Sem opção: somente leitura local, sem conexão de rede nem instalação.' \
            '--test-connection: executa scripts/testar_conexao.sh com a configuração AGUA_*.'; exit 0 ;;
        *) printf '%s\n' 'Argumento inválido. Use --help.' >&2; exit 2 ;;
        esac ;;
    *) printf '%s\n' 'Argumentos inválidos. Use --help.' >&2; exit 2 ;;
esac

task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_local_error=0
task_config_error=0
task_config_pending=0
task_installed=()
task_missing=()
task_adjustments=()
task_optional=()
task_pending=()
task_authorization=()

task_record() { printf '[%s] %s — %s\n' "$1" "$2" "$3"; }
task_absent() {
    task_record 'Pendente' "$1" "$2"
    task_missing+=("$1: $2")
    task_local_error=1
}
task_broken() {
    task_record 'Com erro' "$1" "$2"
    task_adjustments+=("$1: $2")
    task_local_error=1
}
task_present() {
    task_record 'Configurada' "$1" "$2"
    task_installed+=("$1: $2")
}
task_show_category() {
    task_title=$1; shift
    printf '\n%s\n' "$task_title"
    if [ "$#" -eq 0 ]; then printf '%s\n' '  Nenhuma identificada.'
    else for task_item in "$@"; do printf '  - %s\n' "$task_item"; done; fi
}
task_uint() {
    # Numeric validation matches config.c; bounded length before arithmetic prevents overflow.
    task_num=$1; task_max=$2; task_length=$3
    case "$task_num" in ''|*[!0-9]*) return 1 ;; esac
    [ "${#task_num}" -le "$task_length" ] || return 1
    task_decimal=$((10#$task_num))
    [ "$task_decimal" -ge 1 ] && [ "$task_decimal" -le "$task_max" ]
}
task_credentials_within_limits() {
    local LC_ALL=C
    [ "${#task_user}" -le 256 ] && [ "${#task_password}" -le 1024 ]
}
task_optional_executable() {
    if command -v "$1" >/dev/null 2>&1; then
        task_record 'Configurada' "$2" 'executável acessível; ferramenta opcional'
        task_optional+=("$2: acessível, opcional.")
    else
        task_record 'Não aplicável' "$2" 'não acessível; dispensável para o cliente C'
        task_optional+=("$2: não acessível, dispensável.")
    fi
}

printf '%s\n' 'AQUASENSE — DIAGNÓSTICO DO AMBIENTE' \
    'Verificações locais de leitura. Nenhuma instalação, atualização, alteração de PATH ou conexão de rede automática.'
task_os=$(uname -s 2>/dev/null || true)
task_arch=$(uname -m 2>/dev/null || true)
if [ "$task_os" != Darwin ]; then
    task_record 'Não aplicável' 'Sistema operacional' 'este diagnóstico é destinado ao macOS'
    exit 1
fi
task_present 'Sistema operacional / processador' "macOS / $task_arch"
if [ "$task_arch" = arm64 ]; then
    task_present 'Apple Silicon' 'processo ARM64 nativo'
elif [ "$task_arch" = x86_64 ]; then
    task_translated=$(sysctl -in sysctl.proc_translated 2>/dev/null || true)
    if [ "$task_translated" = 1 ]; then
        task_broken 'Apple Silicon' 'Terminal em Rosetta; abra uma sessão ARM64 antes de compilar'
    else task_record 'Não aplicável' 'Apple Silicon' 'máquina Intel; projeto também usa cliente macOS'; fi
else task_broken 'Arquitetura' 'arquitetura do processo não reconhecida'; fi

if command -v xcode-select >/dev/null 2>&1; then
    task_clt=$(xcode-select -p 2>/dev/null || true)
    if [ -n "$task_clt" ] && [ -d "$task_clt" ]; then task_present 'Apple Command Line Tools / Xcode' "$task_clt"
    else
        task_absent 'Apple Command Line Tools / Xcode' 'xcode-select -p não aponta para uma instalação disponível'
        task_authorization+=('Somente se a ausência for confirmada: xcode-select --install; instala ferramentas Apple e requer autorização prévia.')
    fi
else task_absent 'xcode-select' 'executável não acessível no Terminal'; fi

task_cc=${CC:-clang}
if command -v "$task_cc" >/dev/null 2>&1; then
    task_cc_version=$("$task_cc" --version 2>/dev/null | head -n 1)
    if [ -n "$task_cc_version" ]; then task_present 'Compilador C' "$task_cc_version"
    else task_broken 'Compilador C' 'não foi possível obter a versão'; fi
    if printf '%s\n' '#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L' \
        '#error C11 necessário' '#endif' 'int main(void) { return 0; }' | \
        "$task_cc" -std=c11 -x c -fsyntax-only - >/dev/null 2>&1; then
        task_present 'Linguagem C11' 'verificada pelo compilador com -std=c11'
    else task_broken 'Linguagem C11' 'compilador não aceitou o código C11'; fi
else task_absent 'Compilador C' 'clang/CC não acessível; verifique clang --version e xcode-select -p'; fi

task_brew_prefix=''
task_brew_odbc=''
if command -v brew >/dev/null 2>&1; then
    task_brew_version=$(brew --version 2>/dev/null | head -n 1)
    task_brew_prefix=$(brew --prefix 2>/dev/null || true)
    task_brew_odbc=$(brew --prefix unixodbc 2>/dev/null || true)
    if [ -n "$task_brew_version" ] && [ -d "$task_brew_prefix" ]; then
        task_present 'Homebrew' "$task_brew_version; prefixo real: $task_brew_prefix"
    else task_broken 'Homebrew' 'não foi possível verificar a versão/prefixo'; fi
    task_formula_odbc=$(brew list --versions unixodbc 2>/dev/null || true)
    task_formula_driver=$(brew list --versions msodbcsql18 2>/dev/null || true)
    if [ -n "$task_formula_odbc" ]; then task_present 'Fórmula unixODBC' "$task_formula_odbc"
    else task_record 'Não verificada' 'Fórmula unixODBC' 'ausente do inventário Homebrew; uma instalação alternativa ainda pode funcionar'; fi
    if [ -n "$task_formula_driver" ]; then task_present 'Fórmula Microsoft Driver 18' "$task_formula_driver"
    else task_record 'Não verificada' 'Fórmula Microsoft Driver 18' 'ausente do inventário Homebrew; será verificado o registro ODBC'; fi
else task_absent 'Homebrew' 'brew não acessível; nenhuma instalação será iniciada'; fi

task_odbc_prefix=${ODBC_PREFIX:-}
if command -v odbc_config >/dev/null 2>&1; then
    task_odbc_version=$(odbc_config --version 2>/dev/null || true)
    task_odbc_config_prefix=$(odbc_config --prefix 2>/dev/null || true)
    if [ -n "$task_odbc_version" ] && [ -d "$task_odbc_config_prefix" ]; then
        task_present 'unixODBC / odbc_config' "versão $task_odbc_version; prefixo real: $task_odbc_config_prefix"
        if [ -z "$task_odbc_prefix" ]; then task_odbc_prefix=$task_odbc_config_prefix; fi
        if [ -n "$task_brew_odbc" ] && [ "$task_brew_odbc" != "$task_odbc_config_prefix" ]; then
            # Homebrew opt and Cellar may be two names for the same installation.
            task_path_a=$(CDPATH= cd -- "$task_brew_odbc" 2>/dev/null && pwd -P || true)
            task_path_b=$(CDPATH= cd -- "$task_odbc_config_prefix" 2>/dev/null && pwd -P || true)
            if [ "$task_path_a" != "$task_path_b" ]; then
                task_broken 'Seleção unixODBC' 'odbc_config e Homebrew apontam para instalações diferentes; escolha um ODBC_PREFIX existente'
            fi
        fi
    else task_broken 'unixODBC / odbc_config' 'prefixo ou versão não disponível'; fi
else
    task_record 'Não verificada' 'odbc_config' 'não acessível; tentando o prefixo unixODBC existente'
    if [ -z "$task_odbc_prefix" ]; then task_odbc_prefix=$task_brew_odbc; fi
fi

if [ -n "$task_odbc_prefix" ] && [ -r "$task_odbc_prefix/include/sql.h" ] && [ -r "$task_odbc_prefix/include/sqlext.h" ]; then
    if command -v "$task_cc" >/dev/null 2>&1 && printf '%s\n' '#include <sql.h>' '#include <sqlext.h>' \
        'int main(void) { (void)SQLAllocHandle; (void)SQLDriverConnect; (void)SQLGetDiagRec; return 0; }' | \
        "$task_cc" -std=c11 -I"$task_odbc_prefix/include" -x c -fsyntax-only - >/dev/null 2>&1; then
        task_present 'Cabeçalhos ODBC' 'sql.h e sqlext.h legíveis e aceitos pelo compilador C11'
    else task_broken 'Cabeçalhos ODBC' 'não foi possível validar os cabeçalhos com o compilador'; fi
else
    task_absent 'Cabeçalhos ODBC' 'sql.h/sqlext.h não encontrados no prefixo unixODBC selecionado'
    task_authorization+=('Somente após confirmar ausência/dano: brew install unixodbc; adiciona bibliotecas/cabeçalhos e requer autorização prévia.')
fi
task_odbc_lib=''
if [ -n "$task_odbc_prefix" ]; then
    if [ -r "$task_odbc_prefix/lib/libodbc.dylib" ]; then task_odbc_lib="$task_odbc_prefix/lib/libodbc.dylib"
    elif [ -r "$task_odbc_prefix/lib/libodbc.a" ]; then task_odbc_lib="$task_odbc_prefix/lib/libodbc.a"; fi
fi
if [ -n "$task_odbc_lib" ]; then
    if command -v lipo >/dev/null 2>&1 && lipo -verify_arch "$task_arch" "$task_odbc_lib" >/dev/null 2>&1; then
        task_present 'Biblioteca libodbc' "legível e compatível com $task_arch"
    elif command -v lipo >/dev/null 2>&1; then task_broken 'Biblioteca libodbc' 'arquitetura incompatível com o processo'
    else task_record 'Não verificada' 'Arquitetura libodbc' 'biblioteca legível; lipo não acessível'; fi
else task_absent 'Biblioteca libodbc' 'biblioteca não encontrada no prefixo selecionado'; fi

task_driver_path=''
if command -v odbcinst >/dev/null 2>&1; then
    task_odbcinst_summary=$(odbcinst -j 2>/dev/null | head -n 1)
    if [ -n "$task_odbcinst_summary" ]; then task_present 'odbcinst' "$task_odbcinst_summary"
    else task_broken 'odbcinst' 'odbcinst -j não retornou o gerenciador'; fi
    if odbcinst -q -d 2>/dev/null | grep -F -x '[ODBC Driver 18 for SQL Server]' >/dev/null 2>&1; then
        task_present 'Microsoft ODBC Driver 18' 'registrado no unixODBC'
        # Print neither complete configuration files nor DSNs, which may contain credentials.
        task_driver_path=$(odbcinst -q -d -n 'ODBC Driver 18 for SQL Server' 2>/dev/null | \
            awk 'tolower($0) ~ /^driver[[:space:]]*=/ {sub(/^[^=]*=[[:space:]]*/, ""); sub(/[[:space:]]*$/, ""); print; exit}')
        if [ -n "$task_driver_path" ] && [ -r "$task_driver_path" ]; then
            if command -v lipo >/dev/null 2>&1 && lipo -verify_arch "$task_arch" "$task_driver_path" >/dev/null 2>&1; then
                task_present 'Biblioteca do Driver 18' "legível e compatível com $task_arch"
            elif command -v lipo >/dev/null 2>&1; then task_broken 'Biblioteca do Driver 18' 'arquitetura incompatível com o processo'
            else task_record 'Não verificada' 'Arquitetura do Driver 18' 'biblioteca legível; lipo não acessível'; fi
            if command -v otool >/dev/null 2>&1; then
                task_deps=$(otool -L "$task_driver_path" 2>/dev/null | tail -n +2 | \
                    awk '{sub(/^[[:space:]]*/, ""); sub(/[[:space:]]+\(compatibility.*$/, ""); print}')
                task_dep_error=0
                task_dep_pending=0
                while IFS= read -r task_dependency; do
                    [ -n "$task_dependency" ] || continue
                    case "$task_dependency" in
                        /System/*|/usr/lib/*) ;; # May reside in the macOS dyld shared cache.
                        /*) if [ ! -r "$task_dependency" ]; then task_dep_error=1; fi ;;
                        @*) task_dep_pending=1 ;;
                        *) task_dep_pending=1 ;;
                    esac
                done <<< "$task_deps"
                if [ "$task_dep_error" -eq 1 ]; then task_broken 'Dependências do Driver 18' 'biblioteca vinculada fora do sistema não encontrada; examine otool -L no arquivo do driver'
                elif [ -z "$task_deps" ] || [ "$task_dep_pending" -eq 1 ]; then task_record 'Não verificada' 'Dependências do Driver 18' 'dependências relativas exigem validação adicional do carregador'
                else task_present 'Dependências do Driver 18' 'bibliotecas externas ao sistema legíveis; dependências Apple fornecidas pelo macOS'; fi
            else task_record 'Não verificada' 'Dependências do Driver 18' 'otool não acessível'; fi
        else task_broken 'Biblioteca do Driver 18' 'registro existe, mas o arquivo Driver não está legível'; fi
    else
        task_absent 'Microsoft ODBC Driver 18' 'nome obrigatório ausente no registro consultado; verifique odbcinst -q -d'
        task_authorization+=('Driver 18 ausente: consultar o guia oficial Microsoft para macOS antes de propor instalação; tap/confiança/instalação nunca são executados por este script.')
    fi
else task_absent 'odbcinst' 'executável não acessível no Terminal; registro do driver não verificado'; fi
for task_odbc_env in ODBCSYSINI ODBCINSTINI ODBCINI; do
    if [ -n "${!task_odbc_env:-}" ]; then
        task_record 'Não verificada' "$task_odbc_env" 'definida; pode selecionar configuração ODBC alternativa; conteúdo não exibido'
        task_pending+=("$task_odbc_env: conferir intenção do caminho alternativo, sem alterar configurações globais.")
    fi
done

printf '\n%s\n' 'Configuração da aplicação (valores privados não são exibidos)'
task_auth=${AGUA_AUTH:-sql}
for task_env in AGUA_SERVER AGUA_DATABASE AGUA_PORT AGUA_AUTH AGUA_USER AGUA_PASSWORD \
    AGUA_LOGIN_TIMEOUT AGUA_CONNECTION_TIMEOUT AGUA_QUERY_TIMEOUT; do
    if [ -n "${!task_env:-}" ]; then task_record 'Configurada' "$task_env" 'definida; conteúdo não exibido'
    else
        case "$task_env" in
            AGUA_DATABASE) task_record 'Configurada' "$task_env" 'padrão MonitoramentoAgua' ;;
            AGUA_PORT) task_record 'Configurada' "$task_env" 'padrão 1433' ;;
            AGUA_AUTH) task_record 'Configurada' "$task_env" 'padrão sql' ;;
            AGUA_LOGIN_TIMEOUT|AGUA_CONNECTION_TIMEOUT) task_record 'Configurada' "$task_env" 'padrão 5 segundos' ;;
            AGUA_QUERY_TIMEOUT) task_record 'Configurada' "$task_env" 'padrão 10 segundos' ;;
            AGUA_USER|AGUA_PASSWORD)
                if [ "$task_auth" = integrated ]; then task_record 'Não aplicável' "$task_env" 'autenticação integrada selecionada'
                else task_record 'Pendente' "$task_env" 'não definida'; fi ;;
            *) task_record 'Pendente' "$task_env" 'não definida' ;;
        esac
    fi
done
task_server=${AGUA_SERVER:-}
case "$task_server" in
    '') task_config_pending=1; task_pending+=('Definir AGUA_SERVER com DNS/IPv4, sem tcp:, porta, vírgula ou atributos ODBC.') ;;
    *[!a-zA-Z0-9.-]*) task_config_error=1; task_adjustments+=('AGUA_SERVER: formato inválido; use DNS/IPv4 e AGUA_PORT separado.') ;;
    *) if [ "${#task_server}" -gt 253 ]; then task_config_error=1; task_adjustments+=('AGUA_SERVER: excede 253 caracteres.'); fi ;;
esac
task_database=${AGUA_DATABASE:-MonitoramentoAgua}
if [ -z "$task_database" ]; then task_database=MonitoramentoAgua; fi
case "$task_database" in *[!a-zA-Z0-9_]*|'') task_config_error=1; task_adjustments+=('AGUA_DATABASE: use letras, números e sublinhado.');; esac
if [ "${#task_database}" -gt 128 ]; then task_config_error=1; task_adjustments+=('AGUA_DATABASE: excede 128 caracteres.'); fi
if ! task_uint "${AGUA_PORT:-1433}" 65535 5; then task_config_error=1; task_adjustments+=('AGUA_PORT: deve ser inteiro de 1 a 65535.'); fi
case "$task_auth" in
    sql)
        if [ -z "${AGUA_USER:-}" ] || [ -z "${AGUA_PASSWORD:-}" ]; then
            task_config_pending=1; task_pending+=('Autenticação SQL: definir AGUA_USER e obter AGUA_PASSWORD de forma segura, sem gravá-la em scripts/histórico.')
        fi
        task_user=${AGUA_USER:-}; task_password=${AGUA_PASSWORD:-}
        if ! task_credentials_within_limits; then task_config_error=1; task_adjustments+=('Credenciais SQL: tamanho superior ao aceito pelo cliente.'); fi
        case "$task_user$task_password" in *$'\r'*|*$'\n'*) task_config_error=1; task_adjustments+=('Credenciais SQL: quebras de linha não são aceitas.');; esac
        unset task_user task_password
        ;;
    integrated) task_record 'Não verificada' 'Autenticação integrada' 'requer identidade Kerberos válida no macOS; nenhuma autenticação realizada' ;;
    *) task_config_error=1; task_adjustments+=('AGUA_AUTH: deve ser sql ou integrated.');;
esac
for task_env in AGUA_LOGIN_TIMEOUT AGUA_CONNECTION_TIMEOUT AGUA_QUERY_TIMEOUT; do
    task_timeout=${!task_env:-}
    if [ -n "$task_timeout" ] && ! task_uint "$task_timeout" 300 3; then
        task_config_error=1; task_adjustments+=("$task_env: deve ser inteiro de 1 a 300 segundos.")
    fi
done
if [ "$task_config_error" -eq 1 ]; then
    task_record 'Com erro' 'Configuração AGUA_*' 'um ou mais valores inválidos; veja ajustes necessários'
elif [ "$task_config_pending" -eq 1 ]; then task_record 'Pendente' 'Configuração AGUA_*' 'faltam parâmetros para conexão real'
else task_record 'Configurada' 'Configuração AGUA_*' 'formato e presença verificados; autenticação ainda não testada'; fi
task_pending+=('Disponibilizar uma instância SQL Server compatível, criar/validar MonitoramentoAgua e configurar permissões e certificado TLS.')
if [ "$task_config_pending" -eq 1 ]; then task_record 'Pendente' 'Servidor SQL Server / banco / TLS' 'conexão ainda não configurada'
else task_record 'Não verificada' 'Servidor SQL Server / banco / TLS' 'nenhuma conexão de rede foi executada'; fi
task_record 'Configurada' 'Política TLS do cliente' 'Driver 18; Encrypt=yes; TrustServerCertificate=no; certificado real ainda não validado'
if [ -x "$task_root/build/aquasense" ] || [ -x "$task_root/build/monitoramento" ]; then
    task_record 'Não verificada' 'Compilação' 'executável presente; este diagnóstico não recompila nem confirma atualização'
else task_record 'Pendente' 'Compilação' 'execute bash scripts/compilar_macos.sh com as ferramentas verificadas'; fi
task_record 'Não verificada' 'Testes locais' 'este diagnóstico não executa a suíte; consulte resultados documentados ou execute tests/run_tests.py'
task_optional_executable sqlcmd 'sqlcmd / mssql-tools18'
task_optional_executable code 'VS Code CLI'
task_record 'Não verificada' 'Extensões VS Code C/C++ e MSSQL' 'opcionais; este diagnóstico não abre o editor nem consulta serviços externos'
task_record 'Não aplicável' 'SQL Server nativo no macOS ARM64' 'cliente remoto; instalação do driver não instala o mecanismo SQL Server'

task_show_category 'Dependências instaladas e verificadas' ${task_installed[@]+"${task_installed[@]}"}
task_show_category 'Dependências obrigatórias ausentes' ${task_missing[@]+"${task_missing[@]}"}
task_show_category 'Dependências/configurações que exigem ajustes' ${task_adjustments[@]+"${task_adjustments[@]}"}
task_show_category 'Ferramentas opcionais' ${task_optional[@]+"${task_optional[@]}"}
task_show_category 'Configurações pendentes' ${task_pending[@]+"${task_pending[@]}"}
task_show_category 'Ações que exigem autorização antes de alterar o ambiente' ${task_authorization[@]+"${task_authorization[@]}"}
printf '\n%s\n' 'Diagnóstico concluído. Nenhuma ferramenta/configuração global foi alterada.'

if [ "$task_local_error" -eq 1 ] || [ "$task_config_error" -eq 1 ]; then exit 1; fi
if [ "$task_test_connection" -eq 1 ]; then
    if [ "$task_config_pending" -eq 1 ]; then
        printf '%s\n' 'Teste de conexão não executado: configuração AGUA_* pendente.' >&2
        exit 1
    fi
    if [ ! -f "$task_root/scripts/testar_conexao.sh" ]; then
        printf '%s\n' 'Teste de conexão não executado: scripts/testar_conexao.sh não encontrado.' >&2
        exit 1
    fi
    bash "$task_root/scripts/testar_conexao.sh"
    exit "$?"
fi
exit 0
