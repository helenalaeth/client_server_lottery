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
#define BUF_SIZE 1024

/* ---- Dados compartilhados entre as duas threads do servidor ---- */
static CRITICAL_SECTION g_lock;
static int               g_contadorMensagens = 0; /* estrutura de dados protegida */
static volatile LONG     g_terminar = 0;
static SOCKET            g_clientSocket = INVALID_SOCKET;

static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

/* -------------------------------------------------------------------------
   THREAD 1: loop de leitura do socket. Recebe mensagens do cliente,
   incrementa o contador em memoria compartilhada (protegido por mutex)
   e verifica o comando de saida ":sair".
   ------------------------------------------------------------------------- */
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

        if (_stricmp(buffer, ":sair") == 0) {
            printf("[Servidor] Cliente solicitou encerramento.\n");
            InterlockedExchange(&g_terminar, 1);
            break;
        }

        EnterCriticalSection(&g_lock);
        g_contadorMensagens++;
        int total = g_contadorMensagens;
        LeaveCriticalSection(&g_lock);

        printf("[Servidor] Mensagem recebida: \"%s\" (total ate agora: %d)\n", buffer, total);
    }
    return 0;
}

/* -------------------------------------------------------------------------
   THREAD 2: periodicamente (a cada 10 segundos nesta etapa de teste) le a
   memoria compartilhada e envia uma atualizacao ao cliente. Na etapa final,
   esta thread sera substituida pela logica de sorteio a cada 1 minuto.
   ------------------------------------------------------------------------- */
DWORD WINAPI threadEnviaPeriodico(LPVOID arg) {
    while (!g_terminar) {
        for (int i = 0; i < 10 && !g_terminar; i++) Sleep(1000);
        if (g_terminar) break;

        EnterCriticalSection(&g_lock);
        int total = g_contadorMensagens;
        LeaveCriticalSection(&g_lock);

        char horario[16];
        obterHorario(horario, sizeof(horario));
        char msg[BUF_SIZE];
        snprintf(msg, sizeof(msg), "%s: Servidor ativo. Total de mensagens recebidas: %d\n",
                 horario, total);

        send(g_clientSocket, msg, (int)strlen(msg), 0);
        printf("[Servidor] Atualizacao enviada ao cliente.\n");
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

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 1);

    printf("Servidor de Loteria aguardando conexao na porta %d...\n", PORT);

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
    HANDLE hThread2 = CreateThread(NULL, 0, threadEnviaPeriodico, NULL, 0, NULL);

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