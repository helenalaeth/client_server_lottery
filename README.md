# client_server_lottery

Este projeto implementa um sistema de loteria baseado em uma arquitetura cliente/servidor, onde usuários podem apostar em números sorteados periodicamente. A aplicação permite a configuração dinâmica dos parâmetros da loteria e a realização de apostas por meio de uma interface cliente que se comunica com o servidor via rede.

Funcionamento
O usuário inicia a conexão com o servidor através do cliente da aplicação e recebe uma mensagem de confirmação no formato:
"<HORÁRIO>: CONECTADO!!".

Após a conexão, duas threads são criadas tanto no cliente quanto no servidor para gerenciar a comunicação e o processamento das apostas.

O usuário pode configurar a loteria enviando comandos via teclado no cliente, utilizando o formato:

:inicio <NÚMERO> — define o número inicial do intervalo para o sorteio.
:fim <NÚMERO> — define o número final do intervalo para o sorteio.
:qtd <NÚMERO> — define a quantidade de números a serem sorteados.
Caso a loteria não seja configurada inicialmente, o sistema assume os valores padrão: intervalo de 0 a 100 e 5 números sorteados.

Para apostar, o usuário digita números separados por espaços, que são enviados ao servidor para registro.

A thread 1 do cliente fica responsável por capturar os comandos e apostas do usuário e enviá-los ao servidor continuamente até o momento do sorteio.

A thread 2 do cliente aguarda mensagens do servidor, exibindo os resultados e informações recebidas.

No servidor, a thread 1 recebe e armazena as apostas dos usuários em uma lista, aguardando novas apostas.

A thread 2 do servidor executa o sorteio a cada 1 minuto, conforme os parâmetros configurados, verifica as apostas feitas, identifica quais números foram acertados e envia os resultados para o cliente.

Após cada sorteio, a lista de apostas é zerada e um novo ciclo de apostas e sorteios é iniciado.

Tecnologias Utilizadas
Comunicação via sockets TCP/IP para conexão cliente/servidor.
Programação concorrente com threads para gerenciamento simultâneo das conexões e processos.
Interface de linha de comando para interação do usuário.
Objetivo
Este projeto tem como objetivo demonstrar a implementação prática de um sistema distribuído cliente/servidor com comunicação em rede, manipulação de threads e controle de fluxo para um cenário realista de apostas e sorteios.
