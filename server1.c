/*
   Projeto Pratico 1 - Redes de Computadores - Tema: Loteria
   FASE 2 - SERVIDOR MULTI-CLIENTE

   Novidades desta fase (em relacao a Fase 1):
   - A thread principal fica em loop chamando accept() e NUNCA bloqueia
     tratando um cliente especifico: assim que aceita uma conexao, cria
     uma "thread de trabalho" para aquele cliente e volta imediatamente
     para o accept(), permitindo multiplos clientes simultaneos.
   - A thread de trabalho verifica se ha vaga disponivel (respeitando o
     limite de clientes passado por linha de comando). Se houver vaga,
     cria as duas threads de servico daquele cliente (recebe/sorteio),
     como na Fase 1, porem agora cada cliente tem sua PROPRIA config e
     lista de apostas (nao sao mais globais), garantindo que os clientes
     sejam tratados de forma totalmente independente.
   - Se nao houver vaga, o servidor informa o limite atingido (no lugar
     do "<HORARIO>: CONECTADO!!") e encerra a conexao daquele cliente.
   - O numero de clientes conectados e a lista de handlers dos clientes
     SAO dados compartilhados entre todas as threads de trabalho e a
     thread principal, por isso ficam protegidos por uma unica
     CRITICAL_SECTION global (g_lockClientes).

   Uso:
       server.exe <numero_maximo_de_clientes>
       Exemplo: server.exe 3

   Compilar (MSVC): cl server.c ws2_32.lib
   Compilar (MinGW): gcc server.c -o server.exe -lws2_32
*/

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT                5000
#define BUF_SIZE            2048
#define MAX_APOSTAS         100
#define MAX_NUMEROS         50
#define MAX_CLIENTES_ARRAY  200   /* tamanho do array de handlers (limite real vem do parametro) */

/* 
   Estrutura com os dados de UM cliente. Cada campo aqui é "instanciado
   dentro da thread" daquele cliente (nao é mais compartilhado entre
   clientes diferentes) - apenas as 2 threads do MESMO cliente acessam
   estes dados, por isso o mutex "lock" é por cliente.
*/
typedef struct {
    int numeros[MAX_NUMEROS];
    int qtd;
} Aposta;

typedef struct ClienteInfo {
    SOCKET          socket;
    int             id;
    int             inicio;
    int             fim;
    int             qtdSorteio;
    Aposta          apostas[MAX_APOSTAS];
    int             numApostas;
    CRITICAL_SECTION lock;         /* protege config/apostas deste cliente */
    volatile LONG   terminar;
    HANDLE          threadRecv;
    HANDLE          threadSorteio;
} ClienteInfo;

/*
   Dados COMPARTILHADOS entre TODAS as conexoes: contagem de clientes
   conectados e a lista de handlers, usados pela thread principal e por
   todas as threads de trabalho para saber se ha vaga disponivel.
*/
static CRITICAL_SECTION g_lockClientes;
static ClienteInfo      *g_clientes[MAX_CLIENTES_ARRAY];
static int               g_numConectados = 0;
static int               g_maxClientes   = 5; /* valor padrao, sobrescrito pelo parametro */
static int               g_proximoId     = 1;

static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

/* 
   THREAD 1 do cliente: loop de leitura do socket. Recebe comandos
   (":inicio", ":fim", ":qtd", ":sair") ou apostas (numeros separados por
   espaco) e atualiza os dados DAQUELE cliente (protegidos pelo lock dele).
*/
DWORD WINAPI threadRecebeCliente(LPVOID arg) {
    ClienteInfo *cli = (ClienteInfo *)arg;
    char buffer[BUF_SIZE];
    int n;

    while (!cli->terminar) {
        n = recv(cli->socket, buffer, BUF_SIZE - 1, 0);
        if (n <= 0) {
            printf("[Servidor] Cliente #%d desconectou.\n", cli->id);
            InterlockedExchange(&cli->terminar, 1);
            break;
        }
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        if (buffer[0] == ':') {
            if (_stricmp(buffer, ":sair") == 0) {
                printf("[Servidor] Cliente #%d solicitou encerramento.\n", cli->id);
                InterlockedExchange(&cli->terminar, 1);
                break;
            }
            char cmd[32];
            int valor;
            if (sscanf(buffer, ":%31s %d", cmd, &valor) == 2) {
                EnterCriticalSection(&cli->lock);
                if (_stricmp(cmd, "inicio") == 0)      cli->inicio = valor;
                else if (_stricmp(cmd, "fim") == 0)    cli->fim = valor;
                else if (_stricmp(cmd, "qtd") == 0)    cli->qtdSorteio = valor;
                LeaveCriticalSection(&cli->lock);
                printf("[Servidor] Cliente #%d atualizou config -> inicio=%d fim=%d qtd=%d\n",
                       cli->id, cli->inicio, cli->fim, cli->qtdSorteio);
            }
        } else {
            EnterCriticalSection(&cli->lock);
            if (cli->numApostas < MAX_APOSTAS) {
                Aposta *a = &cli->apostas[cli->numApostas];
                a->qtd = 0;
                char tmp[BUF_SIZE];
                strncpy(tmp, buffer, BUF_SIZE - 1);
                tmp[BUF_SIZE - 1] = '\0';
                char *tok = strtok(tmp, " ");
                while (tok != NULL && a->qtd < MAX_NUMEROS) {
                    a->numeros[a->qtd++] = atoi(tok);
                    tok = strtok(NULL, " ");
                }
                if (a->qtd > 0) {
                    cli->numApostas++;
                    printf("[Servidor] Cliente #%d fez uma aposta (%d numeros).\n", cli->id, a->qtd);
                }
            }
            LeaveCriticalSection(&cli->lock);
        }
    }
    return 0;
}

/*
   THREAD 2 do cliente: a cada 1 minuto sorteia numeros conforme a config
   DAQUELE cliente, confere as apostas dele e envia o resultado so pra ele.
*/
DWORD WINAPI threadSorteioCliente(LPVOID arg) {
    ClienteInfo *cli = (ClienteInfo *)arg;

    while (!cli->terminar) {
        for (int i = 0; i < 60 && !cli->terminar; i++) Sleep(1000);
        if (cli->terminar) break;

        EnterCriticalSection(&cli->lock);
        int inicio = cli->inicio;
        int fim    = cli->fim;
        int qtdSorteio = cli->qtdSorteio;
        LeaveCriticalSection(&cli->lock);

        if (qtdSorteio > MAX_NUMEROS) qtdSorteio = MAX_NUMEROS;
        if (fim < inicio) { int t = fim; fim = inicio; inicio = t; }
        int faixa = fim - inicio + 1;
        if (qtdSorteio > faixa) qtdSorteio = faixa;

        int sorteados[MAX_NUMEROS];
        int total = 0;
        while (total < qtdSorteio) {
            int n = inicio + rand() % faixa;
            int repetido = 0;
            for (int i = 0; i < total; i++) if (sorteados[i] == n) { repetido = 1; break; }
            if (!repetido) sorteados[total++] = n;
        }

        char msg[BUF_SIZE];
        char horario[16];
        obterHorario(horario, sizeof(horario));
        int pos = snprintf(msg, sizeof(msg), "%s: SORTEIO:", horario);
        for (int i = 0; i < total; i++)
            pos += snprintf(msg + pos, sizeof(msg) - pos, " %d", sorteados[i]);
        pos += snprintf(msg + pos, sizeof(msg) - pos, "\n");

        EnterCriticalSection(&cli->lock);
        if (cli->numApostas == 0) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                             "Nenhuma aposta foi feita neste ciclo.\n");
        } else {
            for (int i = 0; i < cli->numApostas; i++) {
                int acertos = 0;
                char acertosStr[256] = "";
                for (int j = 0; j < cli->apostas[i].qtd; j++) {
                    for (int k = 0; k < total; k++) {
                        if (cli->apostas[i].numeros[j] == sorteados[k]) {
                            acertos++;
                            char tmp[16];
                            snprintf(tmp, sizeof(tmp), "%d ", cli->apostas[i].numeros[j]);
                            strncat(acertosStr, tmp, sizeof(acertosStr) - strlen(acertosStr) - 1);
                            break;
                        }
                    }
                }
                pos += snprintf(msg + pos, sizeof(msg) - pos,
                                 "Aposta %d: %d acerto(s) (%s)\n", i + 1, acertos, acertosStr);
            }
        }
        cli->numApostas = 0;
        LeaveCriticalSection(&cli->lock);

        send(cli->socket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Sorteio enviado ao cliente #%d.\n", cli->id);
    }
    return 0;
}

/* 
   THREAD DE TRABALHO: uma para cada conexao aceita. Decide se ha vaga
   (respeitando g_maxClientes); se nao houver, avisa e encerra a conexao.
   Se houver, registra o cliente na lista compartilhada, envia a MSG1 e
   cria as duas threads de servico daquele cliente, aguardando ambas
   terminarem para entao liberar a vaga.
*/
DWORD WINAPI threadTrabalhoCliente(LPVOID arg) {
    ClienteInfo *cli = (ClienteInfo *)arg;
    char horario[16];
    char msg[BUF_SIZE];
    int slot = -1;

    EnterCriticalSection(&g_lockClientes);
    if (g_numConectados >= g_maxClientes) {
        LeaveCriticalSection(&g_lockClientes);

        obterHorario(horario, sizeof(horario));
        snprintf(msg, sizeof(msg),
                 "%s: SERVIDOR LOTADO! Limite de %d cliente(s) atingido. Tente novamente mais tarde.\n",
                 horario, g_maxClientes);
        send(cli->socket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Conexao recusada: limite de %d clientes atingido.\n", g_maxClientes);

        closesocket(cli->socket);
        free(cli);
        return 0;
    }

    /* Ha vaga: procura uma posicao livre no array e registra o cliente */
    for (int i = 0; i < MAX_CLIENTES_ARRAY; i++) {
        if (g_clientes[i] == NULL) { slot = i; break; }
    }
    cli->id = g_proximoId++;
    if (slot >= 0) g_clientes[slot] = cli;
    g_numConectados++;
    int totalConectados = g_numConectados;
    LeaveCriticalSection(&g_lockClientes);

    /* Inicializa os dados deste cliente (config padrao da loteria) */
    cli->inicio = 0;
    cli->fim = 100;
    cli->qtdSorteio = 5;
    cli->numApostas = 0;
    cli->terminar = 0;
    InitializeCriticalSection(&cli->lock);

    obterHorario(horario, sizeof(horario));
    snprintf(msg, sizeof(msg), "%s: CONECTADO!!\n", horario);
    send(cli->socket, msg, (int)strlen(msg), 0);
    printf("[Servidor] Cliente #%d conectado. (%d/%d vagas em uso)\n",
           cli->id, totalConectados, g_maxClientes);

    cli->threadRecv    = CreateThread(NULL, 0, threadRecebeCliente, cli, 0, NULL);
    cli->threadSorteio = CreateThread(NULL, 0, threadSorteioCliente, cli, 0, NULL);

    WaitForSingleObject(cli->threadRecv, INFINITE);
    InterlockedExchange(&cli->terminar, 1);
    WaitForSingleObject(cli->threadSorteio, INFINITE);

    CloseHandle(cli->threadRecv);
    CloseHandle(cli->threadSorteio);
    DeleteCriticalSection(&cli->lock);
    closesocket(cli->socket);

    /* Libera a vaga deste cliente */
    EnterCriticalSection(&g_lockClientes);
    for (int i = 0; i < MAX_CLIENTES_ARRAY; i++) {
        if (g_clientes[i] == cli) { g_clientes[i] = NULL; break; }
    }
    g_numConectados--;
    int restantes = g_numConectados;
    LeaveCriticalSection(&g_lockClientes);

    printf("[Servidor] Cliente #%d removido. (%d/%d vagas em uso)\n",
           cli->id, restantes, g_maxClientes);

    free(cli);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 2) {
        int valor = atoi(argv[1]);
        if (valor > 0) g_maxClientes = valor;
        else printf("Parametro invalido, usando limite padrao de %d clientes.\n", g_maxClientes);
    } else {
        printf("Uso: %s <numero_maximo_de_clientes>\n", argv[0]);
        printf("Nenhum parametro informado, usando limite padrao de %d clientes.\n", g_maxClientes);
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }
    srand((unsigned int)time(NULL));
    InitializeCriticalSection(&g_lockClientes);
    memset(g_clientes, 0, sizeof(g_clientes));

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        printf("Erro ao criar socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    if (bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Erro no bind: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    /* listen() nao bloqueia: apenas coloca o socket em modo passivo */
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Erro no listen: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    printf("Servidor de Loteria (multi-cliente) na porta %d. Limite: %d clientes.\n",
           PORT, g_maxClientes);
    printf("Aguardando conexoes...\n");

    /* Quem bloqueia é o accept(). A thread principal so aceita e delega
       o atendimento a uma thread de trabalho, voltando IMEDIATAMENTE
       para o accept() em seguida, permitindo multiplos clientes. */
    while (1) {
        struct sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            printf("Erro no accept: %d\n", WSAGetLastError());
            continue;
        }

        ClienteInfo *cli = (ClienteInfo *)malloc(sizeof(ClienteInfo));
        if (cli == NULL) {
            closesocket(clientSocket);
            continue;
        }
        memset(cli, 0, sizeof(ClienteInfo));
        cli->socket = clientSocket;

        HANDLE hWorker = CreateThread(NULL, 0, threadTrabalhoCliente, cli, 0, NULL);
        if (hWorker != NULL) {
            CloseHandle(hWorker); /* nao precisamos esperar: o worker se auto-gerencia */
        } else {
            printf("Erro ao criar thread de trabalho.\n");
            closesocket(clientSocket);
            free(cli);
        }
    }

    /* Este ponto nunca é alcancado nesta versao (encerre com Ctrl+C) */
    DeleteCriticalSection(&g_lockClientes);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
