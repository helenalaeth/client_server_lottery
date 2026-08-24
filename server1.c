/* 
   Compilar (MSVC): cl server.c ws2_32.lib
   Compilar (MinGW): gcc server.c -o server.exe -lws2_32
*/

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT     5000
#define BUF_SIZE 512

/* Retorna o horario atual formatado HH:MM:SS */
static void obterHorario(char *buf, size_t tam) {
    time_t t = time(NULL);
    struct tm *tmInfo = localtime(&t);
    strftime(buf, tam, "%H:%M:%S", tmInfo);
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }

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

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        printf("Erro no listen: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    printf("Servidor de Loteria aguardando conexao na porta %d...\n", PORT);

    struct sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);
    SOCKET clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);
    if (clientSocket == INVALID_SOCKET) {
        printf("Erro no accept: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    /* Envia MSG1 de confirmacao da conexao */
    char horario[16];
    obterHorario(horario, sizeof(horario));
    char msg1[BUF_SIZE];
    snprintf(msg1, sizeof(msg1), "%s: CONECTADO!!\n", horario);
    send(clientSocket, msg1, (int)strlen(msg1), 0);
    printf("Cliente conectado. MSG1 enviada: %s", msg1);

    /* Nesta etapa o servidor apenas confirma a conexao.
       Threads e logica de apostas/sorteio virao nas proximas etapas. */
    printf("Pressione ENTER para encerrar o servidor...\n");
    getchar();

    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}