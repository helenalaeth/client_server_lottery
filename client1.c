/* 
   Compilar (MSVC): cl client.c ws2_32.lib
   Compilar (MinGW): gcc client.c -o client.exe -lws2_32
*/

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT     5000
#define BUF_SIZE 512

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

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        printf("Erro ao criar socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) {
        printf("Erro ao conectar ao servidor: %d\n", WSAGetLastError());
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    /* Recebe MSG1 de confirmacao de conexao */
    char buffer[BUF_SIZE];
    int n = recv(clientSocket, buffer, BUF_SIZE - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s\n", buffer);
    }

    printf("Conexao com o servidor validada com sucesso.\n");

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}