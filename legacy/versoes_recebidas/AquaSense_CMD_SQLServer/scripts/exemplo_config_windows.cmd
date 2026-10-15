@echo off
REM Edite APENAS os valores abaixo. Nao compartilhe arquivos preenchidos com credenciais.
set "AQUA_SERVER=localhost,1433"
set "AQUA_DATABASE=MonitoramentoAgua"
set "AQUA_USER=usuario_aplicacao"
set "AQUA_PASSWORD=SUBSTITUIR_SENHA"
REM Execute depois de compilar pelo MSYS2 UCRT64.
build\aquasense.exe
