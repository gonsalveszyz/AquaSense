@echo off
setlocal
set "AGUA_CONNECTION_OPTION="
if "%~1"=="" goto run
if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help
if /i "%~1"=="--no-prompt" (
    if not "%~2"=="" goto badargs
    set "AGUA_CONNECTION_OPTION=-NoPrompt"
    goto run
)
:badargs
echo Uso: scripts\testar_conexao_windows.cmd [--no-prompt ^| --help]
echo Configure AGUA_SERVER, AGUA_PORT, AGUA_DATABASE, AGUA_AUTH e AGUA_USER no processo atual.
exit /b 2
:help
if not "%~2"=="" goto badargs
echo Uso: scripts\testar_conexao_windows.cmd [--no-prompt ^| --help]
echo Testa conexao, esquema 3 e consultas de leitura pelo cliente C com TLS obrigatorio.
echo Senha SQL: AGUA_PASSWORD existente ou prompt oculto. Nunca recebe senha por argumento.
echo --no-prompt permite automacao; nao solicita senha se ela estiver ausente.
exit /b 0
:run
where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo Teste nao concluido: Windows PowerShell nao esta acessivel.
    exit /b 1
)
powershell.exe -NoLogo -NoProfile -File "%~dp0testar_conexao_windows.ps1" %AGUA_CONNECTION_OPTION%
exit /b %errorlevel%
