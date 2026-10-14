# Consulta à instituição — requisito de banco do AquaSense

Preparado em 14/10/2026 para o usuário encaminhar pelo canal que preferir. Nenhuma mensagem foi enviada automaticamente. A resposta acadêmica continua pendente.

## Texto para copiar

> Olá! Estamos desenvolvendo o AquaSense em C11, T-SQL e Microsoft ODBC Driver 18. O requisito informado é utilizar exclusivamente Microsoft SQL Server. O cliente está compilado no macOS Apple Silicon e também será validado em Windows.
>
> Para cumprir a atividade, precisamos de uma instância completa do Microsoft SQL Server, ou Azure SQL Database é aceito como equivalente? O Azure SQL Database é um serviço gerenciado e tem diferenças nos procedimentos de administração, backup e restauração. Podemos adaptar esses procedimentos se esse serviço for aceito.
>
> A instituição disponibiliza laboratório, computador Windows/Linux x64 ou instância SQL Server remota para o projeto? Se houver, quem administra o acesso e quais são as condições para conexão pelo MacBook, autenticação, TLS e banco exclusivo de testes?
>
> Precisamos criar/validar o banco MonitoramentoAgua, suas tabelas e relacionamentos, importar duas leituras legadas e demonstrar cadastro, consulta e preservação dos dados. As credenciais administrativas e de aplicação serão separadas. Senhas não precisam ser enviadas nesta resposta; podemos combinar um canal seguro para a configuração.

## Como a resposta orientará o projeto

| Resposta | Próxima ação |
| --- | --- |
| Exige SQL Server completo e há laboratório/instância | Confirmar DNS, porta, versão, responsável, certificado e permissões; seguir a preparação para o administrador |
| Exige SQL Server completo e não há infraestrutura | Avaliar host remoto Windows x64/Linux x86-64 compatível, uso permitido e custos antes de provisionar |
| Aceita Azure SQL Database | Verificar elegibilidade/custos e preparar scripts próprios para o serviço; manter os scripts tradicionais preservados |
| Resposta ainda inconclusiva | Preservar o cliente e os scripts existentes; manter infraestrutura e integração real pendentes |

As consultas comuns T-SQL são amplamente compartilhadas, mas Azure SQL Database não oferece todas as operações de uma instância completa. O serviço exige nova conexão para mudar de banco; a administração tradicional com `BACKUP`/`RESTORE` e dependências de `msdb` não se aplica da mesma forma. Essa distinção técnica não decide a equivalência acadêmica: a instituição precisa confirmá-la. [Diferenças oficiais entre SQL Server e Azure SQL Database](https://learn.microsoft.com/en-us/azure/azure-sql/database/transact-sql-tsql-differences-sql-server?view=azuresql).

O roteiro correspondente à instância completa está em [FASE3_PREPARACAO.md](FASE3_PREPARACAO.md). Não há necessidade de reinstalar Clang, Homebrew, unixODBC ou o Driver 18 no MacBook para fazer essa consulta ou prosseguir com a preparação.
