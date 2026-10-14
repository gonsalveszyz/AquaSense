# AquaSense — preservação do ambiente macOS

Esta regra foi solicitada expressamente pelo usuário e prevalece sobre orientações genéricas de instalação da documentação do projeto.

- Reutilizar o ambiente funcional do MacBook Air M4 / Apple Silicon. Homebrew, unixODBC 2.3.14, Microsoft ODBC Driver 18.7.1.1, Clang, Apple Command Line Tools e o tap microsoft/mssql-release já foram verificados. A fórmula msodbcsql18 foi previamente autorizada pelo usuário.
- Antes de cada nova fase, apresentar diagnóstico com dependências instaladas/verificadas, ausentes, ajustes necessários, opcionais, configurações pendentes e ações que exigem autorização.
- Verificar presença, versão necessária, acesso pelo Terminal, cabeçalhos, bibliotecas e conflitos por operações de leitura. Se funcional e compatível com uma necessidade real do projeto, preservar a instalação.
- Não instalar, atualizar, reinstalar ou reconfigurar ferramentas apenas para continuar o desenvolvimento ou por existir versão mais recente.
- Antes de qualquer instalação, atualização ou alteração do ambiente: demonstrar necessidade, informar motivo, comandos e possíveis impactos; aguardar autorização explícita. Autorização antiga de fórmula/tap não autoriza nova modificação do ambiente.
- Não executar brew update, brew upgrade, brew reinstall, brew cleanup, brew trust, brew link/unlink, alterações de PATH, links simbólicos, configuração global ou arquivos de sistema sem necessidade demonstrada e autorização explícita.
- Não instalar automaticamente mssql-tools18/sqlcmd: são opcionais para o cliente C por ODBC.
- O driver ODBC é cliente, não o servidor SQL Server. A pendência principal é disponibilizar uma instância compatível, preparar MonitoramentoAgua e validar a conexão real, autenticação e TLS.
- Não assumir execução nativa do SQL Server em macOS ARM64. Não executar migrações destrutivas sem backup e verificação.
- Confirmar a pasta ativa e reutilizar seu script de compilação. Nesta cópia, scripts/compilar_macos.sh reutiliza build.sh e as ferramentas existentes, gerando build/aquasense e o diagnóstico C. Não criar outra instalação ou alterar o ambiente por diferenças de organização.

Diagnóstico e evidências: docs/AMBIENTE_MACOS.md.

## Autorização de continuidade — 14/10/2026

O usuário autorizou expressamente: “Eu aprovo a continuação continua do projeto sem solicitar permissões novamente”. Essa instrução substitui os pedidos de aprovação entre fases do Prompt Mestre e os registros históricos de espera. Continuar as atividades já autorizadas no código, scripts, documentação e testes de forma incremental, sem pedir nova aprovação de fase.

Preservar o ambiente funcional e a ordem das dependências: não instalar ou reconfigurar ferramentas sem necessidade comprovada; não considerar conexão/banco validados sem SQL Server real. A autorização de continuidade não escolhe uma infraestrutura, não constitui contratação de serviços pagos e não define regras de exposição pública. O usuário informou que ainda não possui infraestrutura. Preparar opções e procedimentos sem provisionar recursos pagos ou abrir acesso externo por suposição.

## Situação da infraestrutura — 14/10/2026

O usuário confirmou que ainda precisa consultar a instituição sobre a equivalência de Azure SQL Database. A aceitação não está definida e não há host SQL Server disponível. Preparar o roteiro e consultas de leitura para uma instância completa; não escolher Azure como aceito, contratar serviços, abrir acesso público ou executar instalação/migração por suposição. As validações reais das Fases 3–5 permanecem pendentes.

## Servidor local autorizado — 14/10/2026

Nesta conversa, o usuário solicitou criar um servidor local para executar o projeto, após autorizar a instalação das ferramentas necessárias. Essa solicitação autoriza o ambiente de desenvolvimento local com Colima/Docker e emulação x86-64 por QEMU no Mac M4. Não presumir suporte oficial Microsoft à emulação ARM nem adequação para produção.

Usar exclusivamente o perfil `aquasense`, contexto Docker `colima-aquasense`, container `aquasense-sqlserver` e porta `127.0.0.1:1433`. Preservar os volumes, os originais SQLite e os bancos existentes. Credenciais/chaves ficam em `.local/sqlserver/`, ignorada pelo Git; nunca imprimir senhas ou adicioná-las a comandos/arquivos compartilhados. A CA local deve valer somente para os processos do projeto, mantendo `Encrypt=yes;TrustServerCertificate=no` e sem alterar a confiança global do Mac.

Inicializar apenas bancos novos. Importar as duas linhas do pacote v3 antes dos testes de escrita; executar testes de integração em `MonitoramentoAguaTest`, separado do banco de uso. Registrar resultados reais e limitações em `docs/SERVIDOR_LOCAL.md` quando a validação terminar.

A instância local foi criada e validada nesta conversa: conexão C/TLS, esquema 3, duas leituras originais, três suítes SQL, 19 métodos distintos de integração e persistência após parada/reinício. Usar `scripts/iniciar_servidor_local.sh` e `scripts/executar_local.sh`; os wrappers selecionam OpenSSL 3 e a CA somente por processo. O ambiente continua experimental no ARM64; Windows e infraestrutura oficialmente suportada permanecem pendentes.
