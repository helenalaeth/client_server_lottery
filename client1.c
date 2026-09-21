/* ==========================================================================
   Projeto Pratico 1 - Redes de Computadores
   Tema: Loteria - CLIENTE
   FASE 3 - PARTE 1: deteccao e relato de excecoes de conexao
   
   Compilar (MinGW):
       gcc client.c -o client.exe -lws2_32

   Compilar (MSVC - Developer Command Prompt):
       cl client.c ws2_32.lib
   ========================================================================== */

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT     5000
#define BUF_SIZE 2048

static SOCKET          g_socket = INVALID_SOCKET;
static volatile LONG   g_terminar = 0;

/* 
   THREAD 1 (cliente): le comandos/apostas do teclado e envia pela rede.
*/
DWORD WINAPI threadEnvia(LPVOID arg) {
    char buffer[BUF_SIZE];

    while (!g_terminar) {
        if (fgets(buffer, BUF_SIZE, stdin) == NULL) continue;
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        if (send(g_socket, buffer, (int)strlen(buffer), 0) == SOCKET_ERROR) {
            int codigo = WSAGetLastError();
            printf("\n[Cliente] Excecao ao enviar dados: %s (codigo %d).\n",
                   descreverErroSocket(codigo), codigo);
            InterlockedExchange(&g_terminar, 1);
            break;
        }

        if (_stricmp(buffer, ":sair") == 0) {
            InterlockedExchange(&g_terminar, 1);
            break;
        }
    }
    return 0;
}

/* THREAD 2 (cliente): recebe dados do servidor e imprime na tela */
DWORD WINAPI threadRecebe(LPVOID arg) {
    char buffer[BUF_SIZE];
    int n;

    while (!g_terminar) {
        n = recv(g_socket, buffer, BUF_SIZE - 1, 0);
        if (n == 0) {
            /* Desconexao normal: o servidor fechou a conexao de forma limpa
               (por exemplo, atingiu o limite e recusou, ou foi encerrado) */
            printf("\n[Cliente] Conexao encerrada pelo servidor.\n");
            InterlockedExchange(&g_terminar, 1);
            break;
        }
        if (n == SOCKET_ERROR) {
            /* Excecao de rede de verdade */
            int codigo = WSAGetLastError();
            printf("\n[Cliente] Excecao de conexao com o servidor: %s (codigo %d).\n",
                   descreverErroSocket(codigo), codigo);
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
    if (g_socket == INVALID_SOCKET) {
        int codigo = WSAGetLastError();
        printf("Excecao ao criar socket: %s (codigo %d).\n", descreverErroSocket(codigo), codigo);
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) != 1) {
        printf("Endereco IP invalido.\n");
        closesocket(g_socket);
        WSACleanup();
        return 1;
    }

    if (connect(g_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) {
        int codigo = WSAGetLastError();
        printf("Excecao ao conectar ao servidor: %s (codigo %d).\n",
               descreverErroSocket(codigo), codigo);
        closesocket(g_socket);
        WSACleanup();
        return 1;
    }

    /* Recebe MSG1 de confirmacao de conexao */
    char buffer[BUF_SIZE];
    int n = recv(g_socket, buffer, BUF_SIZE - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s\n", buffer);
    } else if (n == 0) {
        printf("Conexao encerrada pelo servidor antes de receber a confirmacao.\n");
        closesocket(g_socket);
        WSACleanup();
        return 1;
    } else {
        int codigo = WSAGetLastError();
        printf("Excecao ao receber confirmacao do servidor: %s (codigo %d).\n",
               descreverErroSocket(codigo), codigo);
        closesocket(g_socket);
        WSACleanup();
        return 1;
    }

    printf("Comandos disponiveis:\n");
    printf("  :inicio <N>   -> define o menor numero sorteavel\n");
    printf("  :fim <N>      -> define o maior numero sorteavel\n");
    printf("  :qtd <N>      -> define quantos numeros serao sorteados\n");
    printf("  1 2 3 4 5     -> aposta com numeros separados por espaco\n");
    printf("  :sair         -> encerra o cliente\n\n");
    printf("Configuracao padrao (se nao definida): 0 a 100, 5 numeros sorteados.\n\n");

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
