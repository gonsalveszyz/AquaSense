# Protótipo de monitoramento de água em C + SQL (macOS)

Este projeto didático compila em C e usa SQLite como banco de dados SQL local. Ele permite digitar manualmente temperatura, pH e turbidez, armazenar as leituras no arquivo `monitoramento_agua.db` e consultar as dez mais recentes.

**Limitação importante:** esta primeira versão não lê sensores nem determina se a água é potável. Os números devem ser tratados como dados de teste ou medições inseridas por uma pessoa. Para medições reais, será preciso adquirir sensores adequados, calibrá-los e integrar um microcontrolador ou interface compatível.

## 1. Instalar as ferramentas de compilação

Abra o Terminal do macOS e execute:

```bash
xcode-select --install
```

Se o macOS informar que as ferramentas já estão instaladas, siga para a etapa seguinte. Não é obrigatório instalar o Xcode completo.

## 2. Entrar na pasta do projeto

No Terminal, use `cd` para chegar à pasta onde descompactou este projeto. Por exemplo, se ela estiver em Downloads:

```bash
cd ~/Downloads/monitoramento_agua_c_sql
```

## 3. Compilar o programa

```bash
clang monitoramento.c -o monitoramento -lsqlite3
```

Se a compilação terminar sem mensagens de erro, um executável chamado `monitoramento` será criado.

## 4. Executar

```bash
./monitoramento
```

Escolha `1` para registrar uma leitura de teste, `2` para consultar o histórico e `3` para sair. Digite decimais com ponto (por exemplo, `7.25`).

O arquivo `monitoramento_agua.db` será criado automaticamente na pasta atual. As leituras continuarão salvas depois que o programa for encerrado.

## 5. Consultar o banco diretamente em SQL

Se o comando `sqlite3` estiver disponível, execute:

```bash
sqlite3 monitoramento_agua.db
```

Em seguida, cole uma das consultas abaixo e finalize com `;`:

```sql
SELECT * FROM leituras ORDER BY id DESC;
SELECT COUNT(*) FROM leituras;
```

Para sair do console SQLite, digite `.quit`. As consultas adicionais estão em `consultas.sql`.

Se `sqlite3` não estiver disponível no Terminal, tente `brew install sqlite` caso já tenha o Homebrew instalado. O macOS normalmente já inclui a biblioteca SQLite usada na etapa de compilação, mas a disponibilidade do utilitário de terminal pode variar conforme a configuração.

## Estrutura da tabela

- `id`: identificador da leitura.
- `registrado_em`: data e hora local em que o banco registrou a entrada.
- `temperatura_c`: temperatura em graus Celsius.
- `ph`: valor de pH.
- `turbidez_ntu`: turbidez em unidades nefelométricas (NTU).

Os limites no programa são apenas validações básicas de entrada, não limites oficiais de qualidade da água.

## Próxima etapa: sensores reais

Uma arquitetura comum é: sondas/sensores compatíveis → circuito condicionador e microcontrolador (por exemplo, ESP32) → USB serial ou Wi-Fi → programa no MacBook → SQLite. O sensor não deve ser ligado diretamente à porta USB do MacBook. Verifique compatibilidade elétrica, faixa de medição, calibração, resistência à água e segurança do conjunto. Para um sistema de uso real, defina o objetivo do monitoramento e use métodos adequados para cada parâmetro.

## Solução de problemas

- `clang: command not found`: instale as Command Line Tools com `xcode-select --install`.
- `sqlite3.h file not found` ou erro ao localizar a biblioteca: confirme que as Command Line Tools estão instaladas e atualizadas; se persistir, informe a mensagem completa do compilador.
- `zsh: permission denied`: rode `chmod +x monitoramento` e depois `./monitoramento`.
- Para recomeçar com um banco vazio, feche o programa e apague `monitoramento_agua.db`. Isso exclui o histórico local; faça uma cópia antes se quiser preservar os dados.
