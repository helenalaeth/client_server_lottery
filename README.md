# Projeto Prático 1 — Loteria (Cliente/Servidor com Sockets e Threads)

Implementação em **C** usando **Winsock2** para Windows: conexão TCP,
mensagem `MSG1` (`<HORARIO>: CONECTADO!!`), threads para comunicação
bidirecional assíncrona, suporte a múltiplos clientes simultâneos (Fase 2)
e tratamento de exceções de rede (Fase 3).

## Arquivos
- `server1.c` — servidor da loteria (multi-cliente, com tratamento de exceções)
- `client1.c` — cliente da loteria (com tratamento de exceções)

## Como compilar

### Opção A — MinGW (gcc)
```bash
gcc server1.c -o server1.exe -lws2_32
gcc client1.c -o client1.exe -lws2_32
```

### Opção B — MSVC (cl.exe)
Abra o "x64 Native Tools Command Prompt for VS" (ou rode
`vcvarsall.bat x64` antes, se precisar carregar o compilador manualmente):
```bash
cl server1.c ws2_32.lib
cl client1.c ws2_32.lib
```

## Como executar

```bash
.\server1.exe 3
```
(o número é o limite máximo de clientes simultâneos; se omitido, usa um
padrão de 5 e avisa isso no terminal)

Em outro(s) terminal(is):
```bash
.\client1.exe
```
Informe o IP do servidor quando pedido (`127.0.0.1` na mesma máquina).

## Como usar (comandos do cliente)

- `:inicio <N>` — define o menor número sorteável
- `:fim <N>` — define o maior número sorteável
- `:qtd <N>` — define quantos números serão sorteados por rodada
- `1 2 3 4 5` — faz uma aposta com números separados por espaço
- `:sair` — encerra o cliente e a conexão de forma organizada

Se nada for configurado, o padrão é: números de **0 a 100**, **5 sorteados**.
Cada cliente tem sua própria configuração e lista de apostas, totalmente
independentes dos demais clientes conectados.

A cada **1 minuto**, o servidor sorteia os números daquele cliente, confere
as apostas feitas no ciclo e envia o resultado.

## Multi-cliente (Fase 2)

- A thread principal do servidor fica em loop chamando `accept()`, criando
  uma thread de trabalho para cada cliente e voltando imediatamente ao
  `accept()`, permitindo vários clientes ao mesmo tempo.
- Essa thread de trabalho verifica se há vaga (respeitando o limite passado
  por linha de comando); se não houver, avisa o cliente e encerra a conexão.
- Quando um cliente se desconecta (`:sair` ou queda de conexão), sua vaga é
  liberada automaticamente.

## Tratamento de exceções (Fase 3)

- **Cliente e servidor diferenciam** uma desconexão normal (a outra ponta
  fechou a conexão de forma limpa) de uma **exceção de rede real** (queda
  abrupta, reset de conexão, timeout, etc.), usando `WSAGetLastError()` e
  reportando uma mensagem legível no console.
- Todas as chamadas de `send()` têm seu retorno verificado: se o envio
  falhar, a conexão é encerrada de forma controlada, sem deixar threads
  presas ou dados inconsistentes.
- O cliente detecta e reage tanto a uma **desconexão feita pelo servidor**
  quanto a **erros de conexão** (ex: servidor fora do ar, porta errada).
- O servidor detecta e reage a uma **desconexão feita pelo cliente sem o
  comando `:sair`** (fechamento abrupto), liberando a vaga normalmente.
- A montagem da mensagem de resultado do sorteio é protegida contra estouro
  de buffer (função `appendSeguro`), mesmo com muitas apostas acumuladas.
- A thread de teclado do cliente usa uma espera cancelável (não fica mais
  bloqueada indefinidamente em `fgets`), permitindo que o processo encerre
  rapidamente após uma queda de conexão, mesmo se o usuário não apertar
  Enter imediatamente.
- O servidor roda continuamente, aceitando novas conexões indefinidamente
  e mantendo a contagem de clientes conectados sempre correta, com ou sem
  desconexões abruptas.

## Observações de implementação

- Cada cliente tem sua própria estrutura de dados (configuração + lista de
  apostas), protegida por uma `CRITICAL_SECTION` própria.
- O número de clientes conectados e a lista de handlers são dados globais,
  protegidos por uma `CRITICAL_SECTION` separada.
- Cada mensagem de rede é tratada como uma linha (protocolo simplificado,
  adequado ao escopo do trabalho).

