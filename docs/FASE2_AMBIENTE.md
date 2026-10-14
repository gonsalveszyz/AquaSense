# Fase 2 — diagnóstico, compilação e conexão

Data: 14/10/2026. Pasta ativa: `monitoramento_agua_c_sql` em Downloads. O usuário autorizou continuidade incremental sem novos pedidos de aprovação de fase. A Fase 1 e seu checkpoint `5caf178` permanecem preservados; a autorização atual está registrada por acréscimo em `AGENTS.md`.

## Diagnóstico e preservação

| Categoria | Estado observado/pendência |
| --- | --- |
| Dependências locais configuradas | macOS ARM64; Homebrew 7.0.9; unixODBC 2.3.14; Driver 18.7.1.1 registrado; Clang 21; Apple Command Line Tools; `sql.h`, `sqlext.h` e bibliotecas ARM64 |
| Dependências obrigatórias ausentes no cliente | Nenhuma identificada no ambiente macOS fornecido |
| Ajustes necessários | Nenhum ajuste global demonstrado; os scripts reutilizam os caminhos das instalações existentes |
| Opcionais | `sqlcmd`/`mssql-tools18` e extensão MSSQL do VS Code; sua ausência não impede compilação/execução C por ODBC |
| Configurações pendentes | Infraestrutura SQL Server; endereço DNS/porta; `MonitoramentoAgua`; identidade de aplicação; certificado/cadeia confiável; validação da comunicação real |
| Ações externas ainda não definidas | Escolha de host/serviço e custos. Nenhum serviço contratado/provisionado, firewall aberto ou alteração do Mac executada |

Os verificadores mostram valores privados somente como presentes/ausentes; não imprimem senha, usuário, host ou string de conexão. Em caso de ausência, oferecem diagnóstico e orientação, sem invocar instaladores. Comandos Homebrew utilizados são somente `--version`, `--prefix` e inventário `list --versions`.

No macOS, o diagnóstico classifica itens como **Configurada**, **Pendente**, **Com erro**, **Não verificada** ou **Não aplicável**; o helper Windows usa as mesmas categorias em ASCII (`Nao verificada`, `Nao aplicavel`) para compatibilidade com PowerShell 5.1. Uma biblioteca legível não equivale a conexão validada; um executável presente não equivale a compilação atual. Sem a opção de rede, servidor, banco, autenticação e certificado permanecem pendentes/não verificados.

## Scripts e contratos

| Arquivo | Função |
| --- | --- |
| `scripts/verificar_ambiente_macos.sh` | Verifica macOS/arquitetura, CLT, C11, unixODBC, cabeçalhos/bibliotecas, registro do Driver 18, possíveis conflitos de prefixos, variáveis por presença/formato e ferramentas opcionais |
| `scripts/verificar_ambiente_windows.cmd` | Entrada CMD para o diagnóstico Windows, implementado por helper PowerShell, sem instalar dependências |
| `scripts/compilar_macos.sh` | Reutiliza `build.sh`; mantém `build/monitoramento`, acrescenta `build/aquasense` e compila o diagnóstico C |
| `scripts/compilar_windows_ucrt64.sh` | Compila C11 com GCC do MSYS2 UCRT64 existente e ODBC do Windows; cria executáveis para CMD |
| `scripts/compilar_diagnostico_macos.sh` | Compila apenas `build/diagnostico_conexao` com a camada ODBC de produção |
| `scripts/testar_conexao.sh` | Tentativa explícita de conexão/diagnóstico; senha sem eco quando houver Terminal interativo |
| `scripts/testar_conexao_windows.cmd` | Entrada CMD para o mesmo diagnóstico, com senha lida de modo seguro pelo helper PowerShell |
| `examples/diagnostico_conexao.c` | Diagnóstico SQL somente leitura usando a conexão, timeouts, TLS e limpeza existentes |
| `tests/test_scripts.py` | Testes offline da lógica dos scripts com ferramentas/processo C substituídos por resultados controlados |

Os scripts macOS aceitam `--help`. O verificador aceita `--test-connection`; sem essa opção nunca testa rede e não recompila. Os testes de conexão aceitam `--no-prompt`; sem senha no ambiente, essa opção falha claramente e não lê senha da entrada padrão. Não execute `testar_conexao.sh` com `source`.

Saída `0` do verificador local significa que as verificações locais necessárias não encontraram erro; pode coexistir com **Servidor SQL Server: Pendente**. Saída `1` indica falha de dependência/configuração ou operação; `2` indica argumento/configuração obrigatória inválida no script de conexão. Uma tentativa delegada propaga o código da operação filha. Não inferir sucesso remoto a partir do retorno do diagnóstico local.

Os scripts criam artefatos apenas no projeto ou temporários de verificação. Não alteram PATH permanente, arquivos de perfil, links globais, registro ODBC ou configurações do sistema. `CC` e `ODBC_PREFIX` selecionam instalações existentes somente para o processo.

## Executar no MacBook

```bash
bash scripts/verificar_ambiente_macos.sh
bash scripts/compilar_macos.sh
./build/aquasense --check
```

O último comando requer uma configuração real. Nesta fase, o cliente ainda utiliza `AGUA_*`, com DNS/IPv4 e porta separados; não usar `AQUA_SERVER=tcp:host,1433`. Driver 18 e opções de TLS permanecem fixos no código. O alias `aquasense` executa as funcionalidades existentes; `--demo` e gráficos ASCII continuam pendentes da fase funcional apropriada.

Depois de disponibilizar o servidor e o esquema:

```bash
export AGUA_SERVER='sql.exemplo.interno'
export AGUA_PORT='1433'
export AGUA_DATABASE='MonitoramentoAgua'
export AGUA_AUTH='sql'
export AGUA_USER='agua_cliente'
bash scripts/testar_conexao.sh
```

O host e usuário acima são exemplos. A senha é solicitada sem eco e não aparece no histórico. Para integração automatizada, fornecer `AGUA_PASSWORD` por configuração segura ao processo e usar `--no-prompt`, sem argumento de senha ou arquivo compartilhado. `AGUA_LOGIN_TIMEOUT`, `AGUA_CONNECTION_TIMEOUT` e `AGUA_QUERY_TIMEOUT` mantêm padrões 5/5/10 segundos e intervalo válido 1–300.

O diagnóstico C conecta com `Encrypt=yes;TrustServerCertificate=no`, confirma Driver 18/esquema 3, executa consulta simples, verifica as quatro tabelas de domínio, a presença das colunas essenciais visíveis e permissões essenciais do cliente e fecha a conexão. Verificar permissão `INSERT` não grava uma linha. O script não cria banco, não migra dados e não corrige um esquema incompleto automaticamente. Essa verificação de presença/permissão não substitui a validação de todas as FKs, restrições e índices por `sql/validar_integridade.sql`.

## Executar no Windows

No CMD:

```bat
scripts\verificar_ambiente_windows.cmd
```

Para compilar pelo MSYS2 UCRT64 existente, abrir aquele ambiente e executar:

```bash
bash scripts/compilar_windows_ucrt64.sh
```

O GCC selecionado deve gerar Windows x86-64 e oferecer os cabeçalhos `sql.h`/`sqlext.h` e `odbc32`. O script usa C11 e evita vincular o cliente ao runtime POSIX do MSYS. Depois de compilar, no CMD:

```bat
set "AGUA_SERVER=sql.exemplo.interno"
set "AGUA_PORT=1433"
set "AGUA_DATABASE=MonitoramentoAgua"
set "AGUA_AUTH=sql"
set "AGUA_USER=agua_cliente"
scripts\testar_conexao_windows.cmd
build\aquasense.exe
```

Os `.cmd` chamam o PowerShell existente e não alteram a política de execução permanentemente. A rota MSVC `build.bat` foi preservada e está documentada no README. O requisito de execução é Driver 18 correspondente à arquitetura e as DLLs de runtime efetivamente vinculadas, quando aplicável; o MSYS2 é requisito de desenvolvimento da rota GCC. **Windows não foi executado nesta sessão macOS**; testes com ferramentas falsas não validam PE, carregamento de DLLs, PowerShell/CMD ou ODBC Windows real.

## Testes e limites

```bash
python3 tests/test_scripts.py
python3 tests/run_tests.py
```

A primeira suíte requer host POSIX com Bash; em Windows nativo os casos ficam explicitamente ignorados. Ela copia os scripts para um projeto temporário com espaços no caminho, usa executáveis controlados no PATH e verifica falhas, ausência de dependências, configuração, arquitetura, delegação do código de saída, ausência de rede automática e não exposição de credenciais. Comandos de instalação não são chamados. O diagnóstico macOS real e a compilação real são evidências separadas desses testes.

Resultados locais desta fase: **44 testes de scripts aprovados** e **75 testes C/ODBC simulado aprovados** (68 de regressão e sete métodos novos de diagnóstico, com cenários adicionais), também **75 aprovados com ASan/UBSan**. A segunda suíte compila o C real com ODBC simulado; a ampliação da Fase 2 testa também o diagnóstico somente leitura, metadados/permissões, falhas e limpeza. Os resultados desta fase ficam em `docs/test-results/phase2-*` e em `docs/TESTES.md`. As evidências anteriores continuam registradas: 68 testes do cliente, 33 de migração offline e 19 integrações ignoradas; esses números históricos não devem ser confundidos com a suíte ampliada.

Conexão SQL Server, autenticação real, certificado TLS, execução T-SQL, cadastro remoto/persistência e testes Windows continuam pendentes. A ausência de infraestrutura foi confirmada pelo usuário. Consulte `INFRAESTRUTURA_SQL_SERVER.md` para a Fase 3; a implementação de demonstração/gráficos/GUI não avança para fases dependentes da validação SQL.

## Alterações e recuperação

Adicionados scripts e diagnóstico C; ampliados testes; atualizados README, documentação de testes e autorização de continuidade em AGENTS. `build.sh` passa a publicar o binário somente após compilação bem-sucedida, preservando o executável anterior em falha; seu nome/contrato foram mantidos. `build.bat`, CLI/menu/relatórios, camadas de configuração/ODBC, esquema 3, arquivos SQL e pacotes de migração existentes foram preservados. O alias de executável não muda o contrato do banco. Nenhuma reinstalação, atualização ou reconfiguração global foi necessária.

O diagnóstico C foi compilado com `-Werror` e analisado pelo Clang sem diagnósticos. Uma tentativa real contra DNS deliberadamente indisponível retornou erro controlado e não expôs a senha fictícia; não estabeleceu uma sessão SQL Server. Evidência em `docs/test-results/phase2-real-probe.json`.

Se o Windows bloquear o helper PowerShell pela política de execução, registre a falha e consulte `Get-ExecutionPolicy -List`. Os scripts não modificam essa política. A validação em Windows deve considerar assinatura/permissão do helper sob a política existente. [Políticas oficiais do PowerShell](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_execution_policies?view=powershell-5.1).

## VS Code — configurações de projeto

Adicionados `.vscode/tasks.json`, `settings.json` e `extensions.json`. Não instalam extensões nem guardam credenciais. Abra a pasta do projeto como workspace: as tarefas usam `${workspaceFolder}` e scripts presentes. A compilação macOS é a tarefa padrão; no Windows selecione MSVC em um VS Code iniciado no Developer Command Prompt existente. A rota UCRT64 permanece disponível pelo script no terminal MSYS2 UCRT64. O alias Windows vem dessa rota; a tarefa de execução Windows usa `monitoramento.exe` para funcionar também com MSVC.

Não foi criado `launch.json`: a depuração depende de verificar um backend compatível no ambiente escolhido, evitando caminhos ou adaptadores fictícios. A tarefa de demonstração será adicionada quando `--demo` estiver implementado; as tarefas atuais não anunciam funcionalidades ausentes. JSONs e caminhos de scripts foram conferidos localmente; execução pelo VS Code/Windows permanece a validar.
