# Execução local da interface e API

O MVP acrescenta frontend estático e API C11 ao projeto existente. A CLI continua disponível em `scripts/executar_local.sh`; suas tabelas e o esquema 3 são preservados. A extensão web mantém uma versão independente em `WebSchemaVersions`.

## Pré-requisitos e preparação

Reutilize o ambiente macOS já configurado: Clang/C11, unixODBC, Microsoft ODBC Driver 18, Python 3, json-c, OpenSSL 3 e o perfil Colima `aquasense`. Os scripts verificam os componentes necessários e não instalam ou atualizam ferramentas. A configuração privada existente em `.local/sqlserver/` fornece conexão e CA; não copie senhas para comandos, documentação ou Git.

Na raiz do projeto:

```bash
bash scripts/iniciar_servidor_local.sh
bash scripts/compilar_web.sh
python3 scripts/preparar_web_local.py
python3 scripts/gerenciar_web_local.py start
```

A compilação gera `build/aquasense_api` e usa bibliotecas já presentes. Se estiverem em caminhos diferentes, os parâmetros `ODBC_PREFIX`, `JSON_C_PREFIX` e `OPENSSL_PREFIX` selecionam suas localizações somente para o processo de compilação; não é necessário alterar configuração global.

Na primeira preparação, o script cria backups COPY_ONLY com CHECKSUM, executa RESTORE VERIFYONLY e aplica `sql/007_web_extension.sql` primeiro no banco de testes e depois no banco de uso. Compara hashes de `Leituras`, `SchemaVersions`, `Sensores`, `LocaisMonitoramento` e `Alertas`, cria a identidade SQL restrita `agua_web` e mantém backups e credenciais em estado privado. Uma preparação já registrada não recria tabelas nem redefine senhas. Em caso de falha, preserve o banco, o backup e o estado privado para diagnóstico.

A API inicia em segundo plano e permanece disponível após fechar o terminal ou a conversa, em [localhost:8080](http://localhost:8080). O frontend é servido pela mesma origem da API. Abrir `frontend/index.html` diretamente como arquivo não fornece autenticação nem conexão com o backend.

## Primeiro uso

1. Escolha **Criar conta**, informe seu nome, e-mail e uma senha com pelo menos 12 caracteres. Não existe conta humana padrão.
2. Se for a primeira conta, escolha conscientemente se as medições anteriores do terminal pertencem a você. A opção de vínculo é desmarcada por padrão, e nenhum valor ou horário original é alterado.
3. Em **Configurações**, cadastre um hidrômetro com identificação e instalação corretas.
4. Registre leituras acumuladas em m³ em ordem temporal. A primeira leitura forma a base; a seguinte permite calcular a diferença em litros. Uma troca de equipamento inicia outra sequência.
5. Configure tarifas e referências de alerta apenas quando conhecidas. Não há volumes por categoria, preços, limites ou medições inventados.

A primeira conta é administradora local. Também pode vincular leituras anteriores ainda sem proprietário em Configurações, com confirmação explícita. As outras contas consultam apenas seus registros. Logout revoga a sessão; o navegador não guarda senha ou token de sessão em `localStorage`.

**Histórico → Todos os registros** permite visualizar medições legadas sem horário UTC confirmado. Filtros por período usam UTC e excluem essas medições; o horário original permanece disponível. A escolha de uma data final inclui o dia completo no fuso do navegador, convertido para fim exclusivo UTC na API.

## Operação e ferramentas

- `python3 scripts/gerenciar_web_local.py status` consulta o processo e a saúde da API/SQL Server.
- `python3 scripts/gerenciar_web_local.py stop` encerra somente a API gerenciada deste checkout, sem apagar registros.
- `python3 scripts/gerenciar_web_local.py start --port 8082` usa outra porta local. Use os mesmos argumentos em `status` e `stop`.
- `python3 scripts/gerenciar_web_local.py start --test` usa exclusivamente `MonitoramentoAguaTest` e, por padrão, a porta 8081. Use `status --test` ou `stop --test` para esse processo. Contas e dados de teste não aparecem no banco de uso.
- `bash scripts/iniciar_web_local.sh --check` verifica a conexão sem servir a interface.
- `bash scripts/iniciar_web_local.sh` continua disponível para depuração em primeiro plano; somente nesse modo `Ctrl+C` encerra a API do terminal.
- `python3 scripts/servidor_local.py stop` encerra o SQL Server local preservando o volume.

O gerenciador guarda PID e log em estado privado, valida o comando completo e o banco esperado, e não encerra processos de outro checkout nem substitui um servidor desconhecido na porta. A tarefa de início no VS Code usa esse gerenciador em segundo plano.

No VS Code, **Executar Tarefa** disponibiliza “AquaSense: compilar API web (macOS)”, “AquaSense: preparar banco web local (macOS)” e “AquaSense: iniciar interface web local (macOS)”, além das tarefas anteriores da CLI e do servidor. A tarefa de preparação precisa que o SQL Server esteja em execução.

## Relatórios e interpretação

O relatório financeiro soma os custos dos intervalos usando a tarifa vigente na leitura final de cada um. `tarifas_aplicadas` informa as vigências e os valores parciais; se algum intervalo não possui tarifa, a estimativa total fica indisponível e a UI informa a cobertura ausente. Não se atribui uma tarifa nova indiscriminadamente a todo o histórico. Faixas progressivas, esgoto, tributos e taxas fixas não são inferidos.

CSV usa UTF-8 com BOM, delimitador `;`, escape de aspas e proteção contra fórmulas. As leituras exportadas incluem tarifa, vigência e custo estimado individual, preservando a precisão recebida; comparações financeiras usam o período anterior calculado pela API. Para PDF, gere um relatório, escolha **Imprimir / PDF** e utilize “Salvar como PDF” no diálogo do navegador. Navegação, filtros e ações são omitidos pela folha de impressão; nenhuma biblioteca de PDF adicional é necessária.

## Limitações de execução

O HTTP é exclusivo do loopback (`127.0.0.1`) e não está configurado para exposição pública. A conexão API–SQL Server usa TLS com validação da CA local. O cookie da sessão usa HttpOnly e SameSite=Strict; `Secure` exige uma implantação HTTPS própria, fora deste servidor de desenvolvimento. Mutações autenticadas exigem token CSRF, e o servidor aplica CSP com scripts e estilos da própria origem.

SQL Server 2022 Developer no Mac M4 utiliza Linux x86-64 emulado pelo ambiente local previamente autorizado. Essa execução é experimental; o projeto não presume suporte oficial da Microsoft a SQL Server sob emulação ARM. A implantação em Windows x64/Linux x86-64 oficialmente compatível e a portabilidade do servidor web para Windows exigem validações específicas. [Requisitos oficiais do SQL Server](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

O sistema acompanha consumo e parâmetros disponíveis. Limites configurados pelo usuário não certificam potabilidade, e possível vazamento é um indício que exige verificação. Não há integração física de sensores, envio de e-mails ou notificações push; a preferência de notificações destaca o indicador de alertas abertos na aplicação.

O frontend segue a descrição branca e azul aprovada. A imagem mencionada não foi recebida, portanto não houve comparação visual com uma referência anexada. Resultados reais de API, SQL Server e navegador devem ser consultados na documentação de testes; uma verificação de sintaxe JavaScript, isoladamente, não comprova a integração.
