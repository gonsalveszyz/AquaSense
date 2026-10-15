# Organização dos arquivos do AquaSense

Consolidação realizada em **15/10/2026**. A pasta principal do projeto é:

```text
/Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql
```

Ela reúne o projeto atual e os materiais relacionados identificados nas pastas pessoais pesquisadas. A raiz permaneceu no endereço existente para preservar os caminhos usados pela API, pelo ambiente Python privado e pela documentação.

## Onde encontrar cada material

```text
monitoramento_agua_c_sql/
├── README.md                         Visão geral e links de entrada
├── backend/                          API C11 atual
├── frontend/                         Interface web atual
├── scripts/                          Compilação e execução do projeto atual
├── sql/                              Esquema e migrações atuais
├── tests/                            Testes
├── docs/
│   ├── GUIA_PROJETO.md                Guia completo e passo a passo
│   ├── ORGANIZACAO_PROJETO.md         Este índice
│   ├── CONSOLIDACAO_ARQUIVOS_20261015.json
│   └── referencias/
│       └── PROMPT_MESTRE_AQUASENSE_20261014.txt
├── legacy/
│   ├── sqlite/                       Originais históricos preservados
│   ├── versoes_recebidas/
│   │   └── AquaSense_CMD_SQLServer/   Versão anterior recebida, completa
│   └── ...                           Pontos anteriores de preservação
├── arquivos_recebidos/
│   ├── compactados/
│   │   └── AquaSense_CMD_SQLServer_v1.zip
│   └── relatorios/
│       ├── aquasense-consumo-2026-10-14.csv
│       └── aquasense-relatorio-financeiro-2026-10-14.csv
├── migration/                        Pacotes de migração
├── build/                            Executáveis atuais
├── .local/sqlserver/                 Credenciais, certificados, logs e backups
└── ...                               Módulos C e configuração existentes
```

Os arquivos ocultos continuam na pasta, inclusive `.git`, `.gitignore` e `.local`. No Finder do macOS, **Command + Shift + .** alterna a exibição de arquivos ocultos.

## O que foi reunido

| Origem | Local atual | Ação |
| --- | --- | --- |
| `Downloads/AquaSense_CMD_SQLServer/` | `legacy/versoes_recebidas/AquaSense_CMD_SQLServer/` | Pasta movida integralmente, com seus 9 arquivos |
| `Downloads/AquaSense_CMD_SQLServer_v1.zip` | `arquivos_recebidos/compactados/` | ZIP original movido, sem extração ou alteração |
| Dois CSVs AquaSense em Downloads | `arquivos_recebidos/relatorios/` | Relatórios movidos, preservando nomes e conteúdo |
| Texto de requisitos anexado à conversa | `docs/referencias/PROMPT_MESTRE_AQUASENSE_20261014.txt` | Cópia de referência; o anexo gerenciado pelo aplicativo permanece na origem |

Foram movidos **12 arquivos** e copiado **1 arquivo de referência**, totalizando **116.570 bytes**. Os hashes SHA-256 e tamanhos foram comparados antes e depois. O registro detalhado está em [CONSOLIDACAO_ARQUIVOS_20261015.json](CONSOLIDACAO_ARQUIVOS_20261015.json).

Os dois CSVs são exportações da validação com dados sintéticos do banco de testes e possuem o mesmo conteúdo das cópias já guardadas em `docs/test-results/webqa/`. Essas cópias de evidência foram preservadas.

## Qual versão executar

Execute o projeto atual pelos scripts da **raiz principal**, conforme o [guia de execução](GUIA_PROJETO.md). Para o uso diário:

```bash
cd /Users/arthurgoncalves/Downloads/monitoramento_agua_c_sql
bash scripts/iniciar_servidor_local.sh
python3 scripts/gerenciar_web_local.py start
python3 scripts/gerenciar_web_local.py status
```

Abra [http://localhost:8080](http://localhost:8080).

A pasta `legacy/versoes_recebidas/AquaSense_CMD_SQLServer/` contém uma implementação anterior destinada ao terminal. Seu código e seu esquema SQL têm contratos diferentes da versão atual. Ela foi mantida inteira para consulta, sem substituir os arquivos da aplicação atual. Os scripts e o SQL dessa pasta não foram executados na consolidação.

O texto em `docs/referencias/` registra os requisitos recebidos. Para operação e estado atual, consulte o guia e os documentos atualizados; preservar essa referência não reaplica suas instruções históricas.

## Dados e infraestrutura

As credenciais, certificados e backups privados permanecem em `.local/sqlserver/`. Os arquivos recebidos e relatórios permanecem locais e são ignorados pelo Git. O código da versão anterior e os índices de organização podem ser versionados; os executáveis gerados continuam excluídos pelas regras existentes.

O SQL Server continua utilizando o volume `aquasense-sqlserver-data`, dentro da VM Colima. O perfil Colima, o contexto Docker e as ferramentas instaladas permanecem nos locais próprios do sistema. Esses recursos são infraestrutura de execução; seus diretórios não foram movidos para dentro do projeto.

Para transferir contas e registros para outro computador, siga um procedimento de backup e restauração. Copiar somente a pasta de código não transfere o volume ativo do SQL Server. Esta organização não altera dados do banco, senhas, certificados ou configurações da VM.
