# Interface web AquaSense

A interface em `frontend/` utiliza HTML5, CSS3 e JavaScript sem etapa de build, bibliotecas externas ou fontes remotas. O servidor C fornece os quatro arquivos estáticos e a API na mesma origem. Após preparar o banco e compilar a API conforme a documentação do ambiente local, execute:

```bash
python3 scripts/gerenciar_web_local.py start
```

Acesse `http://localhost:8080`. O gerenciador mantém a API ativa; use `python3 scripts/gerenciar_web_local.py status` ou `stop` para consultar ou encerrar. A interface segue a descrição branca e azul aprovada pelo usuário. A imagem de referência não foi fornecida; uma comparação de fidelidade visual permanece dependente dessa referência.

## Uso

- **Conta:** cadastro, login e logout reais. O primeiro administrador pode optar explicitamente por vincular as medições existentes do terminal; a opção não vem marcada.
- **Início:** consumo calculado, estimativa pela tarifa vigente na leitura final de cada intervalo, média por duração observada e parâmetros de qualidade disponíveis. Gráfico de evolução, alertas recentes e ações rápidas.
- **Consumo de Água:** períodos de 7/30 dias, 6/12 meses ou personalizado; agrupamento diário/mensal e filtro por hidrômetro. Leitura acumulada em m³, histórico e classificação manual por categoria, com origem informada, medida ou estimada.
- **Histórico:** consumo e qualidade separados, ordenação por data, paginação, filtros, detalhes e exportação CSV. “Todos os registros” permite consultar qualidade legada sem supor UTC para o horário original.
- **Alertas:** tipos, contagens, filtros, severidades, status e histórico auditável. Atualizações usam Aberto, Em análise e Resolvido.
- **Relatórios:** consumo, financeiro, alertas e detalhado. Gerar pelo período, exportar CSV ou escolher “Imprimir / PDF” e salvar como PDF pelo navegador.
- **Configurações:** perfil, unidade de exibição, aparência clara/escura, notificações na aplicação, limites, parâmetros, hidrômetros e histórico de tarifas. A substituição preserva a sequência do equipamento anterior.

Os formulários enviam os dados para a API e exibem sucesso apenas após uma resposta válida. A ausência de medições produz estados “Sem dados”; nenhum período ou percentual é preenchido artificialmente. Qualidade e indícios de vazamento não equivalem a certificação de potabilidade ou confirmação de vazamento.

## Integração e segurança

Todas as requisições usam `/api/v1` e o envelope `{data, error}`. A sessão reside no cookie HttpOnly definido pelo backend; o frontend mantém somente o token CSRF em memória e inclui `X-CSRF-Token` nas mutações autenticadas. Não há credenciais ou sessões armazenadas em `localStorage`.

`PATCH /configuracoes` envia objetos independentes `{usuario: {nome, email}}` ou `{preferencias: {...}}`, preservando os outros campos no servidor. Datas de formulários e filtros são convertidas do horário local para UTC `YYYY-MM-DDTHH:mm:ssZ`. O início é inclusivo e o fim do filtro é exclusivo, incluindo integralmente a data final escolhida. Medições legadas sem UTC confirmado exibem seu horário original; filtros UTC não as incluem. A estimativa financeira soma os custos dos intervalos conforme a vigência da tarifa em cada leitura final. Relatórios detalham as tarifas aplicadas e mantêm o total indisponível quando algum intervalo não possui cobertura.

O conteúdo retornado pela API é inserido com `textContent`/nós de texto. O único `innerHTML` é usado para ícones SVG definidos estaticamente no código. Não há scripts, eventos ou estilos inline provenientes de dados. Os arquivos respeitam a CSP de origem própria do servidor.

CSV usa UTF-8 com BOM, separador `;`, delimitadores escapados e proteção contra fórmulas de planilha. A exportação do histórico percorre as páginas; períodos maiores que 100 mil registros exigem um filtro menor. Impressão usa CSS específico e omite navegação, formulários e ações interativas.

## Acessibilidade e verificação

Campos possuem labels, erros são anunciados, gráficos incluem descrição e tabela equivalente, pontos podem ser focados por teclado, diálogos possuem foco contido e o menu móvel fechado fica inerte. A interface se adapta a telefone/tablet/desktop e respeita `prefers-reduced-motion`.

Verificação de sintaxe executada:

```bash
node --check frontend/app.js
```

Esse comando passou; ele não substitui a validação integrada da API nem a verificação visual em navegador. Os resultados integrados devem ser registrados na documentação de testes do projeto.
