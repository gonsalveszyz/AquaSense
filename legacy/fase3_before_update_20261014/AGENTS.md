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
- Confirmar a pasta ativa e reutilizar seu script de compilação. O usuário informou sucesso com bash scripts/compilar_macos.sh; nesta cópia em Downloads existe build.sh. Não criar outra instalação ou alterar o ambiente por essa diferença de organização.

Diagnóstico e evidências: docs/AMBIENTE_MACOS.md.

## Autorização de continuidade — 14/10/2026

O usuário autorizou expressamente: “Eu aprovo a continuação continua do projeto sem solicitar permissões novamente”. Essa instrução substitui os pedidos de aprovação entre fases do Prompt Mestre e os registros históricos de espera. Continuar as atividades já autorizadas no código, scripts, documentação e testes de forma incremental, sem pedir nova aprovação de fase.

Preservar o ambiente funcional e a ordem das dependências: não instalar ou reconfigurar ferramentas sem necessidade comprovada; não considerar conexão/banco validados sem SQL Server real. A autorização de continuidade não escolhe uma infraestrutura, não constitui contratação de serviços pagos e não define regras de exposição pública. O usuário informou que ainda não possui infraestrutura. Preparar opções e procedimentos sem provisionar recursos pagos ou abrir acesso externo por suposição.
