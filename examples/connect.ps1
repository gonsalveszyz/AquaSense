# Executar: .\examples\connect.ps1 -ServerName <DNS> -SqlUser <usuario-SQL>
param(
    [string]$ServerName = $env:AGUA_SERVER,
    [string]$SqlUser = $env:AGUA_USER
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ServerName) -or
    [string]::IsNullOrWhiteSpace($SqlUser)) {
    Write-Error 'Informe -ServerName e -SqlUser, ou AGUA_SERVER e AGUA_USER.' -ErrorAction Continue
    exit 2
}
$aguaExampleBinary = Join-Path $PSScriptRoot '..\build\monitoramento.exe'
if (-not (Test-Path -LiteralPath $aguaExampleBinary -PathType Leaf)) {
    Write-Error 'Cliente ausente: compile primeiro com build.bat.' -ErrorAction Continue
    exit 1
}

$env:AGUA_SERVER = $ServerName
$env:AGUA_USER = $SqlUser
$env:AGUA_AUTH = 'sql'
if (-not $env:AGUA_PORT) { $env:AGUA_PORT = '1433' }
if (-not $env:AGUA_DATABASE) { $env:AGUA_DATABASE = 'MonitoramentoAgua' }
if (-not $env:AGUA_LOGIN_TIMEOUT) { $env:AGUA_LOGIN_TIMEOUT = '5' }
if (-not $env:AGUA_CONNECTION_TIMEOUT) { $env:AGUA_CONNECTION_TIMEOUT = '5' }
if (-not $env:AGUA_QUERY_TIMEOUT) { $env:AGUA_QUERY_TIMEOUT = '10' }

$aguaExampleSecurePassword = $null
$aguaExamplePasswordPtr = [IntPtr]::Zero
$aguaExampleResult = 1
try {
    $aguaExampleSecurePassword = Read-Host 'Senha SQL Server' -AsSecureString
    $aguaExamplePasswordPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($aguaExampleSecurePassword)
    $env:AGUA_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($aguaExamplePasswordPtr)
    & $aguaExampleBinary --check
    $aguaExampleResult = $LASTEXITCODE
} finally {
    Remove-Item Env:AGUA_PASSWORD -ErrorAction SilentlyContinue
    if ($aguaExamplePasswordPtr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($aguaExamplePasswordPtr)
    }
    if ($null -ne $aguaExampleSecurePassword) {
        $aguaExampleSecurePassword.Dispose()
    }
}
exit $aguaExampleResult
