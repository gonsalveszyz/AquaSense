# AquaSense — entrega local incremental, 14/10/2026

O projeto foi evoluído no checkout existente, na branch `branch/aquasense-web-mvp`, após o checkpoint `ebcb655`. A CLI C11, os arquivos SQLite preservados e as duas leituras originais continuam disponíveis. Não foi contratada infraestrutura, feita publicação externa ou instalada uma nova dependência nesta evolução web. Clang, unixODBC, Driver 18, Python, json-c e OpenSSL 3 existentes foram reutilizados.

## Etapa 1 — Auditoria

1. **Diagnóstico:** a CLI de qualidade, relatórios ODBC, validação de entrada e servidor SQL local já funcionavam. Não havia API HTTP, interface, usuários, hidrômetros, consumo acumulado ou sessões.
2. **Implementação:** criação de um checkpoint e separação dos módulos adicionais; reaproveitamento da conexão, validação C e contratos de relatório. A opção de seguir a descrição branca e azul foi confirmada pelo usuário.
3. **Arquivos:** `docs/AUDITORIA_WEB.md`, `docs/web/CONTRATO_IMPLEMENTACAO.md` e fontes C existentes examinadas.
4. **Validação:** inventário, esquema CLI 3, ferramentas e conexão real conferidos; regressões anteriores preservadas.
5. **Pendências:** a imagem mencionada não foi recebida; não se alega fidelidade a uma referência visual ausente.
6. **Próxima etapa:** extensão aditiva, com backup verificado e versão web independente.

## Etapa 2 — Banco de dados

1. **Diagnóstico:** `dbo.Leituras` precisa permanecer intacta. `dbo.Alertas` legado exige sensor/local e aceita apenas dois status, incompatíveis com o acompanhamento web por usuário.
2. **Implementação:** extensão web 1 com usuários, sessões, preferências, medidores, consumo, tarifas, vínculo de qualidade, categorias, alertas e auditoria. Chaves compostas protegem relacionamentos de propriedade; nenhuma alteração em `MAX(SchemaVersions)=3`.
3. **Arquivos:** `sql/007_web_extension.sql`, `scripts/preparar_web_local.py`, `docs/web/MODELO_WEB.md`.
4. **Validação:** BACKUP COPY_ONLY/CHECKSUM e RESTORE VERIFYONLY aprovados primeiro em `MonitoramentoAguaTest`, depois em `MonitoramentoAgua`. Hashes completos de cinco tabelas originais iguais antes/depois; cópias privadas 0600 no Mac. Conta de serviço restrita, sem privilégios de DDL.
5. **Pendências:** backups precisam ser preservados junto do volume e das credenciais privadas. O script não faz reset ou restauração automática.
6. **Próxima etapa:** implementar regras de consumo e persistência por usuário.

## Etapa 3 — Backend

1. **Diagnóstico:** as medidas de qualidade existentes não representam consumo. A leitura acumulada precisa de antecessor no mesmo equipamento; não se deve inventar zero para a primeira leitura.
2. **Implementação:** consumo exato em litros, validação decimal, ordem temporal, rejeição de duplicação/contador reduzido/datas futuras, substituição de medidor e origem das categorias. Estimativas somam tarifas vigentes em cada intervalo; cobertura incompleta fica explícita. CLI ganhou demo offline identificado e gráficos ASCII.
3. **Arquivos:** `backend/consumption.c/h`, `backend/web_domain.c`, `demo.c/h`, `ascii_report.c/h`, CLI e listas de fontes dos builds.
4. **Validação:** 38 verificações de consumo; 12 testes demo/ASCII; ambos também aprovados com ASan/UBSan. Zero chamadas ODBC no demo. Gráfico real de pH das duas leituras originais aprovado.
5. **Pendências:** integração física de sensores, tarifa progressiva/esgoto/taxas e confirmação de vazamento exigem dados e regras adicionais.
6. **Próxima etapa:** API local e autenticação real.

## Etapa 4 — API

1. **Diagnóstico:** o frontend precisava de uma origem comum e endpoints com propriedade derivada da sessão, sem escolher usuário no JSON.
2. **Implementação:** API C11 HTTP/1.1 limitada ao loopback, arquivos estáticos em lista explícita, JSON com erros uniformes, consultas preparadas Unicode, autenticação e cookies de sessão. PBKDF2-SHA256 com 600.000 iterações/salt aleatório; apenas hash do token bearer no banco. Origin e CSRF são conferidos nas mutações; CSP e demais cabeçalhos aplicados.
3. **Arquivos:** `backend/web_server.c`, `web_auth.c/h`, `web_support.c/h`, `scripts/compilar_web.sh`, `iniciar_web_local.py/sh`.
4. **Validação:** conexão real TLS com conta web e esquema CLI/web conferidos nos dois bancos. Tratamento de SQL_NO_DATA em DML, textos Unicode e limpeza ODBC corrigidos durante a integração.
5. **Pendências:** HTTP local não é implantação HTTPS pública. O cookie não usa Secure neste servidor HTTP; uma implantação HTTPS deve incluir esse atributo e uma camada HTTP adequada. O serviço é sequencial e não foi submetido a teste de carga.
6. **Próxima etapa:** navegação e componentes visuais integrados à API.

## Etapa 5 — Interface gráfica

1. **Diagnóstico:** não havia frontend. A referência disponível era uma descrição, sem imagem anexada.
2. **Implementação:** HTML/CSS/JavaScript sem framework ou instalação adicional, layout branco/azul, navegação responsiva, formulários, estados vazios/erros/carregamento e gráficos SVG com alternativas acessíveis. Dados de usuário são inseridos como texto.
3. **Arquivos:** `frontend/index.html`, `styles.css`, `app.js`, `favicon.svg`, `docs/web/INTERFACE.md`.
4. **Validação:** sintaxe JavaScript aprovada; 88 verificações dos formatadores/CSV aprovadas, preservando DECIMAL, centavos e limites exatos com BigInt. Interface aberta no navegador com conta exclusiva de testes. Painel, consumo, categorias e histórico mostraram dados persistidos; nenhum volume ou limite foi semeado no banco de uso.
5. **Pendências:** ajuste posterior conforme a imagem de referência, se fornecida; testes de acessibilidade com usuários/dispositivos adicionais não foram executados.
6. **Próxima etapa:** concluir as seis telas e os quatro relatórios.

## Etapa 6 — Funcionalidades

1. **Diagnóstico:** dashboard, consumo, histórico, alertas, relatórios e configurações precisavam compartilhar filtros e dados consistentes.
2. **Implementação:** cards, gráficos, períodos/medidores/paginação/ordenação, cadastro de consumo/qualidade, classificação com origem e seleção paginada, alertas com três status/auditoria, perfil/preferências/tarifas/troca de medidor. Quatro relatórios com CSV e folha de impressão/PDF. Exportação conserva aba/filtros mesmo quando a interface muda durante a consulta.
3. **Arquivos:** `backend/web_domain.c`, `frontend/app.js`, estilos de impressão e contrato de integração.
4. **Validação:** navegador registrou leitura de 400 L e categoria de 50 L somente em `MonitoramentoAguaTest`, consultou as duas abas de histórico e baixou CSVs de consumo/financeiro. Relatório financeiro mostrou 37.525 L e R$243,91 para a fixture. Esses números são dados sintéticos de QA, não consumo do usuário.
5. **Pendências:** a exportação PDF usa Imprimir/Salvar como PDF do navegador. A tentativa no navegador integrado não confirmou a abertura do diálogo; o usuário informou que ele não apareceu. Nenhum PDF final foi gravado ou renderizado nesta QA. A impressão deve ser conferida em um navegador comum. Notificações são o indicador interno da aplicação, sem envio externo.
6. **Próxima etapa:** revisão de segurança e verificação dos resultados.

## Etapa 7 — Segurança e testes

1. **Diagnóstico:** a revisão encontrou riscos de truncamento Unicode, datas futuras, cálculo de tarifas, parser JSON/HTTP, limite de tentativas e resultado de gravação incerto.
2. **Implementação:** limites em unidades UTF-16, SQLWCHAR para texto das consultas, UTF-8 estrito/pares Unicode, duplicatas JSON rejeitadas em todos os níveis, enquadramento HTTP restrito, prazo total de 5 segundos para leitura HTTP, buckets de login sem evicção ativa e limpeza ODBC com duas tentativas/encerramento em falha. Mutações não confirmadas orientam consultar o histórico antes de reenviar.
3. **Arquivos:** fontes do backend, `tests/web/test_api.py`, `test_core.c`, `run_core_tests.py`, `test_consumption.c` e testes da CLI.
4. **Validação:** 19 métodos HTTP/SQL Server aprovados; 344 verificações de protocolo/autenticação aprovadas em execução normal e ASan/UBSan; envio lento encerrado em cerca de 5.001 ms. 75 regressões C, 44 testes de scripts e 12 de demo/ASCII aprovados. Sete unidades novas analisadas pelo Clang sem diagnóstico após correção. A preservação do pacote SQLite original permanece verificável pelos hashes.
5. **Pendências:** LeakSanitizer não está disponível no runtime Apple usado. Expiração após uma hora completa, falhas controladas de infraestrutura e carga concorrente não foram alegadas. Durante QA houve um timeout TCP transitório; a interface informou o resultado não confirmado, a saúde voltou a passar e o formulário foi concluído por novo envio explícito.
6. **Próxima etapa:** documentação, manifesto e execução persistente local.

## Etapa 8 — Finalização

1. **Diagnóstico:** o usuário precisa acessar a aplicação e controlar o processo sem reabrir esta conversa ou expor senhas.
2. **Implementação:** gerenciador start/status/stop com PID/log privados, verificação de comando/projeto/banco e parada normal; tarefas VS Code e documentação atualizadas. Processo desconhecido não é substituído ou encerrado.
3. **Arquivos:** `scripts/gerenciar_web_local.py`, `.vscode/tasks.json`, `README.md`, `docs/web/EXECUCAO_LOCAL.md`, `DELIVERY_MANIFEST.json` e este relatório.
4. **Validação:** o banco de uso conserva as duas leituras originais; os testes escrevem somente no banco separado. Consulte o manifesto para hashes e resultados desta entrega, e o comando status para o estado atual do servidor.
5. **Pendências:** ambiente SQL Server emulado no Mac ARM é experimental. Validação de produção oficialmente suportada, portabilidade web para Windows e exposição HTTPS são trabalhos posteriores; não houve implantação pública.
6. **Próxima etapa:** abrir `http://localhost:8080`, criar sua conta e decidir conscientemente se as leituras anteriores pertencem a ela. Cadastrar o hidrômetro, referências e tarifas reais antes de usar os relatórios como acompanhamento pessoal.

## Comandos de operação

```bash
python3 scripts/gerenciar_web_local.py start
python3 scripts/gerenciar_web_local.py status
python3 scripts/gerenciar_web_local.py stop
```

Para testes HTTP, inicie o gerenciador com `start --test` e execute `AGUA_WEB_TEST_URL=http://localhost:8081 python3 tests/web/test_api.py`. O teste recusa o banco de uso e não apaga o histórico. A documentação completa de execução e limites está em `EXECUCAO_LOCAL.md`.
