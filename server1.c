/*
   Projeto Pratico 1 - Redes de Computadores - Tema: Loteria

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

typedef struct {
    int inicio;
    int fim;
    int qtd;
} ConfigLoteria;

/* Dados AINDA compartilhados entre TODOS os clientes (limitacao desta parte) */
static ConfigLoteria    g_config = { 0, 100, 5 };
static Aposta           g_apostas[MAX_APOSTAS];
static int              g_numApostas = 0;
static CRITICAL_SECTION g_lock;

/*
   Cada conexao tem seu proprio socket e sua propria flag de termino, para
   que o comando ":sair" de UM cliente nao derrube os demais.
*/
typedef struct {
    SOCKET        socket;
    int           id;
    volatile LONG terminar;
} Conexao;

static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

/* THREAD 1 da conexao: recebe comandos/apostas (na config/lista GLOBAL) */
DWORD WINAPI threadRecebeCliente(LPVOID arg) {
    Conexao *con = (Conexao *)arg;
    char buffer[BUF_SIZE];
    int n;

    while (!con->terminar) {
        n = recv(con->socket, buffer, BUF_SIZE - 1, 0);
        if (n <= 0) {
            printf("[Servidor] Cliente #%d desconectou.\n", con->id);
            InterlockedExchange(&con->terminar, 1);
            break;
        }
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        if (buffer[0] == ':') {
            if (_stricmp(buffer, ":sair") == 0) {
                printf("[Servidor] Cliente #%d solicitou encerramento.\n", con->id);
                InterlockedExchange(&con->terminar, 1);
                break;
            }
            char cmd[32];
            int valor;
            if (sscanf(buffer, ":%31s %d", cmd, &valor) == 2) {
                EnterCriticalSection(&g_lock);
                if (_stricmp(cmd, "inicio") == 0)      g_config.inicio = valor;
                else if (_stricmp(cmd, "fim") == 0)    g_config.fim = valor;
                else if (_stricmp(cmd, "qtd") == 0)    g_config.qtd = valor;
                LeaveCriticalSection(&g_lock);
                printf("[Servidor] Cliente #%d atualizou config -> inicio=%d fim=%d qtd=%d\n",
                       con->id, g_config.inicio, g_config.fim, g_config.qtd);
            }
        } else {
            EnterCriticalSection(&g_lock);
            if (g_numApostas < MAX_APOSTAS) {
                Aposta *a = &g_apostas[g_numApostas];
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
                    g_numApostas++;
                    printf("[Servidor] Cliente #%d fez uma aposta (%d numeros).\n", con->id, a->qtd);
                }
            }
            LeaveCriticalSection(&g_lock);
        }
    }
    return 0;
}

/* THREAD 2 da conexao: a cada 1 minuto sorteia (usando a config GLOBAL) e
   envia o resultado para O SOCKET DESTA conexao */
DWORD WINAPI threadSorteioCliente(LPVOID arg) {
    Conexao *con = (Conexao *)arg;

    while (!con->terminar) {
        for (int i = 0; i < 60 && !con->terminar; i++) Sleep(1000);
        if (con->terminar) break;

        EnterCriticalSection(&g_lock);
        int inicio = g_config.inicio;
        int fim = g_config.fim;
        int qtdSorteio = g_config.qtd;
        LeaveCriticalSection(&g_lock);

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

        EnterCriticalSection(&g_lock);
        if (g_numApostas == 0) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                             "Nenhuma aposta foi feita neste ciclo.\n");
        } else {
            for (int i = 0; i < g_numApostas; i++) {
                int acertos = 0;
                char acertosStr[256] = "";
                for (int j = 0; j < g_apostas[i].qtd; j++) {
                    for (int k = 0; k < total; k++) {
                        if (g_apostas[i].numeros[j] == sorteados[k]) {
                            acertos++;
                            char tmp[16];
                            snprintf(tmp, sizeof(tmp), "%d ", g_apostas[i].numeros[j]);
                            strncat(acertosStr, tmp, sizeof(acertosStr) - strlen(acertosStr) - 1);
                            break;
                        }
                    }
                }
                pos += snprintf(msg + pos, sizeof(msg) - pos,
                                 "Aposta %d: %d acerto(s) (%s)\n", i + 1, acertos, acertosStr);
            }
        }
        g_numApostas = 0; /* zera a lista GLOBAL, afetando todos os clientes */
        LeaveCriticalSection(&g_lock);

        send(con->socket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Sorteio enviado ao cliente #%d.\n", con->id);
    }
    return 0;
}

/* Uma thread de trabalho por conexao aceita: envia MSG1 e sobe as 2 threads */
DWORD WINAPI threadTrabalhoCliente(LPVOID arg) {
    Conexao *con = (Conexao *)arg;

    char horario[16];
    obterHorario(horario, sizeof(horario));
    char msg1[BUF_SIZE];
    snprintf(msg1, sizeof(msg1), "%s: CONECTADO!!\n", horario);
    send(con->socket, msg1, (int)strlen(msg1), 0);
    printf("[Servidor] Cliente #%d conectado.\n", con->id);

    HANDLE hRecv    = CreateThread(NULL, 0, threadRecebeCliente, con, 0, NULL);
    HANDLE hSorteio = CreateThread(NULL, 0, threadSorteioCliente, con, 0, NULL);

    WaitForSingleObject(hRecv, INFINITE);
    InterlockedExchange(&con->terminar, 1);
    WaitForSingleObject(hSorteio, INFINITE);

    CloseHandle(hRecv);
    CloseHandle(hSorteio);
    closesocket(con->socket);
    printf("[Servidor] Cliente #%d removido.\n", con->id);

    free(con);
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }
    srand((unsigned int)time(NULL));
    InitializeCriticalSection(&g_lock);

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN); /* listen() nao bloqueia, so prepara o socket */

    printf("Servidor de Loteria (multi-cliente - Parte 1) na porta %d.\n", PORT);
    printf("Aguardando conexoes...\n");

    int proximoId = 1;

    /* A thread principal so aceita conexoes e delega o atendimento a uma
       thread de trabalho, voltando IMEDIATAMENTE ao accept() em seguida. */
    while (1) {
        struct sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) continue;

        Conexao *con = (Conexao *)malloc(sizeof(Conexao));
        con->socket = clientSocket;
        con->id = proximoId++;
        con->terminar = 0;

        HANDLE hWorker = CreateThread(NULL, 0, threadTrabalhoCliente, con, 0, NULL);
        if (hWorker != NULL) CloseHandle(hWorker);
    }

    DeleteCriticalSection(&g_lock);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}