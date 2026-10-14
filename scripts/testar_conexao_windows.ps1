# Windows PowerShell 5.1+. Nao instala nem modifica ambiente persistente.
param([switch]$NoPrompt, [switch]$Help)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ($Help) {
    Write-Output 'Uso: scripts\testar_conexao_windows.cmd [--no-prompt | --help]'
    exit 0
}
if (-not [Environment]::Is64BitOperatingSystem) {
    Write-Output 'Teste nao concluido: o cliente desta compilacao exige Windows de 64 bits.'
    exit 1
}
$projectDir = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $projectDir 'build\diagnostico_conexao.exe'
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    Write-Output 'Teste nao concluido: compile build\diagnostico_conexao.exe pelo script UCRT64 ou conforme a documentacao MSVC.'
    exit 1
}
$server = [Environment]::GetEnvironmentVariable('AGUA_SERVER', 'Process')
if ([string]::IsNullOrWhiteSpace($server)) {
    Write-Output 'PENDENTE: instancia SQL Server ainda nao configurada. Defina AGUA_SERVER antes do teste real.'
    exit 2
}
# Mesmo contrato do cliente: hostname DNS/IPv4, porta separada, sem DSN livre.
if ($server.Length -gt 253 -or $server -notmatch '^[A-Za-z0-9.-]+\z') {
    Write-Output 'Teste nao concluido: AGUA_SERVER deve conter apenas um hostname DNS ou IPv4; use AGUA_PORT separadamente.'
    exit 2
}
$portText = [Environment]::GetEnvironmentVariable('AGUA_PORT', 'Process')
if (-not [string]::IsNullOrEmpty($portText)) {
    $portNumber = 0
    if ($portText.Length -gt 5 -or $portText -notmatch '^[0-9]+\z' -or -not [int]::TryParse($portText, [ref]$portNumber) -or $portNumber -lt 1 -or $portNumber -gt 65535) {
        Write-Output 'Teste nao concluido: AGUA_PORT deve estar entre 1 e 65535.'
        exit 2
    }
}
$databaseName = [Environment]::GetEnvironmentVariable('AGUA_DATABASE', 'Process')
if (-not [string]::IsNullOrEmpty($databaseName) -and ($databaseName.Length -gt 128 -or $databaseName -notmatch '^[A-Za-z0-9_]+\z')) {
    Write-Output 'Teste nao concluido: AGUA_DATABASE aceita letras, numeros e sublinhado, ate 128 caracteres.'
    exit 2
}
foreach ($timeoutVariable in @('AGUA_LOGIN_TIMEOUT','AGUA_CONNECTION_TIMEOUT','AGUA_QUERY_TIMEOUT')) {
    $timeoutText = [Environment]::GetEnvironmentVariable($timeoutVariable, 'Process')
    if (-not [string]::IsNullOrEmpty($timeoutText)) {
        $timeoutNumber = 0
        if ($timeoutText.Length -gt 3 -or $timeoutText -notmatch '^[0-9]+\z' -or -not [int]::TryParse($timeoutText, [ref]$timeoutNumber) -or $timeoutNumber -lt 1 -or $timeoutNumber -gt 300) {
            Write-Output ('Teste nao concluido: ' + $timeoutVariable + ' deve ser um inteiro entre 1 e 300 segundos.')
            exit 2
        }
    }
}
$auth = [Environment]::GetEnvironmentVariable('AGUA_AUTH', 'Process')
if ([string]::IsNullOrEmpty($auth)) { $auth = 'sql' }
if ($auth -cne 'sql' -and $auth -cne 'integrated') {
    Write-Output 'Teste nao concluido: AGUA_AUTH deve ser sql ou integrated.'
    exit 2
}
$credentialText = $null
$secureCredential = $null
$credentialBstr = [IntPtr]::Zero
$startInfo = $null
$child = $null
$result = 1
try {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $probe
    $startInfo.WorkingDirectory = $projectDir
    $startInfo.UseShellExecute = $false
    # O filho herda configuracoes AGUA_* existentes; nenhum valor e impresso.
    $startInfo.EnvironmentVariables['AGUA_AUTH'] = $auth
    if ($auth -ceq 'sql') {
        $userName = [Environment]::GetEnvironmentVariable('AGUA_USER', 'Process')
        if ([string]::IsNullOrEmpty($userName) -or $userName.Length -gt 256 -or $userName -match '[\r\n]') {
            Write-Output 'Teste nao concluido: configure AGUA_USER para autenticacao SQL; nao envie senhas pelo chat.'
            $result = 2
        } else {
            $credentialText = [Environment]::GetEnvironmentVariable('AGUA_PASSWORD', 'Process')
            if ([string]::IsNullOrEmpty($credentialText)) {
                if ($NoPrompt -or [Console]::IsInputRedirected) {
                    Write-Output 'Teste nao concluido: senha ausente e prompt desabilitado. Use um terminal interativo ou injecao segura no processo.'
                    $result = 2
                } else {
                    $secureCredential = Read-Host 'Senha SQL (entrada oculta, somente para o processo filho)' -AsSecureString
                    $credentialBstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureCredential)
                    $credentialText = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($credentialBstr)
                }
            }
            if (-not [string]::IsNullOrEmpty($credentialText) -and ($credentialText.Length -gt 1024 -or $credentialText -match '[\r\n]')) {
                Write-Output 'Teste nao concluido: a credencial excede o limite do cliente ou contem quebra de linha.'
                $result = 2
            } elseif (-not [string]::IsNullOrEmpty($credentialText)) {
                $startInfo.EnvironmentVariables['AGUA_PASSWORD'] = $credentialText
                $result = 0
            } elseif ($result -ne 2) {
                Write-Output 'Teste nao concluido: senha SQL vazia.'
                $result = 2
            }
        }
    } else {
        $startInfo.EnvironmentVariables.Remove('AGUA_PASSWORD')
        $result = 0
    }
    if ($result -eq 0) {
        Write-Output 'Iniciando teste C/ODBC: Encrypt=yes, TrustServerCertificate=no; somente consultas de leitura.'
        $child = [Diagnostics.Process]::Start($startInfo)
        $child.WaitForExit()
        $result = $child.ExitCode
        if ($result -eq 0) {
            Write-Output 'Teste de conexao e desconexao concluido. Confira o diagnostico C para tabelas e esquema.'
        } else {
            Write-Output 'Operacao nao concluida; o cliente C informou o diagnostico ODBC acima.'
        }
    }
} catch {
    # Nao ecoar Exception.Message: pode incluir argumentos/valores externos.
    Write-Output 'Teste nao concluido: nao foi possivel ler a credencial ou iniciar o cliente. Confira terminal e dependencias DLL.'
    $result = 1
} finally {
    if ($null -ne $startInfo) { $startInfo.EnvironmentVariables.Remove('AGUA_PASSWORD') }
    if ($credentialBstr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($credentialBstr) }
    if ($null -ne $secureCredential) { $secureCredential.Dispose() }
    if ($null -ne $child) { $child.Dispose() }
    # Strings gerenciadas sao imutaveis: elimina referencias, sem prometer sobrescrita.
    $credentialText = $null
    $secureCredential = $null
}
exit $result
