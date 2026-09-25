#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define CLOSE_SOCKET(s) closesocket(s)
  #define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/select.h>
  #include <strings.h>
  typedef int socket_t;
  #define CLOSE_SOCKET(s) close(s)
  #define IS_INVALID_SOCKET(s) ((s) < 0)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 65536

static void handle_client(socket_t client_sock) {
    char buf[BUFFER_SIZE];
    int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        /* Initial connection handler placeholder */
    }
    CLOSE_SOCKET(client_sock);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(server_sock)) {
        fprintf(stderr, "Failed to create socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        CLOSE_SOCKET(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_sock, 128) != 0) {
        fprintf(stderr, "Failed to listen on socket\n");
        CLOSE_SOCKET(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("HTTP/1.1 Calculator Server listening on port %d...\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (IS_INVALID_SOCKET(client_sock)) {
            continue;
        }
        handle_client(client_sock);
    }

    CLOSE_SOCKET(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
