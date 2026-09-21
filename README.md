# Projeto Prático 1 — Loteria (Cliente/Servidor com Sockets e Threads)

Implementação em **C** usando **Winsock2** para Windows, seguindo a especificação:
conexão TCP, mensagem `MSG1` (`<HORARIO>: CONECTADO!!`), threads para
comunicação bidirecional assíncrona, e suporte a múltiplos clientes
simultâneos (Fase 2).

## Arquivos
- `server.c` — servidor da loteria (multi-cliente)
- `client.c` — cliente da loteria

## Como compilar

### Opção A — MinGW (gcc)
```bash
gcc server.c -o server.exe -lws2_32
gcc client.c -o client.exe -lws2_32
```

### Opção B — MSVC (cl.exe)
Abra o "Developer Command Prompt for VS":
```bash
cl server.c ws2_32.lib
cl client.c ws2_32.lib
```

> Dica no VS Code: instale a extensão **C/C++** da Microsoft. Se usar MinGW,
> garanta que o `gcc` esteja no PATH (`gcc --version` deve funcionar no terminal).

## Como executar

O servidor recebe o número máximo de clientes simultâneos como parâmetro de
linha de comando:

```bash
server.exe 3
```

Se rodar sem parâmetro, ele usa um limite padrão de 5 clientes e avisa isso
no terminal.

Depois, abra quantos clientes quiser testar, cada um em um terminal:

```bash
client.exe
```

Ele vai perguntar o IP do servidor (use `127.0.0.1` se for na mesma máquina).

Se você tentar conectar mais clientes do que o limite permite, o cliente
excedente recebe uma mensagem de **"SERVIDOR LOTADO"** em vez do
`CONECTADO!!`, e a conexão é encerrada automaticamente.

## Como usar (comandos do cliente)

- `:inicio <N>` — define o menor número sorteável
- `:fim <N>` — define o maior número sorteável
- `:qtd <N>` — define quantos números serão sorteados por rodada
- `1 2 3 4 5` — faz uma aposta com números separados por espaço
- `:sair` — encerra o cliente e a conexão

Se nada for configurado, o padrão é: números de **0 a 100**, **5 sorteados**.
Cada cliente tem sua própria configuração e lista de apostas — independentes
dos demais clientes conectados.

A cada **1 minuto**, o servidor sorteia os números daquele cliente, verifica
quantos números de cada aposta feita no ciclo bateram com o sorteio, envia o
resultado e reinicia a lista de apostas para o próximo ciclo.

## Como funciona o multi-cliente (Fase 2)

- A thread principal do servidor fica em loop chamando `accept()`. Assim que
  aceita uma conexão, ela cria uma **thread de trabalho** para aquele
  cliente e volta imediatamente para o `accept()`, permitindo vários
  clientes ao mesmo tempo.
- Essa thread de trabalho verifica se ainda há vaga disponível (respeitando
  o limite passado por linha de comando). Se não houver, avisa o cliente e
  encerra a conexão; se houver, registra o cliente e cria as duas threads de
  serviço dele (recebe apostas/comandos e envia sorteios).
- Quando um cliente se desconecta (`:sair` ou queda de conexão), sua vaga é
  liberada automaticamente para outro cliente entrar.

## Observações de implementação

- Cada cliente tem sua própria estrutura de dados (configuração + lista de
  apostas), protegida por uma `CRITICAL_SECTION` própria — os dois threads
  daquele cliente (recebe/sorteio) compartilham esse mutex.
- O número de clientes conectados e a lista de handlers são dados
  **globais**, compartilhados entre todas as conexões, por isso ficam numa
  `CRITICAL_SECTION` separada.
- Cada mensagem de rede é tratada como uma linha (protocolo simplificado,
  adequado ao escopo do trabalho — não há fragmentação/remontagem de pacotes
  TCP maiores que o buffer).
