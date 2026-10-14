@echo off
setlocal
set "AGUA_DIAG_OPTION="
if "%~1"=="" goto run
if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help
if /i "%~1"=="--test-connection" (
    if not "%~2"=="" goto badargs
    set "AGUA_DIAG_OPTION=-TestConnection"
    goto run
)
:badargs
echo Uso: scripts\verificar_ambiente_windows.cmd [--test-connection ^| --help]
exit /b 2
:help
if not "%~2"=="" goto badargs
echo Uso: scripts\verificar_ambiente_windows.cmd [--test-connection ^| --help]
echo Diagnostico somente leitura. Conexao SQL somente com --test-connection.
exit /b 0
:run
where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo Diagnostico nao concluido: Windows PowerShell nao esta acessivel.
    exit /b 1
)
powershell.exe -NoLogo -NoProfile -File "%~dp0verificar_ambiente_windows.ps1" %AGUA_DIAG_OPTION%
exit /b %errorlevel%
