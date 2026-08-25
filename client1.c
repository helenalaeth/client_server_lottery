/* 
   Compilar (MSVC): cl client.c ws2_32.lib
   Compilar (MinGW): gcc client.c -o client.exe -lws2_32
 */

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT     5000
#define BUF_SIZE 1024

static SOCKET        g_socket = INVALID_SOCKET;
static volatile LONG g_terminar = 0;

/* THREAD 1: le do teclado e envia pela rede (em loop, ate ":sair") */
DWORD WINAPI threadEnvia(LPVOID arg) {
    char buffer[BUF_SIZE];

    while (!g_terminar) {
        if (fgets(buffer, BUF_SIZE, stdin) == NULL) continue;
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        send(g_socket, buffer, (int)strlen(buffer), 0);

        if (_stricmp(buffer, ":sair") == 0) {
            InterlockedExchange(&g_terminar, 1);
            break;
        }
    }
    return 0;
}

/* THREAD 2: recebe dados do servidor e imprime na tela (em loop infinito) */
DWORD WINAPI threadRecebe(LPVOID arg) {
    char buffer[BUF_SIZE];
    int n;

    while (!g_terminar) {
        n = recv(g_socket, buffer, BUF_SIZE - 1, 0);
        if (n <= 0) {
            printf("\n[Cliente] Conexao encerrada pelo servidor.\n");
            InterlockedExchange(&g_terminar, 1);
            break;
        }
        buffer[n] = '\0';
        printf("%s\n", buffer);
    }
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }

    char serverIp[64];
    printf("Digite o IP do servidor (ex: 127.0.0.1): ");
    fgets(serverIp, sizeof(serverIp), stdin);
    serverIp[strcspn(serverIp, "\r\n")] = '\0';
    if (strlen(serverIp) == 0) strcpy(serverIp, "127.0.0.1");

    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);

    if (connect(g_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) {
        printf("Erro ao conectar ao servidor: %d\n", WSAGetLastError());
        closesocket(g_socket);
        WSACleanup();
        return 1;
    }

    char buffer[BUF_SIZE];
    int n = recv(g_socket, buffer, BUF_SIZE - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s\n", buffer);
    }

    printf("Digite mensagens livres para enviar ao servidor (ou :sair para encerrar).\n");
    printf("A cada 10 segundos o servidor envia uma atualizacao automatica.\n\n");

    HANDLE hThread1 = CreateThread(NULL, 0, threadEnvia, NULL, 0, NULL);
    HANDLE hThread2 = CreateThread(NULL, 0, threadRecebe, NULL, 0, NULL);

    WaitForSingleObject(hThread1, INFINITE);
    WaitForSingleObject(hThread2, INFINITE);

    CloseHandle(hThread1);
    CloseHandle(hThread2);
    closesocket(g_socket);
    WSACleanup();

    printf("Cliente encerrado.\n");
    return 0;
}