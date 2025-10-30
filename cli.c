// win_command_client_loop.c
// Compile (MinGW): gcc win_command_client_loop.c -o client.exe -lws2_32
// Compile (MSVC): cl win_command_client_loop.c ws2_32.lib

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 8080
#define BUF_SIZE 4096
#define END_MARKER "\n--END-OF-OUTPUT--\n"

int recv_until_marker(SOCKET s, const char *marker) {
    char buffer[BUF_SIZE + 1];
    int marker_len = (int)strlen(marker);
    char tail[128] = {0};

    while (1) {
        int n = recv(s, buffer, BUF_SIZE, 0);
        if (n == 0) return 1;
        if (n < 0) {
            fprintf(stderr, "recv failed: %d\n", WSAGetLastError());
            return -1;
        }
        buffer[n] = '\0';

        char tmp[sizeof(tail) + BUF_SIZE + 1];
        snprintf(tmp, sizeof(tmp), "%s%s", tail, buffer);
        char *pos = strstr(tmp, marker);
        if (pos != NULL) {
            int upto = (int)(pos - tmp);
            if (upto > 0) fwrite(tmp, 1, upto, stdout);
            return 0;
        } else {
            if (tail[0] != '\0') {
                fputs(tail, stdout);
                tail[0] = '\0';
            }
            fputs(buffer, stdout);

            int copy = (int)min((size_t)(sizeof(tail)-1), (size_t)n);
            if (copy > 0) {
                memcpy(tail, buffer + n - copy, copy);
                tail[copy] = '\0';
            }
        }
    }
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    char server_ip[64];
    printf("Enter server IP (default 127.0.0.1): ");
    fgets(server_ip, sizeof(server_ip), stdin);

    // trim CR/LF
    size_t len = strlen(server_ip);
    while (len && (server_ip[len-1] == '\n' || server_ip[len-1] == '\r')) server_ip[--len] = '\0';
    if (len == 0) strcpy(server_ip, "127.0.0.1");

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);
    serv.sin_addr.s_addr = inet_addr(server_ip);

    printf("[*] Connecting to %s:%d ...\n", server_ip, SERVER_PORT);
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
        fprintf(stderr, "connect failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("[*] Connected. Type commands (empty line to quit).\n");

    char line[1024];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) { line[--L] = '\0'; }

        if (L == 0) break;

        char sendbuf[2048];
        snprintf(sendbuf, sizeof(sendbuf), "%s\n", line);
        if (send(sock, sendbuf, (int)strlen(sendbuf), 0) == SOCKET_ERROR) {
            fprintf(stderr, "send failed: %d\n", WSAGetLastError());
            break;
        }

        int r = recv_until_marker(sock, END_MARKER);
        if (r == 1) {
            printf("\n[*] Server closed connection.\n");
            break;
        } else if (r < 0) {
            printf("\n[*] Receive error.\n");
            break;
        } else {
            printf("\n-- end of output --\n");
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
