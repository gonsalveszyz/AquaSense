# Servidor SQL Server local no macOS

Ambiente preparado e validado em 14/10/2026 no MacBook Air M4. O AquaSense
conecta ao SQL Server real em `localhost:1433`, usa o esquema 3 e preserva as
duas leituras originais do projeto. O servidor foi criado para desenvolvimento
local, sem serviço pago ou exposição à rede externa.

## Uso diário

No Terminal, entre na cópia ativa do projeto:

```sh
cd /Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql
```

Inicie a VM, o SQL Server e o túnel local:

```sh
bash scripts/iniciar_servidor_local.sh
```

Abra o menu do programa:

```sh
bash scripts/executar_local.sh
```

Para consultar sem abrir o menu:

```sh
bash scripts/executar_local.sh --list
```

O wrapper injeta as credenciais e a configuração TLS somente no processo do
cliente. Não é necessário exportar senhas no Terminal ou alterar a configuração
global do Mac. O banco e os usuários já foram inicializados: não execute o
bootstrap novamente para uso diário.

Comandos de diagnóstico, consulta de estado e encerramento:

```sh
python3 scripts/servidor_local.py check
python3 scripts/servidor_local.py status
python3 scripts/servidor_local.py stop
```

`check` executa o diagnóstico C de conexão, esquema e metadados. `status`
informa o estado da VM, do container e do túnel. `stop` encerra o túnel, o
container e a VM, preservando o volume e os arquivos locais. Para retomar,
use novamente `iniciar_servidor_local.sh`.

Se os executáveis estiverem ausentes após uma limpeza da pasta `build/`,
recompile com as ferramentas já instaladas:

```sh
bash scripts/compilar_macos.sh
```

## Arquitetura e recursos

| Componente | Configuração local |
| --- | --- |
| Servidor | SQL Server 2022 Developer `16.0.4305.5`, container Linux `amd64` |
| Execução no Mac M4 | VM Linux `x86_64` emulada por QEMU, perfil Colima `aquasense` |
| VM | 4 CPUs, 6 GiB de memória e disco virtual de 30 GiB |
| Container | `aquasense-sqlserver`, limite de memória de 5 GiB |
| Memória do SQL Server | `memorylimitmb=4096` |
| Contexto Docker | `colima-aquasense`, informado explicitamente pelos scripts |
| Dados persistentes | Volume Docker `aquasense-sqlserver-data`, em `/var/opt/mssql` |
| Endereço do cliente | `localhost:1433`, acessível no Mac por `127.0.0.1:1433` |
| Banco de uso local | `MonitoramentoAgua`, conta restrita `agua_cliente` |
| Banco de integração | `MonitoramentoAguaTest`, conta restrita `agua_teste` |

Esta execução em Apple Silicon é **experimental**. A Microsoft suporta as
imagens de SQL Server em hosts Linux com CPU Intel/AMD x86-64 e informa que
ambientes de emulação ou tradução, incluindo QEMU, não são testados nem
suportados. Utilize este arranjo para desenvolvimento local; a implantação
definitiva precisa de uma plataforma compatível. [Política oficial de instalação
e suporte](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

A emulação tem custo de desempenho. A inicialização observada ficou na ordem
de um minuto, mas pode demorar mais conforme a carga do Mac e o estado da VM;
esse tempo não é uma garantia. Execute `stop` quando terminar para liberar os
recursos usados pela VM.

A imagem inicialmente obtida de `mcr.microsoft.com/mssql/server:2022-latest`
fica registrada por digest em `.local/sqlserver/image.json`. Os scripts
reutilizam a imagem registrada e não fazem atualização automática a cada
inicialização. O contexto Docker padrão do usuário é preservado.

## Ferramentas instaladas nesta preparação

| Ferramenta | Versão verificada | Finalidade |
| --- | --- | --- |
| Colima | 0.10.3 | Gerenciar a VM do servidor |
| Lima | 2.2.1, com guest agents | Executar e controlar a VM Linux |
| Docker CLI no Mac | 29.9.0 | Controlar o Docker no contexto dedicado |
| Docker Engine na VM | 29.5.2 | Executar o container SQL Server |
| QEMU | 11.1.2 | Emular o servidor x86-64 no Apple Silicon |
| pyodbc | 5.3.0 | Executar a integração Python real, no venv privado |

Clang/C11, Python, Homebrew, unixODBC e Microsoft ODBC Driver 18 existentes
foram reutilizados. `sqlcmd` é executado dentro da imagem do servidor; não foi
necessário instalar `mssql-tools18` no Mac. O cliente C não depende de pyodbc.
O pacote Python fica somente em `.local/sqlserver/venv`.

A instalação do QEMU pelo Homebrew também instalou suas dependências e
atualizou `pcre2` para 10.49, `glib` para 2.90.1, `jpeg-turbo` para 3.2.0,
`libpng` para 1.6.59 e `xz` para 5.8.4. unixODBC e o Driver 18 permaneceram
nas versões existentes, verificadas após a instalação.

## Rede e TLS

O Mac recebe a porta SQL exclusivamente por um túnel SSH com listener em
`127.0.0.1:1433`. A publicação do container também usa `127.0.0.1:1433` dentro
da VM. O túnel usa o socket de controle curto
`~/.colima/aquasense/aqua-forward.sock` e a configuração SSH do perfil.

O encaminhamento automático de portas do Colima fica desativado com
`portForwarder: none`. Na combinação Colima 0.10.3/Lima 2.2.1, foi necessário
o wrapper privado `scripts/local-tools/limactl` para fixar
`guestIPMustBeZero=false` na regra que ignora portas do guest. Ele é selecionado
somente no processo de início do Colima. Não adicione sua pasta ao `PATH`
global. A verificação real dos listeners no Mac confirmou somente o túnel SQL
em loopback; o DNS interno do guest não foi encaminhado ao Mac.

O servidor exige criptografia TLS 1.2. O cliente mantém `Encrypt=yes` e
`TrustServerCertificate=no`. A CA privada do projeto assina um certificado
com os nomes `localhost` e `127.0.0.1`. Os wrappers definem `SSL_CERT_FILE` e
`SSL_CERT_DIR` para o processo, sem instalar a CA no chaveiro ou modificar a
confiança global do sistema.

O Driver 18 nesta máquina precisa das bibliotecas OpenSSL 3 existentes. O
wrapper define `DYLD_LIBRARY_PATH=/opt/homebrew/opt/openssl@3/lib` somente no
processo do cliente e da integração. Essa seleção resolveu a incompatibilidade
observada com a biblioteca OpenSSL 4 disponível globalmente, sem relink do
Homebrew ou alteração do driver. Execute o cliente pelos wrappers documentados
para reutilizar essa configuração.

## Credenciais e preservação dos dados

A pasta `.local/` é ignorada pelo Git. `.local/sqlserver/` e seus diretórios
privados usam permissão `0700`; as credenciais, configurações privadas e chaves
locais usam `0600`. Não compartilhe ou inclua esses arquivos em commits:

| Arquivo/pasta | Conteúdo |
| --- | --- |
| `.local/sqlserver/credentials.json` | Senhas distintas de administrador, aplicativo e testes |
| `.local/sqlserver/tls/` | CA, chaves e certificado TLS locais |
| `.local/sqlserver/docker.env` | Configuração privada de inicialização do container |
| `.local/sqlserver/image.json` | Identificação da imagem registrada |
| `.local/sqlserver/initialized.json` | Registro da inicialização concluída, sem senhas |
| `.local/sqlserver/venv/` | Ambiente Python exclusivo da integração |

`agua_cliente` e `agua_teste` pertencem apenas ao papel `agua_app` em seus
respectivos bancos. Esse papel permite consulta e cadastro de leituras e
consulta dos cadastros/metadados necessários. Não concede `UPDATE`, `DELETE`,
DDL ou `db_owner` ao aplicativo. Não houve flexibilização dessas permissões
para fazer a conexão ou os testes passarem.

O banco permanece no volume `aquasense-sqlserver-data` após o encerramento.
**Não remova esse volume nem exclua o perfil Colima ou `.local/sqlserver/`
para reiniciar o projeto.** O volume contém o banco; os arquivos privados
guardam as credenciais e a configuração necessárias para voltar a acessá-lo.
Se o estado divergir ou um arquivo estiver ausente, preserve o que existe e
revise a recuperação. Os scripts não substituem silenciosamente senhas de um
servidor existente.

O bootstrap `scripts/inicializar_sql_local.py` foi aplicado uma única vez.
Ele recusa bancos e logins preexistentes antes de criar recursos; não apaga,
reseta, adota ou migra automaticamente um banco existente. A importação v3
verificou os hashes do pacote e preservou as duas leituras originais com IDs
1 e 2, valores medidos, horários originais e instantes UTC. A confirmação de
`America/Sao_Paulo` usa offset de -180 minutos para esses registros; nenhum
fuso ou horário de coleta adicional foi presumido.

## Testes reais realizados

Em 14/10/2026, as verificações no servidor local produziram:

| Verificação | Resultado |
| --- | --- |
| TLS e autenticação administrativa pelo sqlcmd do container | PASS, com validação do certificado |
| Diagnóstico C pelo Driver 18 | PASS: conexão real, esquema 3 e metadados |
| Listagem pelo cliente C | PASS: duas leituras legadas preservadas |
| `sql/test_schema.sql`, no banco teste vazio | PASS: casos válidos, 32 rejeições com códigos esperados e rollback |
| `sql/test_smoke.sql`, no banco teste vazio | PASS: cadastro, limites, UTC, TOP(10), permissões e rollback |
| `sql/test_reports.sql` | PASS: operadores T-SQL, filtros, agregados, paginação e grupos |
| Integração Python/C normal | **18 testes aprovados e 1 ignorado**, de 19 métodos, em 10,9 segundos |
| Validação do hostname TLS, executada separadamente | PASS: 1 teste, com alias IPv4 `127.1` fora do certificado |

O caso ignorado na execução normal exige um alias que alcance o mesmo SQL
Server e não esteja no certificado. Ele foi validado separadamente com `127.1`:
a resolução para `127.0.0.1` foi conferida e uma conexão de
controle, com o nome esperado `localhost`, confirmou o mesmo servidor
`16.0.4305.5`. O cliente C com validação normal rejeitou o alias com a mensagem
`certificate verify failed: subject name does not match host name`. O método
de integração correspondente passou. Assim, os **19 métodos distintos** foram
verificados, preservando o registro exato de 18 aprovados/1 ignorado no runner
normal e 1 aprovado na execução adicional. Nenhuma configuração global de rede
ou confiança foi alterada para esse teste.

As três suítes SQL foram executadas antes da integração;
`test_schema.sql` e `test_smoke.sql` exigem banco de teste sem leituras. Seus
rollbacks preservam dados, mas os IDs de teste consumidos não são reutilizados.

Para executar novamente a integração real:

```sh
python3 scripts/servidor_local.py test
```

O comando usa o Python do venv privado, o Driver 18, o binário
`build/aquasense` e a conta `agua_teste` em `MonitoramentoAguaTest`. A suíte
completa deixa **uma nova leitura persistente no banco teste por execução**.
Não a direcione ao banco de uso local nem apague registros para repetir testes.
As consultas e os relatórios são comparados a resultados do servidor real.

## Verificações adicionais

O comando `stop` encerrou o túnel, o container e a VM. Após iniciar novamente
pelo wrapper, a conexão TLS voltou a funcionar: as duas leituras originais
mantiveram IDs, instantes UTC e medidas, e a leitura de integração permaneceu
no banco teste. O menu interativo abriu e encerrou com sucesso. O listener no
Mac continuou restrito a `127.0.0.1:1433`; o DNS do guest permaneceu sem
encaminhamento ao host.

Os resultados acima validam a execução local observada. Execução do cliente no
Windows, desempenho de produção e implantação numa plataforma oficialmente
suportada permanecem fora deste teste no Mac M4.
