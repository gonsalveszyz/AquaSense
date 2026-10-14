@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
where cl >nul 2>nul
if errorlevel 1 (
    echo Abra o Developer Command Prompt x64 do Visual Studio ou Build Tools.
    exit /b 1
)
cl /nologo /std:c11 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /TC monitoramento.c config.c input.c db_odbc.c odbc_diag.c reports_odbc.c report_input.c csv_report.c demo.c ascii_report.c /Fo:build\ /Fe:build\monitoramento.exe /link odbc32.lib
if errorlevel 1 exit /b 1
echo Cliente compilado: build\monitoramento.exe
