# Preservação do ambiente macOS do AquaSense

Regra obrigatória fornecida pelo usuário: preservar o ambiente funcional existente no MacBook Air M4 (Apple Silicon / ARM64), reutilizando as ferramentas já instaladas. Ela prevalece sobre recomendações genéricas de instalação nos demais documentos.

## Diagnóstico somente de leitura — 14/10/2026

| Item | Estado verificado | Ação necessária |
| --- | --- | --- |
| Homebrew | 7.0.9; executável /opt/homebrew/bin/brew | Reutilizar |
| unixODBC | 2.3.14; odbcinst acessível pelo Terminal | Reutilizar |
| Driver Microsoft | msodbcsql18 18.7.1.1; ODBC Driver 18 for SQL Server registrado | Reutilizar |
| Clang | Apple Clang 21.0.0; alvo ARM64; /usr/bin/clang | Reutilizar |
| Command Line Tools | /Library/Developer/CommandLineTools | Reutilizar |
| Cabeçalhos | sql.h e sqlext.h em /opt/homebrew/opt/unixodbc/include | Disponíveis |
| Bibliotecas | libodbc, libodbcinst, libmsodbcsql18 e libltdl ARM64; referências coerentes | Disponíveis |
| Tap Microsoft | microsoft/mssql-release presente | Preservar |
| Fórmula msodbcsql18 | Autorização anterior informada pelo usuário; driver instalado | Não repetir autorização/configuração |
| mssql-tools18 / sqlcmd | Fórmula não listada; sqlcmd ausente do PATH | Opcional; instalação não necessária nesta fase |
| Instância SQL Server | Ainda não disponibilizada, conforme informação do usuário | Configuração pendente |

Nenhum conflito de arquitetura ou referência ao iODBC foi observado nas bibliotecas verificadas. Não há dependência obrigatória ausente nem ajuste local necessário identificado. A disponibilidade remota não foi inferida nem testada por varredura de rede.

Foram utilizados comandos de diagnóstico como `brew --version`, `brew list --versions`, `brew tap`, `clang --version`, `xcode-select -p`, `command -v`, `odbcinst -q -d`, `odbcinst -j`, `file` e `otool -L`. Não foram executadas instalações, atualizações, limpeza, reconfiguração de PATH ou alterações dos links existentes.

## Procedimento antes de cada nova fase

Apresentar ao usuário:

1. Dependências instaladas e verificadas, com versões relevantes.
2. Dependências indispensáveis ausentes, se houver.
3. Ajustes necessários sustentados por evidência, se houver.
4. Ferramentas opcionais, separadas dos requisitos do cliente C.
5. Configurações pendentes do projeto e do servidor.
6. Ações que dependem de autorização explícita.

Uma versão mais recente disponível não constitui motivo suficiente para atualizar. Verificar apenas a compatibilidade com requisitos reais. Confirmar execução pelo Terminal, cabeçalhos/bibliotecas e possíveis conflitos antes de propor instalação.

## Alterações que exigem autorização

Uma instalação ou atualização somente pode ser proposta se a ferramenta estiver ausente, incompatível com necessidade comprovada, danificada/inutilizável, ou for indispensável à funcionalidade solicitada. Antes de executar, apresentar motivo, comandos e impactos e aguardar a autorização do usuário.

A mesma exigência vale para `brew update`, `brew upgrade`, `brew reinstall`, `brew cleanup`, alteração de taps/confiança, PATH, links simbólicos, configurações globais e arquivos de sistema. A autorização anterior de uma fórmula não autoriza modificações futuras.

A regra permite trabalho no código e na documentação do projeto dentro do escopo solicitado. Não é necessário reinstalar ferramentas para editar ou compilar o programa.

## Compilação existente e pendências

O usuário informou compilação bem-sucedida com `bash scripts/compilar_macos.sh`. Nesta cópia disponibilizada em Downloads, esse caminho não foi localizado; o script existente é `build.sh`, já utilizado nas verificações anteriores. Antes de outra fase de implementação, confirmar a pasta ativa e reutilizar seu script, sem mudar ferramentas por causa de diferenças na organização do projeto.

A próxima etapa depende de uma instância Microsoft SQL Server compatível: disponibilizar endereço DNS/porta, preparar o banco MonitoramentoAgua, configurar autenticação segura e validar TLS e operações reais por ODBC. Não enviar senhas pelo chat.

O Driver 18 permite que o C se conecte ao servidor; sua instalação não instala o SQL Server. A arquitetura não pressupõe SQL Server nativo em macOS ARM64. Nenhuma instalação ou alteração do ambiente está recomendada nesta fase.
