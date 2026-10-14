# Windows PowerShell 5.1+. Diagnostico somente leitura; nunca instala ferramentas.
param([switch]$TestConnection, [switch]$Help)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ($Help) {
    Write-Output 'Uso: scripts\verificar_ambiente_windows.cmd [--test-connection | --help]'
    exit 0
}
$checks = @()
function Add-Check {
    param([string]$Name, [string]$State, [string]$Detail, [bool]$Mandatory = $false)
    # Categorias do Prompt Mestre; ASCII mantem compatibilidade com PowerShell 5.1.
    $State = switch ($State) {
        'OK' { 'Configurada' }
        'DEFINIDA' { 'Configurada' }
        'DISPONIVEL' { 'Configurada' }
        'AJUSTAR' { 'Com erro' }
        'AUSENTE' { if ($Mandatory) { 'Pendente' } else { 'Nao aplicavel' } }
        'NAO_VERIFICADO' { 'Nao verificada' }
        default { 'Pendente' }
    }
    $script:checks += [pscustomobject]@{Item=$Name; Estado=$State; Detalhe=$Detail; Obrigatorio=$Mandatory}
}
function Find-PathEntry {
    param([string]$Entries, [string]$File)
    if ([string]::IsNullOrWhiteSpace($Entries)) { return $null }
    foreach ($entry in ($Entries -split ';')) {
        if (-not [string]::IsNullOrWhiteSpace($entry)) {
            $candidate = Join-Path ($entry.Trim('"')) $File
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    return $null
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    Write-Output 'Diagnostico nao concluido: este script destina-se ao Windows 10/11.'
    exit 1
}
$os64 = [Environment]::Is64BitOperatingSystem
$process64 = [Environment]::Is64BitProcess
if ($os64) {
    Add-Check 'Windows 64 bits' 'OK' 'Sistema operacional de 64 bits confirmado.' $true
} else {
    Add-Check 'Windows 64 bits' 'AUSENTE' 'Compilacao UCRT64 produz cliente x86_64.' $true
}
Add-Check 'PowerShell' 'OK' ('Versao ' + $PSVersionTable.PSVersion.ToString() + '; processo 64 bits: ' + $process64)

$driverName = 'ODBC Driver 18 for SQL Server'
$driverRegistered = $false
$driverFile = $null
$driverRegistrationInspected = $false
$registry = $null
$registeredDrivers = $null
$driverKey = $null
try {
    # Registry64 torna a leitura consistente mesmo a partir de um PowerShell x86.
    $registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    $registeredDrivers = $registry.OpenSubKey('SOFTWARE\ODBC\ODBCINST.INI\ODBC Drivers', $false)
    $driverKey = $registry.OpenSubKey('SOFTWARE\ODBC\ODBCINST.INI\' + $driverName, $false)
    if ($null -ne $registeredDrivers -and $null -ne $driverKey) {
        $driverRegistrationInspected = $true
        $driverRegistered = ($registeredDrivers.GetValue($driverName) -eq 'Installed')
        $driverFile = $driverKey.GetValue('Driver')
    } else {
        $driverRegistrationInspected = $true
    }
} catch {
    # Leitura negada nao e prova de ausencia. Sem mensagens de excecao externas.
    $driverRegistrationInspected = $false
} finally {
    if ($null -ne $driverKey) { $driverKey.Dispose() }
    if ($null -ne $registeredDrivers) { $registeredDrivers.Dispose() }
    if ($null -ne $registry) { $registry.Dispose() }
}
$odbcCommand = Get-Command Get-OdbcDriver -ErrorAction SilentlyContinue
if ($null -ne $odbcCommand) {
    try {
        $driverRecords = @(Get-OdbcDriver -Name $driverName -Platform '64-bit' -ErrorAction Stop)
        if ($driverRecords.Count -gt 0) { $driverRegistered = $true }
    } catch { }
}
if ($driverRegistered -and -not [string]::IsNullOrEmpty($driverFile) -and (Test-Path -LiteralPath $driverFile -PathType Leaf)) {
    try {
        $driverVersionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($driverFile)
        $driverVersion = $driverVersionInfo.FileVersion
        # A DLL Windows usa versao interna 2022.x; a versao comercial e 18.x.
        # Registro exato + DLL + familia de versao confirmam o Driver 18 local.
        if ($driverVersion -match '^(18|2022)\.' -or $driverVersionInfo.ProductVersion -match '^18\.') {
            Add-Check 'Driver ODBC 18 (64 bits)' 'OK' ('Registrado; DLL presente; versao ' + $driverVersion) $true
        } else {
            Add-Check 'Driver ODBC 18 (64 bits)' 'AJUSTAR' 'Registrado, mas a versao da DLL nao foi confirmada como 18.' $true
        }
    } catch {
        Add-Check 'Driver ODBC 18 (64 bits)' 'NAO_VERIFICADO' 'Registro e DLL encontrados; versao nao verificada.' $true
    }
} elseif ($driverRegistered) {
    Add-Check 'Driver ODBC 18 (64 bits)' 'AJUSTAR' 'Registro encontrado, mas o arquivo do driver nao foi confirmado.' $true
} elseif ($driverRegistrationInspected) {
    Add-Check 'Driver ODBC 18 (64 bits)' 'AUSENTE' 'Registro de 64 bits nao encontrado. Nao foi feita instalacao.' $true
} else {
    Add-Check 'Driver ODBC 18 (64 bits)' 'NAO_VERIFICADO' 'Leitura do registro indisponivel; ausencia nao foi presumida.' $true
}

$compilerReady = $false
$gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
if ($null -ne $gcc) {
    try {
        $gccTarget = (& $gcc.Source -dumpmachine 2>$null | Out-String).Trim()
        $targetExit = $LASTEXITCODE
        $compilerProbe = @'
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdint.h>
#ifndef _UCRT
#error GCC deve usar UCRT
#endif
_Static_assert(sizeof(void *) == 8, "cliente ODBC x64");
int main(void) { SQLHENV env = SQL_NULL_HENV; return env != SQL_NULL_HENV; }
'@
        $probeOutput = ($compilerProbe | & $gcc.Source -std=c11 -fsyntax-only -x c - 2>&1 | Out-String)
        $probeExit = $LASTEXITCODE
        $odbcImport = (& $gcc.Source -print-file-name=libodbc32.a 2>$null | Out-String).Trim()
        $importExit = $LASTEXITCODE
        if ($targetExit -eq 0 -and $gccTarget -eq 'x86_64-w64-mingw32' -and $probeExit -eq 0 -and $importExit -eq 0 -and $odbcImport -ne 'libodbc32.a' -and (Test-Path -LiteralPath $odbcImport -PathType Leaf)) {
            $compilerReady = $true
            Add-Check 'GCC UCRT64/C11' 'OK' 'Headers Windows/sql.h/sqlext.h e importacao odbc32 confirmados.'
        } else {
            Add-Check 'GCC UCRT64/C11' 'AJUSTAR' 'Executavel acessivel; arquitetura, UCRT, headers ou importacao nao validados. Abra o terminal UCRT64 existente.'
        }
        $probeOutput = $null
    } catch {
        Add-Check 'GCC UCRT64/C11' 'NAO_VERIFICADO' 'Executavel encontrado; verificacao nao concluida.'
    }
} else {
    Add-Check 'GCC UCRT64/C11' 'NAO_VERIFICADO' 'Nao acessivel pelo CMD atual; pode estar instalado no terminal UCRT64.'
}
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if ($null -ne $cl) {
    $sqlHeader = Find-PathEntry $env:INCLUDE 'sql.h'
    $sqlExtHeader = Find-PathEntry $env:INCLUDE 'sqlext.h'
    $windowsHeader = Find-PathEntry $env:INCLUDE 'windows.h'
    $odbcLib = Find-PathEntry $env:LIB 'odbc32.lib'
    $clVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($cl.Source).FileVersion
    $clCompatible = $false
    if (-not [string]::IsNullOrEmpty($clVersion)) {
        $versionNumbers = [regex]::Match($clVersion, '^(\d+)\.(\d+)')
        if ($versionNumbers.Success) {
            $clCompatible = ([int]$versionNumbers.Groups[1].Value -gt 19 -or ([int]$versionNumbers.Groups[1].Value -eq 19 -and [int]$versionNumbers.Groups[2].Value -ge 28))
        }
    }
    $clX64 = ($cl.Source -match '[\\/]Host(x86|x64)[\\/]x64[\\/]cl\.exe$')
    if ($clCompatible -and $clX64 -and $null -ne $sqlHeader -and $null -ne $sqlExtHeader -and $null -ne $windowsHeader -and $null -ne $odbcLib -and $odbcLib -match '[\\/]x64[\\/]') {
        $compilerReady = $true
        Add-Check 'MSVC/Windows SDK' 'OK' ('MSVC ' + $clVersion + ' x64; headers e odbc32.lib localizados. Compilacao deve confirmar C11.')
    } else {
        Add-Check 'MSVC/Windows SDK' 'NAO_VERIFICADO' 'MSVC acessivel; C11/x64 ou headers/biblioteca nao confirmados. Abra o Developer Command Prompt x64 existente.'
    }
} else {
    Add-Check 'MSVC/Windows SDK' 'NAO_VERIFICADO' 'Nao acessivel no terminal atual; alternativa ao GCC, nao precisa ser instalado se GCC funcional.'
}
if ($compilerReady) {
    Add-Check 'Compilacao local' 'OK' 'Uma cadeia C11 x64 existente atende aos arquivos do cliente.' $true
} else {
    Add-Check 'Compilacao local' 'NAO_VERIFICADO' 'Nenhuma cadeia C11 x64 foi validada neste terminal; nao instalar antes de verificar o terminal correto.' $true
}
foreach ($tool in @('code', 'sqlcmd', 'git', 'python')) {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if ($null -ne $found) {
        Add-Check ($tool + ' (opcional)') 'DISPONIVEL' 'Acessivel no terminal atual; nao condiciona o cliente C.'
    } else {
        Add-Check ($tool + ' (opcional)') 'AUSENTE' 'Opcional; nenhuma instalacao automatica.'
    }
}
foreach ($variable in @('AGUA_SERVER','AGUA_PORT','AGUA_DATABASE','AGUA_AUTH','AGUA_USER','AGUA_PASSWORD','AGUA_LOGIN_TIMEOUT','AGUA_CONNECTION_TIMEOUT','AGUA_QUERY_TIMEOUT')) {
    $value = [Environment]::GetEnvironmentVariable($variable, 'Process')
    if ([string]::IsNullOrEmpty($value)) {
        Add-Check $variable 'NAO_DEFINIDA' 'Somente presenca verificada; valores nao sao impressos.'
    } else {
        Add-Check $variable 'DEFINIDA' 'Somente presenca verificada; valores nao sao impressos.'
    }
    $value = $null
}
$configInvalid = $false
foreach ($variable in @('AGUA_SERVER','AGUA_PORT','AGUA_DATABASE','AGUA_AUTH','AGUA_LOGIN_TIMEOUT','AGUA_CONNECTION_TIMEOUT','AGUA_QUERY_TIMEOUT')) {
    $value = [Environment]::GetEnvironmentVariable($variable, 'Process')
    if ([string]::IsNullOrEmpty($value)) { continue }
    $invalid = $false
    switch ($variable) {
        'AGUA_SERVER' { $invalid = ($value.Length -gt 253 -or $value -notmatch '^[A-Za-z0-9.-]+\z') }
        'AGUA_DATABASE' { $invalid = ($value.Length -gt 128 -or $value -notmatch '^[A-Za-z0-9_]+\z') }
        'AGUA_AUTH' { $invalid = ($value -cne 'sql' -and $value -cne 'integrated') }
        'AGUA_PORT' {
            $numeric = 0
            $invalid = ($value.Length -gt 5 -or $value -notmatch '^[0-9]+\z' -or -not [int]::TryParse($value, [ref]$numeric) -or $numeric -lt 1 -or $numeric -gt 65535)
        }
        default {
            $numeric = 0
            $invalid = ($value.Length -gt 3 -or $value -notmatch '^[0-9]+\z' -or -not [int]::TryParse($value, [ref]$numeric) -or $numeric -lt 1 -or $numeric -gt 300)
        }
    }
    if ($invalid) {
        Add-Check ($variable + ' (formato)') 'AJUSTAR' 'Configuracao invalida para o contrato do cliente; valor mantido oculto.' $true
        $configInvalid = $true
    }
    $value = $null
}
if (-not $configInvalid) {
    Add-Check 'Configuracao AGUA_*' 'OK' 'Formatos nao secretos definidos validados. Ausencias do servidor/autenticacao permanecem pendentes.'
}
$serverConfigured = -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('AGUA_SERVER', 'Process'))
if ($serverConfigured) {
    Add-Check 'SQL Server/TLS/esquema' 'PENDENTE' 'Endereco configurado; disponibilidade e certificado exigem teste real explicito.'
} else {
    Add-Check 'SQL Server/TLS/esquema' 'PENDENTE' 'Infraestrutura ainda nao escolhida; driver instalado nao e o servidor.'
}
$checks | Format-Table Item, Estado, Detalhe -Wrap -AutoSize | Out-String -Width 180 | Write-Output
Write-Output 'Acoes dependentes de autorizacao: qualquer instalacao, atualizacao ou alteracao global, somente apos demonstrar a necessidade.'
Write-Output 'Nenhuma ferramenta foi instalada, PATH alterado ou configuracao persistente gravada.'
$failures = @($checks | Where-Object { $_.Obrigatorio -and $_.Estado -ne 'Configurada' })
if ($failures.Count -gt 0) {
    Write-Output 'Diagnostico local nao concluido: resolva os itens obrigatorios no ambiente ja instalado.'
    exit 1
}
if ($TestConnection) {
    & (Join-Path $PSScriptRoot 'testar_conexao_windows.ps1') -NoPrompt
    exit $LASTEXITCODE
}
Write-Output 'Diagnostico local concluido; teste real SQL Server permanece separado.'
exit 0
