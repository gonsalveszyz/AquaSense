# Escolha de infraestrutura SQL Server — Fase 3

Situação em 14/10/2026: o MacBook M4 tem o cliente ODBC instalado; ainda não há servidor disponível. Este guia prepara a escolha. Nenhuma assinatura, máquina virtual, regra de firewall ou serviço foi criado.

## Opções para o AquaSense

| Opção | Adequação ao projeto | Infraestrutura e custos |
| --- | --- | --- |
| SQL Server remoto de laboratório/faculdade | Mantém a instância completa e os scripts atuais, sujeito às permissões concedidas pelo administrador | Consultar disponibilidade, acesso remoto, certificado e política do laboratório; não pressupor gratuidade |
| SQL Server Developer em computador Windows x64 | Instância completa para desenvolvimento/teste; usar edição e sistema operacional compatíveis | Edição Developer gratuita para uso não produtivo; requer outro computador ou VM x64. Energia, hardware e hospedagem podem ter custo |
| SQL Server Developer em Linux x86-64 remoto | Instância completa; permite os scripts tradicionais atuais | Requer distribuição suportada para a versão escolhida e administração do host. VM, disco, rede e backups podem ter custo |
| Azure SQL Database | Serviço de banco gerenciado, condicionado à aceitação acadêmica como equivalente funcional | Requer conta/assinatura elegível, limites de oferta gratuita e adaptação dos scripts administrativos. Não é instância completa |

Para preservar a exigência de Microsoft SQL Server e aproveitar a modelagem existente, a primeira alternativa a verificar é uma instância remota da instituição. Se ela não estiver disponível, uma máquina Windows/Linux x64 com Developer atende ao desenvolvimento. O SQL Server 2025 oferece Developer para desenvolvimento/testes; a edição não autoriza uso em produção. [Edições oficiais](https://learn.microsoft.com/en-us/sql/sql-server/editions-and-components-of-sql-server-2025), [Downloads oficiais](https://www.microsoft.com/en-us/sql-server/sql-server-downloads).

No Windows, confirme a combinação de versão, edição e sistema operacional na matriz oficial antes da instalação. O mecanismo SQL Server é suportado em processadores x64. [Requisitos do SQL Server 2025 em Windows](https://learn.microsoft.com/en-us/sql/sql-server/install/hardware-and-software-requirements-for-installing-sql-server-2025).

No Linux, utilize distribuição e versão suportadas pelo SQL Server escolhido. Contêineres oficiais exigem host Linux Intel/AMD x86-64; emulação ou tradução ARM por Rosetta/QEMU não é testada nem suportada. O MacBook permanece cliente e não recebe uma instalação de SQL Server. [Plataformas Linux e política de contêineres](https://learn.microsoft.com/en-us/sql/linux/install-upgrade/setup?view=sql-server-ver17).

## Se a escolha for SQL Server completo

Antes de provisionar, registrar: host disponível, Windows/Linux x64, edição/versão, administrador responsável, uso acadêmico/não produtivo e custos previstos. A licença gratuita Developer não torna uma VM gratuita.

O procedimento do administrador é:

1. Instalar somente o mecanismo necessário no host escolhido usando o instalador ou repositório oficial correspondente. No caso de instância existente, conferir sua configuração antes de alterar.
2. Configurar TCP com porta fixa, inicialmente `1433` se livre, e um DNS que corresponda ao certificado do serviço. Restringir acesso ao cliente/rede autorizada; evitar exposição geral à Internet.
3. Configurar certificado de autenticação de servidor vigente, SAN para o DNS escolhido e cadeia confiável no cliente. `Encrypt=yes` e `TrustServerCertificate=no` continuam obrigatórios. [Requisitos de certificado](https://learn.microsoft.com/en-us/sql/database-engine/configure-windows/certificate-requirements?view=sql-server-ver17).
4. Criar `MonitoramentoAgua` e instalar o esquema 3 seguindo a sequência do README. A conta administrativa é distinta de `agua_app`; associar o usuário de aplicação somente aos papéis necessários. Usar SQL Authentication para o cliente atual ou autenticação integrada quando a infraestrutura SSPI/Kerberos estiver pronta. Não usar `sa` no aplicativo.
5. Configurar backup, verificar restauração em banco separado e registrar onde o administrador controla início/parada do serviço. Não interromper uma instância compartilhada para testar indisponibilidade.
6. Entregar somente DNS, porta, banco e método de autenticação. Fornecer a senha diretamente ao ambiente local, sem chat, Git ou scripts compartilhados.

Não há comando de instalação universal: ele depende do host escolhido. A Fase 3 produzirá o procedimento exato para aquele host antes de qualquer modificação externa. Os scripts locais desta fase não instalam nada e não abrem firewall.

## Se Azure SQL Database for aceito

Azure SQL Database oferece o mecanismo T-SQL como serviço gerenciado, com administração de banco. Não expõe as mesmas operações de instância, sistema operacional e bancos de sistema do SQL Server completo. `USE` não muda para outro banco, `msdb` não é uma dependência disponível para estes scripts, e `BACKUP DATABASE`/`RESTORE DATABASE` não são procedimentos administrativos do serviço. [Diferenças T-SQL oficiais](https://learn.microsoft.com/en-us/azure/azure-sql/database/transact-sql-tsql-differences-sql-server?view=azuresql).

**Não executar os scripts tradicionais atuais diretamente no Azure.** `000` cria o banco e muda contexto; `004`/`006` verificam histórico de backup no `msdb`; `administracao.sql` contém operações de backup/restauração tradicionais. Após confirmar a aceitação acadêmica, criar scripts separados, conectados diretamente a `MonitoramentoAgua`, com usuários contidos/permissões e plano de recuperação próprios. As consultas do cliente são uma base reaproveitável, mas a compatibilidade real exige testes.

A oferta oficial atualmente permite até 10 bancos por assinatura, cada um com 100.000 vCore-segundos, 32 GB de dados e 32 GB de backup por mês. Exige conta e assinatura Azure; verificar no portal a elegibilidade, região e disponibilidade do botão de aplicação da oferta. Selecionar **Auto-pause the database until next month** ao atingir os limites, quando disponível, em vez de continuar com cobranças adicionais. Essa pausa pode impedir testes até o mês seguinte. Não criar o recurso se a oferta não estiver explicitamente aplicada e as condições/custos não tiverem sido conferidos. [Oferta gratuita](https://learn.microsoft.com/en-us/azure/azure-sql/database/free-offer?view=azuresql), [Perguntas frequentes](https://learn.microsoft.com/en-us/azure/azure-sql/database/free-offer-faq?view=azuresql).

O endpoint típico é `nome.database.windows.net`, banco `MonitoramentoAgua`, porta TCP `1433`. A política de conexão pode exigir outras portas; planejar a rede conforme a política realmente escolhida. Não liberar todas as origens ou serviços Azure por conveniência. [Arquitetura e portas de conexão](https://learn.microsoft.com/en-us/azure/azure-sql/database/connectivity-architecture?view=azuresql), [Regras de firewall](https://learn.microsoft.com/en-us/azure/azure-sql/database/firewall-configure?view=azuresql). O cliente atual suporta `AGUA_AUTH=sql`; login Entra exige implementação específica ainda não oferecida por `AGUA_AUTH=integrated`.

O serviço cria backups automáticos e permite recuperação para outro banco dentro da retenção configurada. Verificar se a proteção inicial já está disponível antes de importar; testar recuperação separadamente. BACPAC exporta esquema/dados, mas não substitui uma política de recuperação para um ponto no tempo. [Backups e recuperação do Azure SQL Database](https://learn.microsoft.com/en-us/azure/azure-sql/database/automated-backups-overview?view=azuresql).

## Validação depois de disponibilizar o destino

No MacBook, nenhuma reinstalação é necessária:

```bash
bash scripts/verificar_ambiente_macos.sh
bash scripts/compilar_macos.sh
bash scripts/compilar_diagnostico_macos.sh
export AGUA_SERVER='sql.exemplo.interno'
export AGUA_PORT='1433'
export AGUA_DATABASE='MonitoramentoAgua'
export AGUA_AUTH='sql'
export AGUA_USER='agua_cliente'
bash scripts/testar_conexao.sh
```

O script solicita a senha sem eco se ela não estiver no ambiente do processo. Não a escreva no comando. O destino acima é ilustrativo; substitua pelo DNS real do certificado. O teste deve verificar DNS/porta, conexão TLS/autenticação, banco, consulta simples, esquema/tabelas e desconexão. Uma falha de rede não comprova rejeição de senha ou certificado.

Somente depois dessa validação executar cadastro/consulta e a integração em banco exclusivo de testes. Persistência, integridade referencial, migração e TLS continuam pendentes enquanto não houver destino real.
