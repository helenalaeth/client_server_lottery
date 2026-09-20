/*
   Projeto Pratico 1 - Redes de Computadores - Tema: Loteria
   FASE 2 - PARTE 2: clientes com dados independentes

   Novidade desta parte (em relacao a Parte 1):
   - Corrige a limitacao da Parte 1: agora cada cliente tem sua PROPRIA
     configuracao (:inicio/:fim/:qtd) e sua PROPRIA lista de apostas,
     guardadas numa struct por cliente (ClienteInfo) com seu proprio mutex.
     Clientes diferentes nao interferem mais um no outro.

   LIMITACAO CONHECIDA desta parte (sera corrigida na Parte 3):
   - Ainda NAO ha limite de clientes simultaneos: o servidor aceita
     qualquer quantidade de conexoes, sem parametro de linha de comando
     e sem recusar quando "lotado". Isso sera implementado na Parte 3.

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

#define PORT        5000
#define BUF_SIZE    2048
#define MAX_APOSTAS 100
#define MAX_NUMEROS 50

typedef struct {
    int numeros[MAX_NUMEROS];
    int qtd;
} Aposta;

/* Agora cada cliente tem sua PROPRIA config + lista de apostas + mutex */
typedef struct {
    SOCKET          socket;
    int             id;
    int             inicio;
    int             fim;
    int             qtdSorteio;
    Aposta          apostas[MAX_APOSTAS];
    int             numApostas;
    CRITICAL_SECTION lock;
    volatile LONG   terminar;
} ClienteInfo;

static int g_proximoId = 1; /* unico dado ainda global: soh o contador de IDs */
static CRITICAL_SECTION g_lockId;

static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

/* THREAD 1 do cliente: recebe comandos/apostas (na config/lista DELE) */
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

/* THREAD 2 do cliente: a cada 1 minuto sorteia usando a config DELE e
   confere so as apostas DELE */
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
        cli->numApostas = 0; /* zera SOMENTE a lista deste cliente */
        LeaveCriticalSection(&cli->lock);

        send(cli->socket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Sorteio enviado ao cliente #%d.\n", cli->id);
    }
    return 0;
}

/* Uma thread de trabalho por conexao aceita: inicializa os dados do
   cliente, envia MSG1 e sobe as 2 threads de servico */
DWORD WINAPI threadTrabalhoCliente(LPVOID arg) {
    ClienteInfo *cli = (ClienteInfo *)arg;

    EnterCriticalSection(&g_lockId);
    cli->id = g_proximoId++;
    LeaveCriticalSection(&g_lockId);

    cli->inicio = 0;
    cli->fim = 100;
    cli->qtdSorteio = 5;
    cli->numApostas = 0;
    cli->terminar = 0;
    InitializeCriticalSection(&cli->lock);

    char horario[16];
    obterHorario(horario, sizeof(horario));
    char msg1[BUF_SIZE];
    snprintf(msg1, sizeof(msg1), "%s: CONECTADO!!\n", horario);
    send(cli->socket, msg1, (int)strlen(msg1), 0);
    printf("[Servidor] Cliente #%d conectado.\n", cli->id);

    HANDLE hRecv    = CreateThread(NULL, 0, threadRecebeCliente, cli, 0, NULL);
    HANDLE hSorteio = CreateThread(NULL, 0, threadSorteioCliente, cli, 0, NULL);

    WaitForSingleObject(hRecv, INFINITE);
    InterlockedExchange(&cli->terminar, 1);
    WaitForSingleObject(hSorteio, INFINITE);

    CloseHandle(hRecv);
    CloseHandle(hSorteio);
    DeleteCriticalSection(&cli->lock);
    closesocket(cli->socket);
    printf("[Servidor] Cliente #%d removido.\n", cli->id);

    free(cli);
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }
    srand((unsigned int)time(NULL));
    InitializeCriticalSection(&g_lockId);

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    printf("Servidor de Loteria (multi-cliente - Parte 2) na porta %d.\n", PORT);
    printf("Aguardando conexoes...\n");

    while (1) {
        struct sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) continue;

        ClienteInfo *cli = (ClienteInfo *)malloc(sizeof(ClienteInfo));
        memset(cli, 0, sizeof(ClienteInfo));
        cli->socket = clientSocket;

        HANDLE hWorker = CreateThread(NULL, 0, threadTrabalhoCliente, cli, 0, NULL);
        if (hWorker != NULL) CloseHandle(hWorker);
    }

    DeleteCriticalSection(&g_lockId);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}