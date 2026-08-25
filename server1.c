/* 
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

#define PORT     5000
#define PORT            5000
#define BUF_SIZE        2048
#define MAX_APOSTAS     100
#define MAX_NUMEROS     50

typedef struct {
    int numeros[MAX_NUMEROS];
    int qtd;
} Aposta;

typedef struct {
    int inicio;
    int fim;
    int qtd;
} ConfigLoteria;

/*Dados compartilhados entre as duas threads do servidor*/
static ConfigLoteria    g_config = { 0, 100, 5 };
static Aposta           g_apostas[MAX_APOSTAS];
static int              g_numApostas = 0;
static CRITICAL_SECTION g_lock;
static volatile LONG    g_terminar = 0;
static SOCKET           g_clientSocket = INVALID_SOCKET;

static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

/* 
   THREAD 1: loop de leitura do socket. Recebe mensagens do cliente,
   incrementa o contador em memoria compartilhada (protegido por mutex)
   e verifica o comando de saida ":sair".
 */
DWORD WINAPI threadRecebeCliente(LPVOID arg) {
    char buffer[BUF_SIZE];
    int n;

    while (!g_terminar) {
        n = recv(g_clientSocket, buffer, BUF_SIZE - 1, 0);
        if (n <= 0) {
            printf("[Servidor] Cliente desconectou.\n");
            InterlockedExchange(&g_terminar, 1);
            break;
        }
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        if (buffer[0] == ':') {
            if (_stricmp(buffer, ":sair") == 0) {
                printf("[Servidor] Cliente solicitou encerramento.\n");
                InterlockedExchange(&g_terminar, 1);
                break;
            }
            char cmd[32];
            int valor;
            if (sscanf(buffer, ":%31s %d", cmd, &valor) == 2) {
                EnterCriticalSection(&g_lock);
                if (_stricmp(cmd, "inicio") == 0)      g_config.inicio = valor;
                else if (_stricmp(cmd, "fim") == 0)    g_config.fim    = valor;
                else if (_stricmp(cmd, "qtd") == 0)    g_config.qtd    = valor;
                LeaveCriticalSection(&g_lock);
                printf("[Servidor] Config atualizada -> inicio=%d fim=%d qtd=%d\n",
                       g_config.inicio, g_config.fim, g_config.qtd);
            }
        } else {
            /* Trata como aposta: numeros separados por espaco */
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
                    printf("[Servidor] Aposta recebida (%d numeros).\n", a->qtd);
                }
            } else {
                printf("[Servidor] Limite de apostas do ciclo atingido, aposta ignorada.\n");
            }
            LeaveCriticalSection(&g_lock);
        }
    }
    return 0;
}

/*
   THREAD 2: periodicamente (a cada 10 segundos nesta etapa de teste) le a
   memoria compartilhada e envia uma atualizacao ao cliente. Na etapa final,
   esta thread sera substituida pela logica de sorteio a cada 1 minuto.
*/
DWORD WINAPI threadSorteio(LPVOID arg) {
    while (!g_terminar) {
        /* Espera 1 minuto, verificando a flag de termino a cada segundo */
        for (int i = 0; i < 60 && !g_terminar; i++) Sleep(1000);
        if (g_terminar) break;

        EnterCriticalSection(&g_lock);
        int inicio = g_config.inicio;
        int fim    = g_config.fim;
        int qtdSorteio = g_config.qtd;
        LeaveCriticalSection(&g_lock);

        if (qtdSorteio > MAX_NUMEROS) qtdSorteio = MAX_NUMEROS;
        if (fim < inicio) { int t = fim; fim = inicio; inicio = t; }

        int sorteados[MAX_NUMEROS];
        int total = 0;
        int faixa = fim - inicio + 1;
        if (qtdSorteio > faixa) qtdSorteio = faixa; /* nao ha numeros suficientes */

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
        g_numApostas = 0; /* zera a lista para o proximo ciclo */
        LeaveCriticalSection(&g_lock);

        send(g_clientSocket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Sorteio enviado ao cliente:\n%s", msg);
    }
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }
    InitializeCriticalSection(&g_lock);
    srand((unsigned int)time(NULL));

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 1);

    printf("Servidor de Loteria aguardando conexao na porta %d...\n", PORT);
    printf("Configuracao padrao: numeros de %d a %d, %d sorteados por rodada.\n",
       g_config.inicio,
       g_config.fim,
       g_config.qtd);

    struct sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);
    g_clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);

    char horario[16];
    obterHorario(horario, sizeof(horario));
    char msg1[BUF_SIZE];
    snprintf(msg1, sizeof(msg1), "%s: CONECTADO!!\n", horario);
    send(g_clientSocket, msg1, (int)strlen(msg1), 0);
    printf("Cliente conectado.\n");

    HANDLE hThread1 = CreateThread(NULL, 0, threadRecebeCliente, NULL, 0, NULL);
    HANDLE hThread2 = CreateThread(NULL, 0, threadSorteio, NULL, 0, NULL);

    WaitForSingleObject(hThread1, INFINITE);
    InterlockedExchange(&g_terminar, 1);
    WaitForSingleObject(hThread2, INFINITE);

    CloseHandle(hThread1);
    CloseHandle(hThread2);
    DeleteCriticalSection(&g_lock);
    closesocket(g_clientSocket);
    closesocket(listenSocket);
    WSACleanup();

    printf("Servidor encerrado.\n");
    return 0;
}