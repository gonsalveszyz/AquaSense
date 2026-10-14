# Contrato de integração do MVP local

Backend C11 adicional em `backend/`, frontend estático em `frontend/`, HTTP local em `http://localhost:8080`. A CLI existente e seu esquema 3 continuam disponíveis. A extensão web é versionada em `WebSchemaVersions`, sem trocar `MAX(SchemaVersions)=3`.

JSON: sucesso `{ "data": ..., "error": null }`; erro `{ "data": null, "error": { "code": "...", "message": "..." } }`. Datas de API são UTC, ISO 8601; consultas usam início incluso e fim exclusivo. DECIMAL é serializado como string. Paginação usa `page`, `page_size` (máximo 100); filtros `inicio`, `fim`, `agrupamento=dia|mes` e `medidor_id` quando pertinente.

Sessão real: cookie `aqua_session`, HttpOnly, SameSite=Strict; mutações autenticadas enviam `X-CSRF-Token`. `Secure` depende de HTTPS; o ambiente de desenvolvimento é exclusivamente loopback HTTP. Não guardar senha/token de sessão em localStorage. `GET /api/v1/auth/me` retorna usuário e CSRF. Cadastro/login retornam o mesmo objeto e criam a sessão; logout revoga a sessão.

Rotas públicas: `GET /api/v1/health`; `GET /api/v1/auth/setup` (primeiro usuário e quantidade de registros sem vínculo); `POST /api/v1/auth/register` (`nome`, `email`, `senha`, `importar_legado` opcional somente primeiro usuário); `POST /api/v1/auth/login` (`email`, `senha`). Autenticadas: `GET /api/v1/auth/me`, `POST /api/v1/auth/logout`.

Rotas de domínio:

| Rota | Contrato principal |
| --- | --- |
| GET `/api/v1/dashboard` | resumo de consumo, qualidade, série, categorias e alertas, sem números inventados |
| GET `/api/v1/medidores` | lista de medidores do usuário |
| POST `/api/v1/medidores` | `nome`, `codigo`, `instalado_em_utc`, `substitui_medidor_id` opcional; unidade m³ |
| PATCH `/api/v1/medidores/{id}` | `status`: ATIVO ou INATIVO |
| GET `/api/v1/consumo/resumo` | consumo em litros, média observada, estimativa financeira ou null sem tarifa |
| GET `/api/v1/consumo/historico` | leituras acumuladas, diferenças, medidor, origem e observações, paginadas |
| POST `/api/v1/consumo/leituras` | `medidor_id`, `acumulado_m3` (decimal até três casas), `registrado_em_utc`, `observacoes`; origem manual |
| GET `/api/v1/qualidade/leituras` | leituras vinculadas ao usuário, paginadas e filtradas |
| POST `/api/v1/qualidade/leituras` | `temperatura_c`, `ph`, `turbidez_ntu`; validação C existente, vínculo atômico |
| POST `/api/v1/qualidade/importar-legado` | vínculo explícito de leituras do terminal ainda sem proprietário, apenas administrador local |
| GET `/api/v1/alertas` | lista, contagens e histórico de status; sem diagnóstico de potabilidade ou vazamento confirmado |
| PATCH `/api/v1/alertas/{id}` | `status`: ABERTO, EM_ANALISE ou RESOLVIDO; registrar auditoria |
| GET/PATCH `/api/v1/configuracoes` | perfil e preferências do usuário; limites, qualidade, unidade, tema e notificações |
| GET/POST `/api/v1/tarifas` | histórico; POST `valor_m3`, `inicio_utc`; estimativa linear pela tarifa vigente no fim do intervalo |
| GET `/api/v1/categorias` | categorias disponíveis e volumes informados, sem percentuais fictícios |
| POST `/api/v1/categorias/registros` | `leitura_id`, `categoria_id`, `litros`, `origem` (INFORMADO/MEDIDO/ESTIMADO); soma não excede consumo do intervalo |
| GET `/api/v1/relatorios/consumo` e `/financeiro` | resumo e dados dos filtros, exportados no frontend para CSV e impressão/PDF |

O objeto de login/me tem `{usuario:{id,nome,email,administrador},csrf_token}`. Preferências: `limite_diario_litros`, `limite_mensal_litros`, `vazamento_litros_minuto`, `ph_min`, `ph_max`, `temperatura_min`, `temperatura_max`, `turbidez_max` (null se ausentes), `unidade` (litros/m3), `tema` (claro/escuro), `notificacoes` (booleano).

`backend/web_support.h` é o contrato C comum. `web_domain_route` retorna 1 para rota tratada e preenche `WebResult`, 0 para rota desconhecida. Toda consulta de domínio usa `WebUser.id` autenticado, nunca um usuário escolhido no JSON. Chamadores possuem os objetos JSON retornados por `web_query`; campos DECIMAL ficam como strings. A autenticação usa `WebUsuarios`, `WebSessoes`, hash PBKDF2-SHA256 (600000 iterações), salt aleatório e hash do token de sessão.

O registro primeiro usuário tem opção explícita para vincular leituras anteriores; a criação de conta nunca deve mudar medidas, IDs ou horários originais. Alertas web podem exigir uma tabela própria por causa das FKs obrigatórias e status restritos de `dbo.Alertas` legado; documentar a justificativa e preservar todos os objetos antigos.
